//
// ShadowCascadesTest.cpp - sun cascade splits and fitting, and their dot-paths
//
// Tier 1 (math) and Tier 3 (SceneContext with an EnTT registry). No GPU.
//

#include <gtest/gtest.h>
#include "FrameGraph/ShadowCascades.h"
#include "FrameGraph/DotPathResolver.h"
#include "ECS/Core.h"
#include "ECS/Systems.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace Shoonyakasha;

namespace {

constexpr float kFov = 55.0f;
constexpr float kAspect = 16.0f / 9.0f;
constexpr float kNear = 0.1f;
constexpr float kFar = 500.0f;
const glm::vec3 kSun = glm::normalize(glm::vec3(-0.45f, -0.55f, 0.7f));

glm::mat4 cameraAt(const glm::vec3& eye, const glm::vec3& target) {
    return glm::lookAtRH(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

/// World-space corners of the view frustum between view depths n and f.
std::array<glm::vec3, 8> sliceCorners(const glm::mat4& view, float n, float f) {
    const glm::mat4 inv = glm::inverse(view);
    const float ty = std::tan(glm::radians(kFov) * 0.5f);
    const float tx = ty * kAspect;
    std::array<glm::vec3, 8> out{};
    int k = 0;
    for (float d : {n, f})
        for (float sx : {-1.0f, 1.0f})
            for (float sy : {-1.0f, 1.0f})
                out[k++] = glm::vec3(inv * glm::vec4(sx * tx * d, sy * ty * d, -d, 1.0f));
    return out;
}

/// Where a world point falls in cascade i's shadow map, in texels.
glm::vec2 texelOf(const SunShadowCascades& c, uint32_t i, const glm::vec3& p, float resolution) {
    const glm::vec4 clip = c.viewProj[i] * glm::vec4(p, 1.0f);
    return glm::vec2(clip) * (resolution * 0.5f);
}

} // namespace

// ── Splits ──────────────────────────────────────────────────────

TEST(ShadowCascades, EvenAndLogarithmicSplits) {
    const auto even = cascadeSplitDepths(1.0f, 81.0f, 4, 0.0f);
    EXPECT_FLOAT_EQ(even[0], 1.0f);
    EXPECT_FLOAT_EQ(even[1], 21.0f);
    EXPECT_FLOAT_EQ(even[2], 41.0f);
    EXPECT_FLOAT_EQ(even[4], 81.0f);

    const auto log = cascadeSplitDepths(1.0f, 81.0f, 4, 1.0f);
    EXPECT_NEAR(log[1], 3.0f, 1e-4f);
    EXPECT_NEAR(log[2], 9.0f, 1e-4f);
    EXPECT_NEAR(log[3], 27.0f, 1e-3f);
    EXPECT_FLOAT_EQ(log[4], 81.0f);
}

TEST(ShadowCascades, SplitsIncreaseAndUnusedOnesRepeatTheLast) {
    const auto d = cascadeSplitDepths(0.1f, 60.0f, 3, 0.75f);
    for (int i = 0; i < 3; ++i) EXPECT_LT(d[i], d[i + 1]);
    EXPECT_FLOAT_EQ(d[3], 60.0f);
    EXPECT_FLOAT_EQ(d[4], 60.0f);
}

// ── Fitting ─────────────────────────────────────────────────────

TEST(ShadowCascades, EachCascadeContainsItsSliceOfTheView) {
    SunShadowSettings settings;
    const glm::mat4 view = cameraAt({3.0f, 2.0f, 9.0f}, {-1.0f, 0.5f, -6.0f});
    const auto c = computeSunCascades(view, kFov, kAspect, kNear, kFar, kSun, settings);
    ASSERT_TRUE(c.valid);
    ASSERT_EQ(c.count, 4u);

    const auto depths = cascadeSplitDepths(kNear, settings.maxDistance, 4, settings.splitLambda);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(c.splits[static_cast<int>(i)], depths[i + 1]);
        for (const auto& corner : sliceCorners(view, depths[i], depths[i + 1])) {
            const glm::vec4 clip = c.viewProj[i] * glm::vec4(corner, 1.0f);
            EXPECT_LE(std::abs(clip.x), 1.0f + 1e-3f) << "cascade " << i;
            EXPECT_LE(std::abs(clip.y), 1.0f + 1e-3f) << "cascade " << i;
            EXPECT_GE(clip.z, -1e-4f) << "cascade " << i;
            EXPECT_LE(clip.z, 1.0f + 1e-4f) << "cascade " << i;
        }
    }
}

TEST(ShadowCascades, TurningTheCameraKeepsTheTexelSize) {
    SunShadowSettings settings;
    const glm::vec3 eye{0.0f, 2.0f, 5.0f};
    const auto a = computeSunCascades(cameraAt(eye, {0, 2, -5}), kFov, kAspect, kNear, kFar, kSun, settings);
    const auto b = computeSunCascades(cameraAt(eye, {7, -1, 3}), kFov, kAspect, kNear, kFar, kSun, settings);
    EXPECT_EQ(a.texelWorldSize, b.texelWorldSize);
}

TEST(ShadowCascades, MovingTheCameraShiftsTheMapByWholeTexels) {
    SunShadowSettings settings;
    const float res = static_cast<float>(settings.resolution);
    const glm::vec3 target{0.0f, 0.0f, -10.0f};
    const glm::vec3 probe{4.3f, 0.7f, -12.9f};

    const auto a = computeSunCascades(cameraAt({0.0f, 2.0f, 5.0f}, target), kFov, kAspect, kNear, kFar, kSun, settings);
    for (const glm::vec3 move : {glm::vec3(0.013f, 0.0f, 0.0f), glm::vec3(0.37f, 0.05f, -1.21f),
                                 glm::vec3(-3.3f, 0.2f, 7.77f)}) {
        const auto b = computeSunCascades(cameraAt(glm::vec3(0.0f, 2.0f, 5.0f) + move, target + move),
                                          kFov, kAspect, kNear, kFar, kSun, settings);
        for (uint32_t i = 0; i < 4; ++i) {
            const glm::vec2 shift = texelOf(b, i, probe, res) - texelOf(a, i, probe, res);
            EXPECT_NEAR(shift.x, std::round(shift.x), 2e-2f) << "cascade " << i;
            EXPECT_NEAR(shift.y, std::round(shift.y), 2e-2f) << "cascade " << i;
        }
    }
}

TEST(ShadowCascades, TexelSizeFollowsResolution) {
    SunShadowSettings settings;
    const glm::mat4 view = cameraAt({0, 2, 5}, {0, 0, -5});
    const auto hi = computeSunCascades(view, kFov, kAspect, kNear, kFar, kSun, settings);
    settings.resolution = 1024;
    const auto lo = computeSunCascades(view, kFov, kAspect, kNear, kFar, kSun, settings);
    for (int i = 0; i < 4; ++i) EXPECT_FLOAT_EQ(lo.texelWorldSize[i], 2.0f * hi.texelWorldSize[i]);
}

TEST(ShadowCascades, CascadeCountIsClampedAndUnusedEntriesRepeat) {
    SunShadowSettings settings;
    settings.cascadeCount = 2;
    const auto c = computeSunCascades(cameraAt({0, 2, 5}, {0, 0, -5}), kFov, kAspect, kNear, kFar, kSun, settings);
    EXPECT_EQ(c.count, 2u);
    EXPECT_EQ(c.splits.z, c.splits.y);
    EXPECT_EQ(c.viewProj[3], c.viewProj[1]);

    settings.cascadeCount = 9;
    EXPECT_EQ(computeSunCascades(cameraAt({0, 2, 5}, {0, 0, -5}), kFov, kAspect, kNear, kFar, kSun, settings).count,
              MAX_SUN_CASCADES);
}

TEST(ShadowCascades, ShortFarPlaneLimitsTheDistance) {
    SunShadowSettings settings;
    settings.maxDistance = 100.0f;
    const auto c = computeSunCascades(cameraAt({0, 2, 5}, {0, 0, -5}), kFov, kAspect, kNear, 40.0f, kSun, settings);
    EXPECT_FLOAT_EQ(c.splits.w, 40.0f);
}

TEST(ShadowCascades, StraightDownLightAndDegenerateInputs) {
    SunShadowSettings settings;
    const glm::mat4 view = cameraAt({0, 2, 5}, {0, 0, -5});
    EXPECT_TRUE(computeSunCascades(view, kFov, kAspect, kNear, kFar, {0, -1, 0}, settings).valid);
    EXPECT_FALSE(computeSunCascades(view, kFov, kAspect, kNear, kFar, {0, 0, 0}, settings).valid);
    EXPECT_FALSE(computeSunCascades(view, kFov, 0.0f, kNear, kFar, kSun, settings).valid);
}

// ── In the scene context ────────────────────────────────────────

class SunShadowScene : public testing::Test {
protected:
    void SetUp() override {
        auto camera = registry.create();
        registry.emplace<ECS::TransformComponent>(camera).position = glm::vec3(0.0f, 2.0f, 8.0f);
        auto& cam = registry.emplace<ECS::CameraComponent>(camera);
        cam.isMainCamera = true;
        cam.fov = 50.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 200.0f;

        // A point light first, so the sun is not lights[0].
        auto point = registry.create();
        registry.emplace<ECS::TransformComponent>(point);
        registry.emplace<ECS::LightComponent>(point).type = ECS::LightComponent::Point;

        sun = registry.create();
        auto& t = registry.emplace<ECS::TransformComponent>(sun);
        t.rotation = glm::vec3(-0.8f, 0.6f, 0.0f);
        auto& light = registry.emplace<ECS::LightComponent>(sun);
        light.type = ECS::LightComponent::Directional;

        transformSystem.update(registry, 0.016f);
        cameraSystem.update(registry, 0.016f);
        scene.screenWidth = 1280.0f;
        scene.screenHeight = 720.0f;
    }

