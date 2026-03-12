//
// DotPathResolverECSTest.cpp - Integration tests for the full dot-path resolve pipeline
//
// Tier 3: FrameGraph + ECS integration — uses EnTT registry, DotPathResolver, BufferLayoutResolver
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "FrameGraph/BufferLayoutCompiler.h"
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;
using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// Test Fixture: Populated registry with camera, lights, entity
// ═══════════════════════════════════════════════════════════════

class DotPathResolverECS : public testing::Test {
protected:
    void SetUp() override {
        // Create camera entity
        cameraEntity = registry.create();
        auto& camTransform = registry.emplace<TransformComponent>(cameraEntity);
        camTransform.position = glm::vec3(0.0f, 5.0f, 10.0f);
        auto& cam = registry.emplace<CameraComponent>(cameraEntity);
        cam.isMainCamera = true;
        cam.fov = 60.0f;
        cam.nearPlane = 0.1f;
        cam.farPlane = 500.0f;

        // Create directional light
        lightEntity = registry.create();
        auto& lightTransform = registry.emplace<TransformComponent>(lightEntity);
        lightTransform.position = glm::vec3(10.0f, 20.0f, 10.0f);
        auto& light = registry.emplace<LightComponent>(lightEntity);
        light.type = LightComponent::Directional;
        light.color = glm::vec3(1.0f, 0.9f, 0.8f);
        light.intensity = 2.0f;

        // Create renderable entity with transform + material
        renderEntity = registry.create();
        auto& renderTransform = registry.emplace<TransformComponent>(renderEntity);
        renderTransform.position = glm::vec3(3.0f, 0.0f, -5.0f);
        renderTransform.scale = glm::vec3(2.0f);

        auto& mat = registry.emplace<MaterialComponentV5>(renderEntity);
        mat.setParam("baseColorFactor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
        mat.setParam("metallicFactor", 0.5f);
        mat.setParam("roughnessFactor", 0.8f);
        mat.alphaCutoff = 0.25f;
        mat.alphaMode = AlphaMode::Mask;

        // Run systems to compute matrices
        transformSystem.update(registry, 0.016f);
        cameraSystem.update(registry, 0.016f);

        // Populate scene context
        scene.screenWidth = 1920.0f;
        scene.screenHeight = 1080.0f;
        scene.timeElapsed = 5.0f;
        scene.timeDelta = 0.016f;
        scene.updateFromRegistry(registry);
    }

    entt::registry registry;
    TransformSystem transformSystem;
    CameraSystem cameraSystem;
    DotPathResolver resolver;
    SceneContext scene;

    entt::entity cameraEntity;
    entt::entity lightEntity;
    entt::entity renderEntity;
};

// ═══════════════════════════════════════════════════════════════
// SceneContext::updateFromRegistry Tests
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, SceneContext_CameraDataExtracted) {
    // Camera position should match the camera entity's transform
    TestHelpers::ExpectVec3Near(scene.cameraPosition, glm::vec3(0.0f, 5.0f, 10.0f));
    EXPECT_FLOAT_EQ(scene.cameraFov, 60.0f);
    EXPECT_FLOAT_EQ(scene.cameraNearPlane, 0.1f);
    EXPECT_FLOAT_EQ(scene.cameraFarPlane, 500.0f);
}

TEST_F(DotPathResolverECS, SceneContext_LightsCollected) {
    EXPECT_EQ(scene.lightCount, 1u);

    // Light position (xyz) + type (w=0 for directional)
    EXPECT_NEAR(scene.lights[0].positionType.x, 10.0f, 1e-4f);
    EXPECT_NEAR(scene.lights[0].positionType.y, 20.0f, 1e-4f);
    EXPECT_NEAR(scene.lights[0].positionType.w, 0.0f, 1e-4f);  // Directional = 0

    // Color (xyz) + intensity (w)
    EXPECT_NEAR(scene.lights[0].colorIntensity.x, 1.0f, 1e-4f);
    EXPECT_NEAR(scene.lights[0].colorIntensity.w, 2.0f, 1e-4f);
}

