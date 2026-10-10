//
// CameraRayTest.cpp - screenPointToRay and intersectUnitQuad
//
// Tier 1: pure maths, no GPU. Rays are checked against the camera's own
// projection: a world point projected to a pixel must lie on the ray through
// that pixel.
//

#include <gtest/gtest.h>

#include "ECS/CameraRay.h"
#include "ECS/Core.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Shoonyakasha::ECS;

namespace {

constexpr float kEps = 1e-4f;
const glm::vec2 kScreen(1280.0f, 720.0f);

struct TestCamera {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
};

TestCamera makeCamera(CameraComponent::Type type, const glm::vec3& position,
                      const glm::vec3& lookDirection) {
    CameraComponent camera;
    camera.type = type;
    camera.aspectRatio = kScreen.x / kScreen.y;

    TransformComponent transform;
    transform.position = position;
    transform.rotation = TransformComponent::rotationFacing(lookDirection);

    TestCamera out;
    out.view = glm::inverse(transform.getLocalMatrix());
    out.projection = camera.getProjectionMatrix();
    out.position = position;
    out.forward = transform.getForward();
    return out;
}

/// Window pixel (top-left origin, y down) where `world` appears.
glm::vec2 projectToPixel(const TestCamera& camera, const glm::vec3& world) {
    glm::vec4 clip = camera.projection * camera.view * glm::vec4(world, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return glm::vec2((ndc.x + 1.0f) * 0.5f * kScreen.x, (1.0f - ndc.y) * 0.5f * kScreen.y);
}

float distanceToRay(const Ray& ray, const glm::vec3& point) {
    const glm::vec3 toPoint = point - ray.origin;
    return glm::length(toPoint - glm::dot(toPoint, ray.direction) * ray.direction);
}

void expectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = kEps) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

} // namespace

// ── screenPointToRay ────────────────────────────────────────────

TEST(CameraRay, CentrePixelLooksAlongForward) {
    const auto camera = makeCamera(CameraComponent::Perspective, {1, 2, 3}, {1, -0.5f, -2});
    const Ray ray = screenPointToRay(kScreen * 0.5f, kScreen, camera.view, camera.projection);

    expectVec3Near(ray.direction, camera.forward);
    EXPECT_NEAR(glm::length(ray.direction), 1.0f, kEps);
    // The origin lies on the near plane (0.1 by default) in front of the camera.
    expectVec3Near(ray.origin, camera.position + camera.forward * 0.1f, 1e-3f);
}

TEST(CameraRay, TopRowPointsUpAndLeftColumnPointsLeft) {
    const auto camera = makeCamera(CameraComponent::Perspective, {0, 0, 0}, {0, 0, -1});

    const Ray top = screenPointToRay({kScreen.x * 0.5f, 0.0f}, kScreen, camera.view, camera.projection);
    EXPECT_GT(top.direction.y, 0.0f);

    const Ray left = screenPointToRay({0.0f, kScreen.y * 0.5f}, kScreen, camera.view, camera.projection);
    EXPECT_LT(left.direction.x, 0.0f);
}

TEST(CameraRay, RayPassesThroughProjectedWorldPoints) {
    const auto camera = makeCamera(CameraComponent::Perspective, {-2, 1.5f, 4}, {0.3f, -0.2f, -1});
    const glm::vec3 points[] = {
        {0, 0, 0}, {-1.5f, 1, -2}, {2, -0.5f, 1}, {0.4f, 3, -6}, {-3, -1, -1}
    };

    for (const auto& point : points) {
        const glm::vec2 pixel = projectToPixel(camera, point);
        const Ray ray = screenPointToRay(pixel, kScreen, camera.view, camera.projection);
        EXPECT_LT(distanceToRay(ray, point), 1e-3f)
            << "point (" << point.x << ", " << point.y << ", " << point.z << ")";
        EXPECT_GT(glm::dot(point - ray.origin, ray.direction), 0.0f);
    }
}

TEST(CameraRay, OrthographicRaysAreParallelWithDistinctOrigins) {
    const auto camera = makeCamera(CameraComponent::Orthographic, {0, 5, 0}, {0, -1, -1});

    const Ray a = screenPointToRay({10.0f, 20.0f}, kScreen, camera.view, camera.projection);
    const Ray b = screenPointToRay({1000.0f, 600.0f}, kScreen, camera.view, camera.projection);

    expectVec3Near(a.direction, camera.forward);
    expectVec3Near(b.direction, camera.forward);
    EXPECT_GT(glm::length(a.origin - b.origin), 1.0f);

    const glm::vec3 point(1.0f, 0.5f, -2.0f);
    const Ray through = screenPointToRay(projectToPixel(camera, point), kScreen,
                                         camera.view, camera.projection);
    EXPECT_LT(distanceToRay(through, point), 1e-3f);
}

// ── intersectUnitQuad ───────────────────────────────────────────

namespace {

/// A 2 x 1 quad centred at (0, 1, -5), facing +Z.
const glm::mat4 kQuad = glm::scale(glm::translate(glm::mat4(1.0f), {0, 1, -5}), {2, 1, 1});

Ray rayTowards(const glm::vec3& origin, const glm::vec3& target) {
    return Ray{origin, glm::normalize(target - origin)};
}

} // namespace

TEST(CameraRay, QuadHitAtCentre) {
    const auto hit = intersectUnitQuad(rayTowards({0, 1, 0}, {0, 1, -5}), kQuad);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 5.0f, kEps);
    EXPECT_NEAR(hit->uv.x, 0.5f, kEps);
    EXPECT_NEAR(hit->uv.y, 0.5f, kEps);
    EXPECT_TRUE(hit->frontFacing);
}

TEST(CameraRay, QuadUvStartsAtTopLeft) {
    // World (-0.5, 1.25) is a quarter of the width from the left edge and a
    // quarter of the height from the top edge.
    const auto hit = intersectUnitQuad(rayTowards({0, 0, 0}, {-0.5f, 1.25f, -5}), kQuad);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->uv.x, 0.25f, kEps);
    EXPECT_NEAR(hit->uv.y, 0.25f, kEps);
}

TEST(CameraRay, QuadHitDistanceIsInWorldUnitsWhenScaledAndRotated) {
    const glm::mat4 world = glm::scale(
        glm::rotate(glm::translate(glm::mat4(1.0f), {3, 0, 0}), glm::radians(90.0f), {0, 1, 0}),
        {4, 4, 1});
    // Rotated 90 degrees about Y, the quad faces +X. A ray from x = 10 hits it
    // 7 units away.
    const auto hit = intersectUnitQuad(rayTowards({10, 0, 0}, {3, 0, 0}), world);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 7.0f, kEps);
    EXPECT_TRUE(hit->frontFacing);
}

TEST(CameraRay, QuadBackSideIsHitAndReported) {
    const auto hit = intersectUnitQuad(rayTowards({0, 1, -10}, {0, 1, -5}), kQuad);
    ASSERT_TRUE(hit.has_value());
    EXPECT_FALSE(hit->frontFacing);
}

TEST(CameraRay, QuadMisses) {
    EXPECT_FALSE(intersectUnitQuad(rayTowards({0, 1, 0}, {1.2f, 1, -5}), kQuad))
        << "outside the quad's width";
    EXPECT_FALSE(intersectUnitQuad(Ray{{0, 1, 0}, {0, 0, 1}}, kQuad))
        << "quad behind the origin";
    EXPECT_FALSE(intersectUnitQuad(Ray{{0, 1, 0}, {1, 0, 0}}, kQuad))
        << "parallel to the quad";
}
