//
// ComponentRegistryTest.cpp - Tests for string-based component CRUD
//
// Tier 2: ECS integration — uses EnTT registry, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Core.h"

using namespace Shoonyakasha::ECS;

class ComponentRegistryFixture : public testing::Test {
protected:
    void SetUp() override {
        registerAllComponents(compReg);
    }

    entt::registry registry;
    ComponentRegistry compReg;
};

// ═══════════════════════════════════════════════════════════════
// Registration Tests
// ═══════════════════════════════════════════════════════════════

TEST_F(ComponentRegistryFixture, RegisterAndCreate_HasReturnsTrue) {
    auto entity = registry.create();
    EXPECT_TRUE(compReg.createComponent("Transform", registry, entity));
    EXPECT_TRUE(compReg.hasComponent("Transform", registry, entity));
}

TEST_F(ComponentRegistryFixture, HasComponent_BeforeCreate_ReturnsFalse) {
    auto entity = registry.create();
    EXPECT_FALSE(compReg.hasComponent("Transform", registry, entity));
}

TEST_F(ComponentRegistryFixture, RemoveComponent_HasReturnsFalse) {
    auto entity = registry.create();
    compReg.createComponent("Transform", registry, entity);
    EXPECT_TRUE(compReg.hasComponent("Transform", registry, entity));

    compReg.removeComponent("Transform", registry, entity);
    EXPECT_FALSE(compReg.hasComponent("Transform", registry, entity));
}

TEST_F(ComponentRegistryFixture, CreateComponent_UnknownName_ReturnsFalse) {
    auto entity = registry.create();
    EXPECT_FALSE(compReg.createComponent("Nonexistent", registry, entity));
}

TEST_F(ComponentRegistryFixture, HasComponent_UnknownName_ReturnsFalse) {
    auto entity = registry.create();
    EXPECT_FALSE(compReg.hasComponent("Nonexistent", registry, entity));
}

TEST_F(ComponentRegistryFixture, RemoveComponent_UnknownName_ReturnsFalse) {
    auto entity = registry.create();
    EXPECT_FALSE(compReg.removeComponent("Nonexistent", registry, entity));
}

// ═══════════════════════════════════════════════════════════════
// GetAllComponentNames Tests
// ═══════════════════════════════════════════════════════════════

TEST_F(ComponentRegistryFixture, GetAllComponentNames_ContainsRegistered) {
    auto names = compReg.getAllComponentNames();

    // registerAllComponents registers: Tag, Name, Hierarchy, Transform, Camera, Light, RigidBody, Collider, Active
    EXPECT_GE(names.size(), 9u);

    auto hasName = [&](const std::string& n) {
        return std::find(names.begin(), names.end(), n) != names.end();
    };

    EXPECT_TRUE(hasName("Tag"));
    EXPECT_TRUE(hasName("Name"));
    EXPECT_TRUE(hasName("Transform"));
    EXPECT_TRUE(hasName("Camera"));
    EXPECT_TRUE(hasName("Light"));
    EXPECT_TRUE(hasName("Active"));
}

// ═══════════════════════════════════════════════════════════════
// GetComponentName by type_index
// ═══════════════════════════════════════════════════════════════

TEST_F(ComponentRegistryFixture, GetComponentName_KnownType) {
    std::type_index idx(typeid(TransformComponent));
    EXPECT_EQ(compReg.getComponentName(idx), "Transform");
}

TEST_F(ComponentRegistryFixture, GetComponentName_UnknownType_Empty) {
    std::type_index idx(typeid(int));  // Not a registered component
    EXPECT_TRUE(compReg.getComponentName(idx).empty());
}

// ═══════════════════════════════════════════════════════════════
// Multiple Components on Same Entity
// ═══════════════════════════════════════════════════════════════

TEST_F(ComponentRegistryFixture, MultipleComponents_Independent) {
    auto entity = registry.create();
    compReg.createComponent("Transform", registry, entity);
    compReg.createComponent("Camera", registry, entity);
    compReg.createComponent("Name", registry, entity);

    EXPECT_TRUE(compReg.hasComponent("Transform", registry, entity));
    EXPECT_TRUE(compReg.hasComponent("Camera", registry, entity));
    EXPECT_TRUE(compReg.hasComponent("Name", registry, entity));

    // Remove one — others remain
    compReg.removeComponent("Camera", registry, entity);
    EXPECT_TRUE(compReg.hasComponent("Transform", registry, entity));
    EXPECT_FALSE(compReg.hasComponent("Camera", registry, entity));
    EXPECT_TRUE(compReg.hasComponent("Name", registry, entity));
}