TEST_F(DotPathResolverECS, SceneContext_LightCountCapped) {
    // Add MAX_SCENE_LIGHTS + 1 lights
    for (uint32_t i = 0; i < SceneContext::MAX_SCENE_LIGHTS + 5; ++i) {
        auto e = registry.create();
        registry.emplace<TransformComponent>(e);
        auto& l = registry.emplace<LightComponent>(e);
        l.type = LightComponent::Point;
    }

    scene.updateFromRegistry(registry);

    // Should be capped at MAX_SCENE_LIGHTS
    EXPECT_LE(scene.lightCount, SceneContext::MAX_SCENE_LIGHTS);
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Camera Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_CameraView) {
    auto v = resolver.resolveScene("scene.camera.view", scene);
    ASSERT_TRUE(v.isMat4());
    TestHelpers::ExpectMat4Near(v.as<glm::mat4>(), scene.cameraView);
}

TEST_F(DotPathResolverECS, ResolveScene_CameraProjection) {
    auto v = resolver.resolveScene("scene.camera.projection", scene);
    ASSERT_TRUE(v.isMat4());
    TestHelpers::ExpectMat4Near(v.as<glm::mat4>(), scene.cameraProjection);
}

TEST_F(DotPathResolverECS, ResolveScene_CameraPosition) {
    auto v = resolver.resolveScene("scene.camera.position", scene);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(0.0f, 5.0f, 10.0f));
}

TEST_F(DotPathResolverECS, ResolveScene_CameraFov) {
    auto v = resolver.resolveScene("scene.camera.fov", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 60.0f);
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Time Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_TimeElapsed) {
    auto v = resolver.resolveScene("scene.time.elapsed", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 5.0f);
}

TEST_F(DotPathResolverECS, ResolveScene_TimeDelta) {
    auto v = resolver.resolveScene("scene.time.delta", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 0.016f);
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Screen Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_ScreenResolution) {
    auto v = resolver.resolveScene("scene.screen.resolution", scene);
    ASSERT_TRUE(v.isVec2());
    TestHelpers::ExpectVec2Near(v.as<glm::vec2>(), glm::vec2(1920.0f, 1080.0f));
}

TEST_F(DotPathResolverECS, ResolveScene_ScreenWidth) {
    auto v = resolver.resolveScene("scene.screen.width", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 1920.0f);
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Light Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_LightCount) {
    auto v = resolver.resolveScene("scene.lights.count", scene);
    ASSERT_TRUE(v.isUInt());
    EXPECT_EQ(v.as<uint32_t>(), 1u);
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Custom Values
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_CustomFloat) {
    scene.setCustom("gravity", 9.81f);
    auto v = resolver.resolveScene("scene.custom.gravity", scene);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 9.81f);
}

TEST_F(DotPathResolverECS, ResolveScene_CustomVec3) {
    scene.setCustom("windDirection", glm::vec3(1.0f, 0.0f, 0.5f));
    auto v = resolver.resolveScene("scene.custom.windDirection", scene);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(1.0f, 0.0f, 0.5f));
}

TEST_F(DotPathResolverECS, ResolveScene_CustomMissing_Invalid) {
    auto v = resolver.resolveScene("scene.custom.nonexistent", scene);
    EXPECT_FALSE(v.isValid());
}

// ═══════════════════════════════════════════════════════════════
// resolveScene — Invalid Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveScene_InvalidPath_ReturnsInvalid) {
    auto v = resolver.resolveScene("scene.bogus.field", scene);
    EXPECT_FALSE(v.isValid());
}

TEST_F(DotPathResolverECS, ResolveScene_EntityPath_ReturnsInvalid) {
    // resolveScene should not resolve entity paths
    auto v = resolver.resolveScene("entity.transform.worldMatrix", scene);
    EXPECT_FALSE(v.isValid());
}

// ═══════════════════════════════════════════════════════════════
// resolveEntity — Transform Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveEntity_WorldMatrix) {
    auto v = resolver.resolveEntity("entity.transform.worldMatrix", renderEntity, registry);
    ASSERT_TRUE(v.isMat4());

    auto& t = registry.get<TransformComponent>(renderEntity);
    TestHelpers::ExpectMat4Near(v.as<glm::mat4>(), t.worldMatrix);
}

TEST_F(DotPathResolverECS, ResolveEntity_Position) {
    auto v = resolver.resolveEntity("entity.transform.position", renderEntity, registry);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(3.0f, 0.0f, -5.0f));
}

TEST_F(DotPathResolverECS, ResolveEntity_Scale) {
    auto v = resolver.resolveEntity("entity.transform.scale", renderEntity, registry);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(2.0f));
}

