//
// LocalShadowsTest.cpp - spot and point light shadow matrices and slots
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>
#include "FrameGraph/LocalShadows.h"
#include "FrameGraph/DotPathResolver.h"
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <random>
#include <stdexcept>

using namespace Shoonyakasha;

namespace {

/// The cube-map face and texel coordinates Vulkan uses for direction r
/// (specification, "Cube Map Face Selection and Transformations").
struct CubeTexel { int face; float s, t; };
CubeTexel vulkanCubeLookup(const glm::vec3& r) {
    const glm::vec3 a = glm::abs(r);
    float sc, tc, ma;
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = r.x > 0 ? 0 : 1; ma = r.x;
        sc = r.x > 0 ? -r.z : r.z; tc = -r.y;
    } else if (a.y >= a.z) {
        face = r.y > 0 ? 2 : 3; ma = r.y;
        sc = r.x; tc = r.y > 0 ? r.z : -r.z;
    } else {
        face = r.z > 0 ? 4 : 5; ma = r.z;
        sc = r.z > 0 ? r.x : -r.x; tc = -r.y;
    }
    return {face, 0.5f * (sc / std::abs(ma) + 1.0f), 0.5f * (tc / std::abs(ma) + 1.0f)};
}

glm::vec3 project(const glm::mat4& m, const glm::vec3& p) {
    const glm::vec4 c = m * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}

} // namespace

TEST(PointShadowFaces, RenderWhereACubeLookupReads) {
    const glm::vec3 light(1.0f, 2.0f, -3.0f);
    const float nearPlane = 0.05f, farPlane = 20.0f;
    const auto faces = pointShadowFaceViewProj(light, nearPlane, farPlane);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < 500; ++i) {
        glm::vec3 r(u(rng), u(rng), u(rng));
        if (glm::length(r) < 0.1f) continue;
        r = glm::normalize(r) * (0.5f + 10.0f * std::abs(u(rng)));
        const auto lookup = vulkanCubeLookup(r);
        const glm::vec3 ndc = project(faces[lookup.face], light + r);
        // Rendering puts NDC (x, y) at framebuffer ((x + 1) / 2, (y + 1) / 2).
        EXPECT_NEAR((ndc.x + 1.0f) * 0.5f, lookup.s, 1e-4f) << "face " << lookup.face;
        EXPECT_NEAR((ndc.y + 1.0f) * 0.5f, lookup.t, 1e-4f) << "face " << lookup.face;
        // Depth from the major-axis distance, as the shader recomputes it.
        const float m = std::max(std::abs(r.x), std::max(std::abs(r.y), std::abs(r.z)));
        const float A = farPlane / (farPlane - nearPlane), B = -farPlane * nearPlane / (farPlane - nearPlane);
        EXPECT_NEAR(ndc.z, A + B / m, 1e-4f);
        EXPECT_GE(ndc.z, 0.0f);
        EXPECT_LE(ndc.z, 1.0f);
    }
}

TEST(PointShadowFaces, EachFaceSeesOnlyItsSideOfTheLight) {
    const auto faces = pointShadowFaceViewProj(glm::vec3(0.0f), 0.1f, 10.0f);
    const glm::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f) {
        for (int g = 0; g < 6; ++g) {
            const glm::vec4 clip = faces[f] * glm::vec4(axes[g] * 3.0f, 1.0f);
            if (f == g) {
                EXPECT_GT(clip.w, 0.0f);
                EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-5f);
                EXPECT_NEAR(clip.y / clip.w, 0.0f, 1e-5f);
            } else {
                // Behind the face, or on the edge of it at best.
                EXPECT_TRUE(clip.w <= 0.0f || std::abs(clip.x) >= clip.w - 1e-5f ||
                            std::abs(clip.y) >= clip.w - 1e-5f) << f << " sees " << g;
            }
        }
    }
}