    entt::registry registry;
    entt::entity sun{};
    ECS::TransformSystem transformSystem;
    ECS::CameraSystem cameraSystem;
    SceneContext scene;
    DotPathResolver resolver;
};

TEST_F(SunShadowScene, NoCascadesUntilALightCastsShadows) {
    scene.updateFromRegistry(registry);
    EXPECT_FALSE(scene.sunShadow.cascades.valid);
    EXPECT_EQ(scene.sunShadow.lightIndex, -1);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.enabled", scene).as<uint32_t>(), 0u);
}

TEST_F(SunShadowScene, CastingSunIsFittedAndPublished) {
    registry.get<ECS::LightComponent>(sun).castShadows = true;
    scene.sunShadow.settings.cascadeCount = 3;
    scene.updateFromRegistry(registry);

    const auto& sunShadow = scene.sunShadow;
    ASSERT_TRUE(sunShadow.cascades.valid);
    ASSERT_GE(sunShadow.lightIndex, 0);
    EXPECT_EQ(scene.lights[sunShadow.lightIndex].positionType.w, 0.0f);  // directional
    EXPECT_EQ(glm::vec3(sunShadow.direction),
              glm::vec3(scene.lights[sunShadow.lightIndex].directionRange));

    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.enabled", scene).as<uint32_t>(), 1u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.cascadeCount", scene).as<uint32_t>(), 3u);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.lightIndex", scene).as<int32_t>(), sunShadow.lightIndex);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.splits", scene).as<glm::vec4>(), sunShadow.cascades.splits);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.texelWorldSize", scene).as<glm::vec4>(),
              sunShadow.cascades.texelWorldSize);
    EXPECT_EQ(resolver.resolveScene("scene.shadows.sun.cascades[2].viewProj", scene).as<glm::mat4>(),
              sunShadow.cascades.viewProj[2]);
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.sun.cascades[4].viewProj", scene).isValid());
    EXPECT_FALSE(resolver.resolveScene("scene.shadows.sun.nothing", scene).isValid());
    EXPECT_EQ(resolver.validatePath("scene.shadows.sun.cascades[0].viewProj"), "");
}
