//
// EntityBuilderTest.cpp - Tests for fluent entity construction
//
// Tier 2: ECS integration — uses EnTT registry, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Core.h"
#include "TestHelpers.h"

using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// Build Defaults Tests
// ═══════════════════════════════════════════════════════════════

TEST(EntityBuilder, Build_AddsTransformAndActive) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).build();

    EXPECT_TRUE(reg.valid(entity));
    EXPECT_TRUE(reg.all_of<TransformComponent>(entity));
    EXPECT_TRUE(reg.all_of<ActiveComponent>(entity));
}

TEST(EntityBuilder, Build_DoesNotDuplicateTransform) {
    entt::registry reg;
    auto entity = EntityBuilder(reg)
        .withTransform(glm::vec3(1, 2, 3))
        .build();

    // Should still have only one TransformComponent
    EXPECT_TRUE(reg.all_of<TransformComponent>(entity));
    auto& t = reg.get<TransformComponent>(entity);
    EXPECT_FLOAT_EQ(t.position.x, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
// Convenience Builder Tests
// ═══════════════════════════════════════════════════════════════

TEST(EntityBuilder, WithName) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).withName("Hero").build();

    ASSERT_TRUE(reg.all_of<NameComponent>(entity));
    EXPECT_EQ(reg.get<NameComponent>(entity).name, "Hero");
}

TEST(EntityBuilder, WithTag) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).withTag("Player").build();

    ASSERT_TRUE(reg.all_of<TagComponent>(entity));
    EXPECT_EQ(reg.get<TagComponent>(entity).tag, "Player");
}

TEST(EntityBuilder, WithTransform_CustomValues) {
    entt::registry reg;
    glm::vec3 pos(10, 20, 30);
    glm::vec3 rot(0.1f, 0.2f, 0.3f);
    glm::vec3 scl(2, 3, 4);

    auto entity = EntityBuilder(reg).withTransform(pos, rot, scl).build();

    auto& t = reg.get<TransformComponent>(entity);
    TestHelpers::ExpectVec3Near(t.position, pos);
    TestHelpers::ExpectVec3Near(t.rotation, rot);
    TestHelpers::ExpectVec3Near(t.scale, scl);
}

TEST(EntityBuilder, WithCamera) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).withCamera(false).build();

    ASSERT_TRUE(reg.all_of<CameraComponent>(entity));
    EXPECT_FALSE(reg.get<CameraComponent>(entity).isMainCamera);
}

TEST(EntityBuilder, WithCamera_MainCamera) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).withCamera(true).build();

    ASSERT_TRUE(reg.all_of<CameraComponent>(entity));
    EXPECT_TRUE(reg.get<CameraComponent>(entity).isMainCamera);
}

TEST(EntityBuilder, WithLight) {
    entt::registry reg;
    auto entity = EntityBuilder(reg)
        .withLight(LightComponent::Point, glm::vec3(1, 0, 0), 5.0f)
        .build();

    ASSERT_TRUE(reg.all_of<LightComponent>(entity));
    auto& light = reg.get<LightComponent>(entity);
    EXPECT_EQ(light.type, LightComponent::Point);
    EXPECT_FLOAT_EQ(light.intensity, 5.0f);
    TestHelpers::ExpectVec3Near(light.color, glm::vec3(1, 0, 0));
}

TEST(EntityBuilder, WithLifetime) {
    entt::registry reg;
    auto entity = EntityBuilder(reg).withLifetime(5.0f).build();

    ASSERT_TRUE(reg.all_of<LifetimeComponent>(entity));
    EXPECT_FLOAT_EQ(reg.get<LifetimeComponent>(entity).timeToLive, 5.0f);
}

TEST(EntityBuilder, WithRigidBody) {
    entt::registry reg;
    auto entity = EntityBuilder(reg)
        .withRigidBody(RigidBodyComponent::Static, 0.0f)
        .build();

    ASSERT_TRUE(reg.all_of<RigidBodyComponent>(entity));
    auto& rb = reg.get<RigidBodyComponent>(entity);
    EXPECT_EQ(rb.type, RigidBodyComponent::Static);
    EXPECT_FLOAT_EQ(rb.mass, 0.0f);
}

TEST(EntityBuilder, WithCollider) {
    entt::registry reg;
    auto entity = EntityBuilder(reg)
        .withCollider(ColliderComponent::Sphere, glm::vec3(0.5f))
        .build();

    ASSERT_TRUE(reg.all_of<ColliderComponent>(entity));
    auto& col = reg.get<ColliderComponent>(entity);
    EXPECT_EQ(col.shape, ColliderComponent::Sphere);
    TestHelpers::ExpectVec3Near(col.size, glm::vec3(0.5f));
}

// ═══════════════════════════════════════════════════════════════
// Fluent Chaining Tests
// ═══════════════════════════════════════════════════════════════

TEST(EntityBuilder, FluentChaining_MultipleComponents) {
    entt::registry reg;
    auto entity = EntityBuilder(reg)
        .withName("TestEntity")
        .withTag("TestTag")
        .withTransform(glm::vec3(1, 0, 0))
        .withLifetime(10.0f)
        .build();

    EXPECT_TRUE(reg.all_of<NameComponent>(entity));
    EXPECT_TRUE(reg.all_of<TagComponent>(entity));
    EXPECT_TRUE(reg.all_of<TransformComponent>(entity));
    EXPECT_TRUE(reg.all_of<LifetimeComponent>(entity));
    EXPECT_TRUE(reg.all_of<ActiveComponent>(entity));
}

// ═══════════════════════════════════════════════════════════════
// Parent-Child Hierarchy Tests
// ═══════════════════════════════════════════════════════════════

TEST(EntityBuilder, WithParent_SetsHierarchy) {
    entt::registry reg;
    auto parent = EntityBuilder(reg).withName("Parent").build();
    auto child = EntityBuilder(reg).withName("Child").withParent(parent).build();

    ASSERT_TRUE(reg.all_of<HierarchyComponent>(child));
    auto& childHierarchy = reg.get<HierarchyComponent>(child);
    EXPECT_EQ(childHierarchy.parent, parent);

    // Parent should have child in its children list
    ASSERT_TRUE(reg.all_of<HierarchyComponent>(parent));
    auto& parentHierarchy = reg.get<HierarchyComponent>(parent);
    ASSERT_EQ(parentHierarchy.children.size(), 1u);
    EXPECT_EQ(parentHierarchy.children[0], child);
}

TEST(EntityBuilder, WithParent_MultipleChildren) {
    entt::registry reg;
    auto parent = EntityBuilder(reg).build();
    auto child1 = EntityBuilder(reg).withParent(parent).build();
    auto child2 = EntityBuilder(reg).withParent(parent).build();

    auto& parentHierarchy = reg.get<HierarchyComponent>(parent);
    EXPECT_EQ(parentHierarchy.children.size(), 2u);
}