TEST(SpotShadow, CoversTheConeAlongItsAxis) {
    const glm::vec3 position(0.0f, 4.0f, 0.0f), direction(0.0f, -1.0f, 0.0f);
    const float cosOuter = std::cos(glm::radians(30.0f));
    const glm::mat4 vp = spotShadowViewProj(position, direction, cosOuter, 0.05f, 10.0f);

    const glm::vec3 onAxis = project(vp, position + direction * 3.0f);
    EXPECT_NEAR(onAxis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(onAxis.y, 0.0f, 1e-5f);
    EXPECT_GT(project(vp, position + direction * 6.0f).z, onAxis.z);   // farther is deeper

    // The cone's edge is inside the map, with a little room.
    const glm::vec3 edgeDir = glm::normalize(glm::vec3(std::tan(glm::radians(30.0f)), -1.0f, 0.0f));
    const glm::vec3 edge = project(vp, position + edgeDir * 3.0f);
    EXPECT_LT(std::max(std::abs(edge.x), std::abs(edge.y)), 1.0f);
    EXPECT_GT(std::max(std::abs(edge.x), std::abs(edge.y)), 0.85f);
}

// ── Slots ───────────────────────────────────────────────────────

namespace {

LocalShadowCandidate candidate(uint32_t index, bool spot, glm::vec3 position, float intensity, float range = 8.0f) {
    LocalShadowCandidate c;
    c.lightIndex = index;
    c.spot = spot;
    c.position = position;
    c.direction = glm::vec3(0, -1, 0);
    c.intensity = intensity;
    c.range = range;
    return c;
}

} // namespace

TEST(LocalShadowSlots, BudgetsKeepTheMostImportantLights) {
    LocalShadowSettings settings;
    settings.spotCount = 2;
    settings.pointCount = 1;
    const std::vector<LocalShadowCandidate> lights = {
        candidate(0, true,  {0, 3, -5}, 1.0f),
        candidate(1, true,  {0, 3, -5}, 5.0f),
        candidate(2, true,  {0, 3, -60}, 5.0f),   // far from the camera
        candidate(3, false, {2, 3, -4}, 1.0f),
        candidate(4, false, {2, 3, -4}, 3.0f),
    };
    const auto s = assignLocalShadows(lights, glm::vec3(0.0f), glm::mat4(1.0f), true, settings);
    ASSERT_EQ(s.spotCount, 2u);
    EXPECT_EQ(s.spot[0].lightIndex, 1);
    EXPECT_EQ(s.spot[1].lightIndex, 0);
    EXPECT_EQ(s.spot[2].lightIndex, -1);
    ASSERT_EQ(s.pointCount, 1u);
    EXPECT_EQ(s.point[0].lightIndex, 4);
    EXPECT_FLOAT_EQ(s.point[0].positionFar.w, 8.0f);
    EXPECT_GT(s.spot[0].params.x, 0.0f);   // texel size at one unit
}

TEST(LocalShadowSlots, LightsThatCannotReachTheViewAreSkipped) {
    const glm::mat4 camera = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f) *
                             glm::lookAtRH(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    LocalShadowSettings settings;
    const auto s = assignLocalShadows({candidate(0, false, {0, 0, 30}, 10.0f, 5.0f),     // behind, out of reach
                                       candidate(1, false, {0, 0, 3}, 1.0f, 5.0f)},      // behind, reaches in
                                      glm::vec3(0.0f), camera, true, settings);
    ASSERT_EQ(s.pointCount, 1u);
    EXPECT_EQ(s.point[0].lightIndex, 1);
}

// ── In the scene context ────────────────────────────────────────

class LocalShadowScene : public testing::Test {
protected:
    void SetUp() override {
        auto camera = registry.create();
        registry.emplace<ECS::TransformComponent>(camera).position = glm::vec3(0.0f, 2.0f, 8.0f);
        auto& cam = registry.emplace<ECS::CameraComponent>(camera);
        cam.isMainCamera = true;
        cam.fov = 60.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 200.0f;

        auto sun = registry.create();
        registry.emplace<ECS::TransformComponent>(sun);
        registry.emplace<ECS::LightComponent>(sun).type = ECS::LightComponent::Directional;

        spot = registry.create();
        registry.emplace<ECS::TransformComponent>(spot).position = glm::vec3(0.0f, 4.0f, 0.0f);
        registry.emplace<ECS::LightComponent>(spot).type = ECS::LightComponent::Spot;

        point = registry.create();
        registry.emplace<ECS::TransformComponent>(point).position = glm::vec3(2.0f, 1.0f, 0.0f);
        registry.emplace<ECS::LightComponent>(point).type = ECS::LightComponent::Point;

        transformSystem.update(registry, 0.016f);
        cameraSystem.update(registry, 0.016f);
    }

    entt::registry registry;
    entt::entity spot{}, point{};
    ECS::TransformSystem transformSystem;
    ECS::CameraSystem cameraSystem;
    SceneContext scene;
    DotPathResolver resolver;
};

TEST_F(LocalShadowScene, OnlyLightsThatCastShadowsGetSlots) {
    scene.updateFromRegistry(registry);
    EXPECT_EQ(scene.localShadow.shadows.spotCount, 0u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot.count", scene).as<uint32_t>(), 0u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].lightIndex", scene).as<int32_t>(), -1);
}