// ═══════════════════════════════════════════════════════════════
// resolveEntity — Material Params
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveEntity_MaterialParam_Vec4) {
    auto v = resolver.resolveEntity("entity.material.params.baseColorFactor", renderEntity, registry);
    ASSERT_TRUE(v.isVec4());
    TestHelpers::ExpectVec4Near(v.as<glm::vec4>(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
}

TEST_F(DotPathResolverECS, ResolveEntity_MaterialParam_Float) {
    auto v = resolver.resolveEntity("entity.material.params.metallicFactor", renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 0.5f);
}

TEST_F(DotPathResolverECS, ResolveEntity_MaterialParam_Missing_Invalid) {
    auto v = resolver.resolveEntity("entity.material.params.nonexistent", renderEntity, registry);
    EXPECT_FALSE(v.isValid());
}

TEST_F(DotPathResolverECS, ResolveEntity_MaterialAlphaCutoff) {
    auto v = resolver.resolveEntity("entity.material.alphaCutoff", renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 0.25f);
}

TEST_F(DotPathResolverECS, ResolveEntity_MaterialAlphaMode) {
    auto v = resolver.resolveEntity("entity.material.alphaMode", renderEntity, registry);
    ASSERT_TRUE(v.isUInt());
    EXPECT_EQ(v.as<uint32_t>(), static_cast<uint32_t>(AlphaMode::Mask));
}

// ═══════════════════════════════════════════════════════════════
// resolveEntity — Skeleton Paths
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveEntity_HasSkeleton_False) {
    auto v = resolver.resolveEntity("entity.skeleton.hasSkeleton", renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 0.0f);
}

TEST_F(DotPathResolverECS, ResolveEntity_HasSkeleton_True) {
    auto skel = std::make_shared<Skeleton>();
    Joint j;
    j.name = "Root";
    skel->joints.push_back(j);
    skel->buildNameLookup();

    auto& skelComp = registry.emplace<SkeletonComponent>(renderEntity);
    skelComp.skeleton = skel;
    skelComp.allocate();

    auto v = resolver.resolveEntity("entity.skeleton.hasSkeleton", renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 1.0f);
}

TEST_F(DotPathResolverECS, ResolveEntity_SkeletonJointCount) {
    auto skel = std::make_shared<Skeleton>();
    for (int i = 0; i < 5; ++i) {
        Joint j;
        j.name = "Joint_" + std::to_string(i);
        skel->joints.push_back(j);
    }
    skel->buildNameLookup();

    auto& skelComp = registry.emplace<SkeletonComponent>(renderEntity);
    skelComp.skeleton = skel;

    auto v = resolver.resolveEntity("entity.skeleton.jointCount", renderEntity, registry);
    ASSERT_TRUE(v.isUInt());
    EXPECT_EQ(v.as<uint32_t>(), 5u);
}

// ═══════════════════════════════════════════════════════════════
// resolveEntity — Invalid Entity
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, ResolveEntity_NullEntity_Invalid) {
    auto v = resolver.resolveEntity("entity.transform.worldMatrix", entt::null, registry);
    EXPECT_FALSE(v.isValid());
}

TEST_F(DotPathResolverECS, ResolveEntity_ScenePath_Invalid) {
    // resolveEntity should not resolve scene paths
    auto v = resolver.resolveEntity("scene.camera.view", renderEntity, registry);
    EXPECT_FALSE(v.isValid());
}

// ═══════════════════════════════════════════════════════════════
// resolve() — Full Dispatch
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, Resolve_DispatchesScene) {
    auto v = resolver.resolve("scene.time.elapsed", scene, renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 5.0f);
}

TEST_F(DotPathResolverECS, Resolve_DispatchesEntity) {
    auto v = resolver.resolve("entity.transform.position", scene, renderEntity, registry);
    ASSERT_TRUE(v.isVec3());
    TestHelpers::ExpectVec3Near(v.as<glm::vec3>(), glm::vec3(3.0f, 0.0f, -5.0f));
}

TEST_F(DotPathResolverECS, Resolve_DispatchesConst) {
    auto v = resolver.resolve("const.42", scene, renderEntity, registry);
    ASSERT_TRUE(v.isFloat());
    EXPECT_FLOAT_EQ(v.as<float>(), 42.0f);
}

TEST_F(DotPathResolverECS, Resolve_ResourcePath_ReturnsInvalid) {
    // Resource paths are handled by the frame graph, not the resolver
    auto v = resolver.resolve("gPosition", scene, renderEntity, registry);
    EXPECT_FALSE(v.isValid());
}

