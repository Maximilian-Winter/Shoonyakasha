//
// SystemsTest.cpp - Tests for TransformSystem, CameraSystem, LifetimeSystem, SystemManager
//
// Tier 2: ECS integration — uses EnTT registry, no GPU
//

#include <gtest/gtest.h>
#include "ECS/Systems.h"
#include "TestHelpers.h"

using namespace Shoonyakasha::ECS;

// ═══════════════════════════════════════════════════════════════
// TransformSystem Tests
// ═══════════════════════════════════════════════════════════════

TEST(TransformSystem, RootEntity_WorldMatrixEqualsLocalMatrix) {
    entt::registry reg;
    TransformSystem system;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(5, 0, 0);

    system.update(reg, 0.016f);

    // Root entity (no HierarchyComponent): worldMatrix = localMatrix
    TestHelpers::ExpectMat4Near(t.worldMatrix, t.localMatrix);
}

TEST(TransformSystem, DirtyFlag_ClearsAfterUpdate) {
    entt::registry reg;
    TransformSystem system;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    EXPECT_TRUE(t.isDirty);

    system.update(reg, 0.016f);
    EXPECT_FALSE(t.isDirty);
}

TEST(TransformSystem, LocalMatrixUpdated_WhenDirty) {
    entt::registry reg;
    TransformSystem system;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(1, 2, 3);

    system.update(reg, 0.016f);

    // localMatrix should reflect position
    EXPECT_FLOAT_EQ(t.localMatrix[3][0], 1.0f);
    EXPECT_FLOAT_EQ(t.localMatrix[3][1], 2.0f);
    EXPECT_FLOAT_EQ(t.localMatrix[3][2], 3.0f);
}

TEST(TransformSystem, ParentChild_WorldMatrixComposition) {
    entt::registry reg;
    TransformSystem system;

    // Parent at (10, 0, 0)
    auto parent = reg.create();
    auto& pt = reg.emplace<TransformComponent>(parent);
    pt.position = glm::vec3(10, 0, 0);

    // Child at (5, 0, 0) local, parented
    auto child = reg.create();
    auto& ct = reg.emplace<TransformComponent>(child);
    ct.position = glm::vec3(5, 0, 0);

    auto& childHierarchy = reg.emplace<HierarchyComponent>(child);
    childHierarchy.parent = parent;
    auto& parentHierarchy = reg.emplace<HierarchyComponent>(parent);
    parentHierarchy.addChild(child);

    system.update(reg, 0.016f);

    // Child world position should be parent + child = (15, 0, 0)
    glm::vec4 childWorldPos = ct.worldMatrix * glm::vec4(0, 0, 0, 1);
    EXPECT_NEAR(childWorldPos.x, 15.0f, 1e-4f);
    EXPECT_NEAR(childWorldPos.y, 0.0f, 1e-4f);
    EXPECT_NEAR(childWorldPos.z, 0.0f, 1e-4f);
}

