//
// TransformTest.cpp - Tests for TransformComponent
//
// Tier 2: ECS integration — uses EnTT, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "TestHelpers.h"
#include <glm/gtc/constants.hpp>

using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// Local Matrix Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, Default_IdentityMatrix) {
    TransformComponent t;
    auto m = t.getLocalMatrix();
    TestHelpers::ExpectMat4Near(m, glm::mat4(1.0f));
}

TEST(TransformComponent, TranslationOnly) {
    TransformComponent t;
    t.position = glm::vec3(1.0f, 2.0f, 3.0f);
    auto m = t.getLocalMatrix();

    // Translation should be in the 4th column
    EXPECT_FLOAT_EQ(m[3][0], 1.0f);
    EXPECT_FLOAT_EQ(m[3][1], 2.0f);
    EXPECT_FLOAT_EQ(m[3][2], 3.0f);
}

TEST(TransformComponent, ScaleOnly) {
    TransformComponent t;
    t.scale = glm::vec3(2.0f, 3.0f, 4.0f);
    auto m = t.getLocalMatrix();

    // Diagonal elements should be the scale
    EXPECT_FLOAT_EQ(m[0][0], 2.0f);
    EXPECT_FLOAT_EQ(m[1][1], 3.0f);
    EXPECT_FLOAT_EQ(m[2][2], 4.0f);
}

TEST(TransformComponent, RotationY_90Degrees) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();  // 90 degrees yaw
    auto m = t.getLocalMatrix();

    // After 90-degree Y rotation (column-major):
    // Column 0 = (cos90, 0, -sin90, 0) → (0, 0, -1, 0)
    // Column 2 = (sin90, 0, cos90, 0)  → (1, 0,  0, 0)
    EXPECT_NEAR(m[0][0], 0.0f, 1e-5f);   // cos(90) ≈ 0
    EXPECT_NEAR(m[0][2], -1.0f, 1e-5f);  // -sin(90) in column 0, row 2
}

TEST(TransformComponent, TRS_Composition) {
    TransformComponent t;
    t.position = glm::vec3(10.0f, 0.0f, 0.0f);
    t.scale = glm::vec3(2.0f);

    auto m = t.getLocalMatrix();

    // The composition should be T * R * S
    // A point at origin scaled by 2 then translated by 10 along X:
    glm::vec4 origin(0, 0, 0, 1);
    glm::vec4 result = m * origin;
    EXPECT_NEAR(result.x, 10.0f, 1e-5f);
    EXPECT_NEAR(result.y, 0.0f, 1e-5f);
    EXPECT_NEAR(result.z, 0.0f, 1e-5f);

    // A point at (1,0,0) should be scaled to (2,0,0) then translated to (12,0,0)
    glm::vec4 unitX(1, 0, 0, 1);
    glm::vec4 result2 = m * unitX;
    EXPECT_NEAR(result2.x, 12.0f, 1e-5f);
}

// ═══════════════════════════════════════════════════════════════
// Direction Vector Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, DefaultForward_NegativeZ) {
    TransformComponent t;
    auto fwd = t.getForward();
    TestHelpers::ExpectVec3Near(fwd, glm::vec3(0.0f, 0.0f, -1.0f));
}