// ═══════════════════════════════════════════════════════════════
// BufferLayoutResolver — fillSceneBuffer
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, FillSceneBuffer_WritesCorrectValues) {
    BufferLayoutCompiler compiler;
    nlohmann::json layoutJson = {
        {"packing", "scalar"},
        {"fields", {
            {{"name", "elapsed"}, {"type", "float"}, {"source", "scene.time.elapsed"}},
            {{"name", "resolution"}, {"type", "vec2"}, {"source", "scene.screen.resolution"}}
        }}
    };

    auto layout = compiler.compile("test", layoutJson);
    std::vector<uint8_t> buffer(layout.totalSize, 0);

    BufferLayoutResolver layoutResolver(resolver);
    layoutResolver.fillSceneBuffer(buffer.data(), layout, scene);

    // Read back elapsed time at offset 0
    float elapsed = 0.0f;
    std::memcpy(&elapsed, buffer.data() + layout.fields[0].offset, sizeof(float));
    EXPECT_FLOAT_EQ(elapsed, 5.0f);

    // Read back resolution at its offset
    glm::vec2 resolution(0.0f);
    std::memcpy(&resolution, buffer.data() + layout.fields[1].offset, sizeof(glm::vec2));
    TestHelpers::ExpectVec2Near(resolution, glm::vec2(1920.0f, 1080.0f));
}

// ═══════════════════════════════════════════════════════════════
// BufferLayoutResolver — fillEntityBuffer
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, FillEntityBuffer_WritesTransformAndMaterial) {
    BufferLayoutCompiler compiler;
    nlohmann::json layoutJson = {
        {"packing", "scalar"},
        {"fields", {
            {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}},
            {{"name", "color"}, {"type", "vec4"}, {"source", "entity.material.params.baseColorFactor"}},
            {{"name", "metallic"}, {"type", "float"}, {"source", "entity.material.params.metallicFactor"}}
        }}
    };

    auto layout = compiler.compile("push", layoutJson);
    std::vector<uint8_t> buffer(layout.totalSize, 0);

    BufferLayoutResolver layoutResolver(resolver);
    layoutResolver.fillEntityBuffer(buffer.data(), layout, scene, renderEntity, registry);

    // Read back world matrix
    glm::mat4 model(1.0f);
    std::memcpy(&model, buffer.data() + layout.fields[0].offset, sizeof(glm::mat4));
    auto& t = registry.get<TransformComponent>(renderEntity);
    TestHelpers::ExpectMat4Near(model, t.worldMatrix);

    // Read back base color
    glm::vec4 color(0.0f);
    std::memcpy(&color, buffer.data() + layout.fields[1].offset, sizeof(glm::vec4));
    TestHelpers::ExpectVec4Near(color, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // Read back metallic factor
    float metallic = 0.0f;
    std::memcpy(&metallic, buffer.data() + layout.fields[2].offset, sizeof(float));
    EXPECT_FLOAT_EQ(metallic, 0.5f);
}

// ═══════════════════════════════════════════════════════════════
// BufferLayoutResolver — Mixed sources in fillBuffer
// ═══════════════════════════════════════════════════════════════

TEST_F(DotPathResolverECS, FillBuffer_MixedSources) {
    BufferLayoutCompiler compiler;
    nlohmann::json layoutJson = {
        {"packing", "scalar"},
        {"fields", {
            {{"name", "time"}, {"type", "float"}, {"source", "scene.time.elapsed"}},
            {{"name", "model"}, {"type", "mat4"}, {"source", "entity.transform.worldMatrix"}},
            {{"name", "zero"}, {"type", "float"}, {"source", "const.0"}}
        }}
    };

    auto layout = compiler.compile("mixed", layoutJson);
    EXPECT_TRUE(layout.hasSceneSources);
    EXPECT_TRUE(layout.hasEntitySources);
    EXPECT_TRUE(layout.hasConstSources);

    std::vector<uint8_t> buffer(layout.totalSize, 0xFF);  // Fill with 0xFF to detect writes

    BufferLayoutResolver layoutResolver(resolver);
    layoutResolver.fillBuffer(buffer.data(), layout, scene, renderEntity, registry);

    // Verify time was written
    float time = 0.0f;
    std::memcpy(&time, buffer.data() + layout.fields[0].offset, sizeof(float));
    EXPECT_FLOAT_EQ(time, 5.0f);

    // Verify const.0 was written
    float zero = 99.0f;
    std::memcpy(&zero, buffer.data() + layout.fields[2].offset, sizeof(float));
    EXPECT_FLOAT_EQ(zero, 0.0f);
}