TEST(TransformSystem, Disabled_DoesNotUpdate) {
    entt::registry reg;
    TransformSystem system;
    system.enabled = false;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(100, 0, 0);

    system.update(reg, 0.016f);

    // Should still be dirty (system was disabled)
    EXPECT_TRUE(t.isDirty);
    // localMatrix should still be identity (not updated)
    TestHelpers::ExpectMat4Near(t.localMatrix, glm::mat4(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// CameraSystem Tests
// ═══════════════════════════════════════════════════════════════

TEST(CameraSystem, ViewMatrix_InverseOfWorldMatrix) {
    entt::registry reg;
    TransformSystem transformSys;
    CameraSystem cameraSys;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(0, 5, 10);
    auto& cam = reg.emplace<CameraComponent>(entity);

    // Update transform first, then camera
    transformSys.update(reg, 0.016f);
    cameraSys.update(reg, 0.016f);

    // viewMatrix should be inverse of worldMatrix
    glm::mat4 expected = glm::inverse(t.worldMatrix);
    TestHelpers::ExpectMat4Near(cam.viewMatrix, expected);
}

TEST(CameraSystem, GetMainCamera_WithMainFlag) {
    entt::registry reg;
    CameraSystem cameraSys;

    auto cam1 = reg.create();
    reg.emplace<CameraComponent>(cam1);

    auto cam2 = reg.create();
    auto& mainCam = reg.emplace<CameraComponent>(cam2);
    mainCam.isMainCamera = true;

    EXPECT_EQ(cameraSys.getMainCamera(reg), cam2);
}

TEST(CameraSystem, GetMainCamera_NoMainFlag_ReturnFirst) {
    entt::registry reg;
    CameraSystem cameraSys;

    auto cam1 = reg.create();
    reg.emplace<CameraComponent>(cam1);

    // No isMainCamera set → returns first camera found
    auto result = cameraSys.getMainCamera(reg);
    EXPECT_TRUE(result != entt::null);
}

TEST(CameraSystem, GetMainCamera_NoCameras_ReturnsNull) {
    entt::registry reg;
    CameraSystem cameraSys;

    EXPECT_TRUE(cameraSys.getMainCamera(reg) == entt::null);
}

// ═══════════════════════════════════════════════════════════════
// LifetimeSystem Tests
// ═══════════════════════════════════════════════════════════════

TEST(LifetimeSystem, TTL_Decrements) {
    entt::registry reg;
    LifetimeSystem system;

    auto entity = reg.create();
    auto& lifetime = reg.emplace<LifetimeComponent>(entity, 5.0f);

    system.update(reg, 1.0f);
    EXPECT_NEAR(lifetime.timeToLive, 4.0f, 1e-5f);
}

TEST(LifetimeSystem, DestroyOnExpire_True_RemovesEntity) {
    entt::registry reg;
    LifetimeSystem system;

    auto entity = reg.create();
    reg.emplace<LifetimeComponent>(entity, 0.5f);

    system.update(reg, 1.0f);  // TTL goes below 0
    EXPECT_FALSE(reg.valid(entity));
}

TEST(LifetimeSystem, DestroyOnExpire_False_EntitySurvives) {
    entt::registry reg;
    LifetimeSystem system;

    auto entity = reg.create();
    auto& lifetime = reg.emplace<LifetimeComponent>(entity, 0.5f);
    lifetime.destroyOnExpire = false;

    system.update(reg, 1.0f);
    EXPECT_TRUE(reg.valid(entity));
    EXPECT_LT(lifetime.timeToLive, 0.0f);
}

TEST(LifetimeSystem, OnlyExpired_Destroyed) {
    entt::registry reg;
    LifetimeSystem system;

    auto shortLived = reg.create();
    reg.emplace<LifetimeComponent>(shortLived, 0.1f);

    auto longLived = reg.create();
    reg.emplace<LifetimeComponent>(longLived, 10.0f);

    system.update(reg, 0.5f);

    EXPECT_FALSE(reg.valid(shortLived));
    EXPECT_TRUE(reg.valid(longLived));
}

TEST(LifetimeSystem, Disabled_DoesNotDestroy) {
    entt::registry reg;
    LifetimeSystem system;
    system.enabled = false;

    auto entity = reg.create();
    reg.emplace<LifetimeComponent>(entity, 0.1f);

    system.update(reg, 1.0f);
    EXPECT_TRUE(reg.valid(entity));  // System disabled, entity survives
}

// ═══════════════════════════════════════════════════════════════
// SystemManager Tests
// ═══════════════════════════════════════════════════════════════

TEST(SystemManager, AddSystem_ReturnsPointer) {
    SystemManager manager;
    auto* sys = manager.addSystem<TransformSystem>();
    EXPECT_NE(sys, nullptr);
}

TEST(SystemManager, GetSystem_ReturnsCorrectType) {
    SystemManager manager;
    manager.addSystem<TransformSystem>();
    manager.addSystem<CameraSystem>();

    auto* transformSys = manager.getSystem<TransformSystem>();
    auto* cameraSys = manager.getSystem<CameraSystem>();

    EXPECT_NE(transformSys, nullptr);
    EXPECT_NE(cameraSys, nullptr);
}

TEST(SystemManager, GetSystem_NotAdded_ReturnsNull) {
    SystemManager manager;
    EXPECT_EQ(manager.getSystem<TransformSystem>(), nullptr);
}

TEST(SystemManager, PriorityOrdering) {
    SystemManager manager;

    auto* camera = manager.addSystem<CameraSystem>();
    camera->priority = 10;

    auto* transform = manager.addSystem<TransformSystem>();
    transform->priority = 0;

    // Both should be findable
    EXPECT_NE(manager.getSystem<TransformSystem>(), nullptr);
    EXPECT_NE(manager.getSystem<CameraSystem>(), nullptr);
}

TEST(SystemManager, DisabledSystem_Skipped) {
    entt::registry reg;
    SystemManager manager;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(5, 0, 0);

    auto* transformSys = manager.addSystem<TransformSystem>();
    transformSys->enabled = false;

    manager.update(reg, 0.016f);

    // Transform should still be dirty because system was disabled
    EXPECT_TRUE(t.isDirty);
}

TEST(SystemManager, Update_AllEnabledSystemsRun) {
    entt::registry reg;
    SystemManager manager;

    auto entity = reg.create();
    auto& t = reg.emplace<TransformComponent>(entity);
    t.position = glm::vec3(0, 5, 0);
    auto& cam = reg.emplace<CameraComponent>(entity);

    manager.addSystem<TransformSystem>();
    manager.addSystem<CameraSystem>();
    manager.update(reg, 0.016f);

    // Transform should have been updated
    EXPECT_FALSE(t.isDirty);
    // Camera view should be set (inverse of world matrix)
    glm::mat4 expectedView = glm::inverse(t.worldMatrix);
    TestHelpers::ExpectMat4Near(cam.viewMatrix, expectedView);
}

// ═══════════════════════════════════════════════════════════════
// SystemManager Name Lookup Tests
// ═══════════════════════════════════════════════════════════════

TEST(SystemManager, FindSystem_ByName_ReturnsSystem) {
    SystemManager manager;
    auto* sys = manager.addSystem<CallbackSystem>("MySystem", [](float) { return true; });
    EXPECT_EQ(manager.findSystem("MySystem"), sys);
}

TEST(SystemManager, FindSystem_UnknownName_ReturnsNull) {
    SystemManager manager;
    manager.addSystem<CallbackSystem>("MySystem", [](float) { return true; });
    EXPECT_EQ(manager.findSystem("Nonexistent"), nullptr);
}

TEST(SystemManager, RemoveSystem_ByName_RemovesAndReturnsTrue) {
    SystemManager manager;
    manager.addSystem<CallbackSystem>("MySystem", [](float) { return true; });
    EXPECT_TRUE(manager.removeSystem("MySystem"));
    EXPECT_EQ(manager.findSystem("MySystem"), nullptr);
}

TEST(SystemManager, RemoveSystem_UnknownName_ReturnsFalse) {
    SystemManager manager;
    EXPECT_FALSE(manager.removeSystem("Nonexistent"));
}

// ═══════════════════════════════════════════════════════════════
// CallbackSystem Tests
// ═══════════════════════════════════════════════════════════════

TEST(CallbackSystem, Update_InvokesCallbackWithDeltaTime) {
    entt::registry reg;
    float lastDt = -1.0f;
    CallbackSystem system("Test", [&](float dt) { lastDt = dt; return true; });

    system.update(reg, 0.25f);
    EXPECT_FLOAT_EQ(lastDt, 0.25f);
}

TEST(CallbackSystem, PriorityIsSetFromConstructor) {
    CallbackSystem system("Test", [](float) { return true; }, /*priority=*/42);
    EXPECT_EQ(system.priority, 42);
}

TEST(CallbackSystem, NameIsSetFromConstructor) {
    CallbackSystem system("MyName", [](float) { return true; });
    EXPECT_EQ(system.name, "MyName");
}