TEST(TransformComponent, DefaultRight_PositiveX) {
    TransformComponent t;
    auto right = t.getRight();
    TestHelpers::ExpectVec3Near(right, glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(TransformComponent, DefaultUp_PositiveY) {
    TransformComponent t;
    auto up = t.getUp();
    TestHelpers::ExpectVec3Near(up, glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(TransformComponent, Forward_After90YawRotation) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();  // 90 degrees yaw
    auto fwd = t.getForward();

    // After 90 Y rotation, forward (-Z) should point toward -X
    TestHelpers::ExpectVec3Near(fwd, glm::vec3(-1.0f, 0.0f, 0.0f));
}

TEST(TransformComponent, Right_After90YawRotation) {
    TransformComponent t;
    t.rotation.y = glm::half_pi<float>();
    auto right = t.getRight();

    // After 90 Y rotation, right (+X) should point toward -Z
    TestHelpers::ExpectVec3Near(right, glm::vec3(0.0f, 0.0f, -1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Dirty Flag Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, DirtyFlag_StartsDirty) {
    TransformComponent t;
    EXPECT_TRUE(t.isDirty);
}

TEST(TransformComponent, DirtyFlag_PositionConstruction) {
    TransformComponent t(glm::vec3(1, 2, 3));
    EXPECT_TRUE(t.isDirty);
    EXPECT_FLOAT_EQ(t.position.x, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
// Constructor Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformComponent, PositionConstructor) {
    TransformComponent t(glm::vec3(5, 10, 15));
    EXPECT_FLOAT_EQ(t.position.x, 5.0f);
    EXPECT_FLOAT_EQ(t.position.y, 10.0f);
    EXPECT_FLOAT_EQ(t.position.z, 15.0f);
    // Scale should still default to 1
    TestHelpers::ExpectVec3Near(t.scale, glm::vec3(1.0f));
}

TEST(TransformComponent, FullConstructor) {
    glm::vec3 pos(1, 2, 3);
    glm::vec3 rot(0.1f, 0.2f, 0.3f);
    glm::vec3 scl(2, 3, 4);
    TransformComponent t(pos, rot, scl);

    TestHelpers::ExpectVec3Near(t.position, pos);
    TestHelpers::ExpectVec3Near(t.rotation, rot);
    TestHelpers::ExpectVec3Near(t.scale, scl);
}

// ═══════════════════════════════════════════════════════════════
// Hierarchy destruction and cycle safety
// ═══════════════════════════════════════════════════════════════

// destroyEntity used to range-for over hierarchy->children while the recursive
// call erased from that same vector, and held a component pointer across
// registry.destroy calls that could relocate it. Both needed two or more
// children to show, and both corrupted silently rather than crashing.
TEST(EntityHelperHierarchy, DestroyParent_WithManyChildren_DestroysAll) {
    entt::registry registry;

    auto parent = registry.create();
    registry.emplace<ECS::HierarchyComponent>(parent);

    std::vector<entt::entity> children;
    for (int i = 0; i < 8; ++i) {
        auto child = registry.create();
        auto& ch = registry.emplace<ECS::HierarchyComponent>(child);
        ch.parent = parent;
        registry.get<ECS::HierarchyComponent>(parent).addChild(child);
        children.push_back(child);
    }

    ECS::EntityHelper::destroyEntity(registry, parent);

    EXPECT_FALSE(registry.valid(parent));
    for (auto c : children) EXPECT_FALSE(registry.valid(c)) << "child outlived its parent";
}

TEST(EntityHelperHierarchy, DestroyParent_DeepChain_DestroysAll) {
    entt::registry registry;

    std::vector<entt::entity> chain;
    entt::entity prev = entt::null;
    for (int i = 0; i < 32; ++i) {
        auto e = registry.create();
        auto& h = registry.emplace<ECS::HierarchyComponent>(e);
        h.parent = prev;
        if (prev != entt::null) registry.get<ECS::HierarchyComponent>(prev).addChild(e);
        chain.push_back(e);
        prev = e;
    }

    ECS::EntityHelper::destroyEntity(registry, chain.front());
    for (auto e : chain) EXPECT_FALSE(registry.valid(e));
}

// A parent and child expiring on the same frame means the second destroy call
// sees an already-dead handle. try_get on a dead entity is an EnTT precondition
// violation.
TEST(EntityHelperHierarchy, DestroyEntity_AlreadyDestroyed_IsNoOp) {
    entt::registry registry;
    auto e = registry.create();
    ECS::EntityHelper::destroyEntity(registry, e);
    EXPECT_NO_FATAL_FAILURE(ECS::EntityHelper::destroyEntity(registry, e));
    EXPECT_NO_FATAL_FAILURE(ECS::EntityHelper::destroyEntity(registry, entt::null));
}

TEST(EntityHelperHierarchy, IsAncestorOf_DetectsSelfAndAncestors) {
    entt::registry registry;

    auto a = registry.create();
    auto b = registry.create();
    auto c = registry.create();
    registry.emplace<ECS::HierarchyComponent>(a);
    registry.emplace<ECS::HierarchyComponent>(b).parent = a;
    registry.emplace<ECS::HierarchyComponent>(c).parent = b;

    EXPECT_TRUE(ECS::EntityHelper::isAncestorOf(registry, a, c));
    EXPECT_TRUE(ECS::EntityHelper::isAncestorOf(registry, b, c));
    EXPECT_TRUE(ECS::EntityHelper::isAncestorOf(registry, c, c)) << "an entity is its own ancestor here";
    EXPECT_FALSE(ECS::EntityHelper::isAncestorOf(registry, c, a));
}

// A cycle can arrive from Scene::deserialize, which restores parent links
// straight from JSON without validating them. isAncestorOf must terminate.
TEST(EntityHelperHierarchy, IsAncestorOf_TerminatesOnCycle) {
    entt::registry registry;

    auto a = registry.create();
    auto b = registry.create();
    registry.emplace<ECS::HierarchyComponent>(a).parent = b;
    registry.emplace<ECS::HierarchyComponent>(b).parent = a;

    EXPECT_NO_FATAL_FAILURE({
        volatile bool r = ECS::EntityHelper::isAncestorOf(registry, a, b);
        (void)r;
    });
}

// TransformSystem walks the hierarchy recursively. With a cycle in the data it
// used to recurse until the stack ran out.
TEST(EntityHelperHierarchy, TransformSystem_SurvivesCyclicHierarchy) {
    entt::registry registry;

    auto a = registry.create();
    auto b = registry.create();
    registry.emplace<ECS::TransformComponent>(a);
    registry.emplace<ECS::TransformComponent>(b);
    registry.emplace<ECS::HierarchyComponent>(a).parent = b;
    registry.emplace<ECS::HierarchyComponent>(b).parent = a;

    ECS::TransformSystem sys;
    EXPECT_NO_FATAL_FAILURE(sys.update(registry, 0.016f));
}