TEST_F(LocalShadowScene, CastingLightsArePublished) {
    registry.get<ECS::LightComponent>(spot).castShadows = true;
    registry.get<ECS::LightComponent>(point).castShadows = true;
    scene.updateFromRegistry(registry);

    const auto& local = scene.localShadow.shadows;
    ASSERT_EQ(local.spotCount, 1u);
    ASSERT_EQ(local.pointCount, 1u);
    EXPECT_EQ(scene.lights[local.spot[0].lightIndex].positionType.w, 2.0f);    // a spot light
    EXPECT_EQ(scene.lights[local.point[0].lightIndex].positionType.w, 1.0f);   // a point light

    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot.count", scene).as<uint32_t>(), 1u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point.count", scene).as<uint32_t>(), 1u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].lightIndex", scene).as<int32_t>(),
              local.spot[0].lightIndex);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.spot[0].viewProj", scene).as<glm::mat4>(), local.spot[0].viewProj);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point[0].positionFar", scene).as<glm::vec4>(),
              local.point[0].positionFar);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.point.faces[3].viewProj", scene).as<glm::mat4>(),
              local.point[0].faceViewProj[3]);
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.spot[8].viewProj", scene).isValid());
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.point.faces[24].viewProj", scene).isValid());
}

// ── Pass views ──────────────────────────────────────────────────

TEST(LocalShadowViews, ParseAndCheckTheirSlot) {
    using nlohmann::json;
    auto graph = [](const std::string& view) {
        return json{{"version", 1},
                    {"resources", json::array({{{"name", "map"}, {"kind", "image"},
                                                {"image", {{"format", "D32_SFLOAT"}}}}})},
                    {"passes", json::array({{{"name", "Casters"}, {"type", "graphics"},
                                             {"execution", {{"type", "shadow_casters"}, {"view", view}}},
                                             {"outputs", json::array({{{"resource", "map"},
                                                                       {"usage", "depth_write"}}})}}})}};
    };
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, graph("shadows.spot[3]"));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.view, FrameGraph::CullView::SpotShadow);
    EXPECT_EQ(b.getPassDeclarations()[0].execution.viewIndex, 3u);
    FrameGraph::loadGraphFromJson(b, graph("shadows.point.faces[23]"));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.view, FrameGraph::CullView::PointShadowFace);
    EXPECT_EQ(b.getPassDeclarations()[0].execution.viewIndex, 23u);
    for (const char* bad : {"shadows.spot[8]", "shadows.point.faces[24]", "shadows.spot[a]", "shadows.point[0]"}) {
        FrameGraph::FrameGraphBuilder c;
        EXPECT_THROW(FrameGraph::loadGraphFromJson(c, graph(bad)), std::runtime_error) << bad;
    }
}
