//
// FacadeTypesTest.cpp - Tests for facade type definitions and handle conversions
//
// Tier 1: Pure logic — no GPU, no ECS
//

#include <gtest/gtest.h>
#include "Facade/FacadeTypes.h"
#include <entt/entt.hpp>   // For verifying handle compatibility

using namespace Shoonyakasha::Facade;

// ═══════════════════════════════════════════════════════════════
// EntityHandle ↔ entt::entity Compatibility
// ═══════════════════════════════════════════════════════════════

TEST(FacadeTypes, EntityHandle_SizeMatchesEnttEntity) {
    EXPECT_EQ(sizeof(EntityHandle), sizeof(entt::entity));
}

TEST(FacadeTypes, NullEntity_EqualsEnttNull) {
    auto enttNull = static_cast<EntityHandle>(entt::null);
    EXPECT_EQ(NullEntity, enttNull);
}

TEST(FacadeTypes, EntityHandle_RoundTrip) {
    entt::registry registry;
    auto entity = registry.create();

    // entt::entity → EntityHandle → entt::entity
    EntityHandle handle = static_cast<EntityHandle>(entity);
    entt::entity back   = static_cast<entt::entity>(handle);
    EXPECT_EQ(entity, back);
}

TEST(FacadeTypes, NullEntity_IsMaxUint32) {
    EXPECT_EQ(NullEntity, UINT32_MAX);
}

// ═══════════════════════════════════════════════════════════════
// Enum Value Stability (must match internal enums)
// ═══════════════════════════════════════════════════════════════

TEST(FacadeTypes, CameraType_Values) {
    EXPECT_EQ(static_cast<uint8_t>(CameraType::Perspective), 0);
    EXPECT_EQ(static_cast<uint8_t>(CameraType::Orthographic), 1);
}

TEST(FacadeTypes, LightType_Values) {
    EXPECT_EQ(static_cast<uint8_t>(LightType::Directional), 0);
    EXPECT_EQ(static_cast<uint8_t>(LightType::Point), 1);
    EXPECT_EQ(static_cast<uint8_t>(LightType::Spot), 2);
}

TEST(FacadeTypes, RigidBodyType_Values) {
    EXPECT_EQ(static_cast<uint8_t>(RigidBodyType::Static), 0);
    EXPECT_EQ(static_cast<uint8_t>(RigidBodyType::Kinematic), 1);
    EXPECT_EQ(static_cast<uint8_t>(RigidBodyType::Dynamic), 2);
}

TEST(FacadeTypes, ColliderShape_Values) {
    EXPECT_EQ(static_cast<uint8_t>(ColliderShape::Box), 0);
    EXPECT_EQ(static_cast<uint8_t>(ColliderShape::Sphere), 1);
    EXPECT_EQ(static_cast<uint8_t>(ColliderShape::Capsule), 2);
    EXPECT_EQ(static_cast<uint8_t>(ColliderShape::Mesh), 3);
    EXPECT_EQ(static_cast<uint8_t>(ColliderShape::Plane), 4);
}

// ═══════════════════════════════════════════════════════════════
// EngineConfig Defaults
// ═══════════════════════════════════════════════════════════════

TEST(FacadeTypes, EngineConfig_Defaults) {
    EngineConfig config;
    EXPECT_EQ(config.width, 1600);
    EXPECT_EQ(config.height, 900);
    EXPECT_EQ(config.title, "Shoonyakasha Application");
    EXPECT_EQ(config.logLevel, 1);
    EXPECT_TRUE(config.hdrEnvironmentPath.empty());
    EXPECT_TRUE(config.pipelineJsonPath.empty());
    EXPECT_EQ(config.maxFramesInFlight, 2u);
    EXPECT_TRUE(config.renderGraphParameters.empty());
}

// ═══════════════════════════════════════════════════════════════
// GltfOptions Defaults
// ═══════════════════════════════════════════════════════════════

TEST(FacadeTypes, GltfOptions_Defaults) {
    GltfOptions opts;
    EXPECT_TRUE(opts.loadTextures);
    EXPECT_TRUE(opts.loadMaterials);
    EXPECT_TRUE(opts.createEntities);
    EXPECT_TRUE(opts.loadSkins);
    EXPECT_TRUE(opts.loadAnimations);
    EXPECT_TRUE(opts.flattenHierarchy);
    EXPECT_EQ(opts.maxTextureSize, 0);
    EXPECT_TRUE(opts.generateMipmaps);
    EXPECT_TRUE(opts.srgbAlbedo);
    EXPECT_TRUE(opts.namePrefix.empty());
}

// ═══════════════════════════════════════════════════════════════
// GltfResult Defaults
// ═══════════════════════════════════════════════════════════════

TEST(FacadeTypes, GltfResult_Defaults) {
    GltfResult result;
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.entities.empty());
    EXPECT_EQ(result.totalVertices, 0u);
    EXPECT_EQ(result.totalIndices, 0u);
    EXPECT_EQ(result.totalTextures, 0u);
    EXPECT_EQ(result.totalMaterials, 0u);
}
