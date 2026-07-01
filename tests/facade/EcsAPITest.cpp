//
// EcsAPITest.cpp - Tests for the EcsAPI facade (generic components + systems)
//
// Tier 2: Uses entt::registry + ECS::SystemManager directly (no GPU)
//
// SHOONYAKASHA_TESTING is defined by CMake, enabling the test constructor.
//

#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include "ECS/Core.h"
#include "ECS/Systems.h"
#include "Facade/EcsAPI.h"
#include "TestHelpers.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::ECS;
using namespace Shoonyakasha::Facade;

// ═══════════════════════════════════════════════════════════════
// Test Fixture — standalone registry + SystemManager
// ═══════════════════════════════════════════════════════════════

class EcsAPIFixture : public testing::Test {
protected:
    void SetUp() override {
        api = std::make_unique<EcsAPI>(registry, systemManager);
    }

    EntityHandle makeEntity() {
        return static_cast<EntityHandle>(registry.create());
    }

    entt::registry registry;
    SystemManager systemManager;
    std::unique_ptr<EcsAPI> api;
};

// ═══════════════════════════════════════════════════════════════
// Script Component Access
// ═══════════════════════════════════════════════════════════════

TEST_F(EcsAPIFixture, SetGetComponent_RoundTrips) {
    auto entity = makeEntity();
    auto payload = std::make_shared<int>(42);

    api->setComponent(entity, "Health", payload);

    auto retrieved = api->getComponent(entity, "Health");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(*static_cast<int*>(retrieved.get()), 42);
    // Same object identity, not a copy.
    EXPECT_EQ(retrieved.get(), payload.get());
}

TEST_F(EcsAPIFixture, GetComponent_Missing_ReturnsNull) {
    auto entity = makeEntity();
    EXPECT_EQ(api->getComponent(entity, "Nope"), nullptr);
}

TEST_F(EcsAPIFixture, HasComponent_ReflectsPresence) {
    auto entity = makeEntity();
    EXPECT_FALSE(api->hasComponent(entity, "Health"));
    api->setComponent(entity, "Health", std::make_shared<int>(1));
    EXPECT_TRUE(api->hasComponent(entity, "Health"));
}

TEST_F(EcsAPIFixture, RemoveComponent_RemovesAndReturnsTrue) {
    auto entity = makeEntity();
    api->setComponent(entity, "Health", std::make_shared<int>(1));
    EXPECT_TRUE(api->removeComponent(entity, "Health"));
    EXPECT_FALSE(api->hasComponent(entity, "Health"));
}

TEST_F(EcsAPIFixture, RemoveComponent_NotPresent_ReturnsFalse) {
    auto entity = makeEntity();
    EXPECT_FALSE(api->removeComponent(entity, "Health"));
}

TEST_F(EcsAPIFixture, SetComponent_InvalidEntity_NoOp) {
    EntityHandle bogus = 999999;
    api->setComponent(bogus, "Health", std::make_shared<int>(1));
    EXPECT_FALSE(api->hasComponent(bogus, "Health"));
}

TEST_F(EcsAPIFixture, GetComponentNames_ListsAll) {
    auto entity = makeEntity();
    api->setComponent(entity, "Health", std::make_shared<int>(1));
    api->setComponent(entity, "Mana", std::make_shared<int>(2));

    auto names = api->getComponentNames(entity);
    EXPECT_EQ(names.size(), 2u);
    EXPECT_NE(std::find(names.begin(), names.end(), "Health"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Mana"), names.end());
}

TEST_F(EcsAPIFixture, FindEntitiesWithComponent_FiltersByKey) {
    auto a = makeEntity();
    auto b = makeEntity();
    auto c = makeEntity();
    api->setComponent(a, "Health", std::make_shared<int>(1));
    api->setComponent(b, "Mana", std::make_shared<int>(1));
    api->setComponent(c, "Health", std::make_shared<int>(1));

    auto withHealth = api->findEntitiesWithComponent("Health");
    EXPECT_EQ(withHealth.size(), 2u);
}

// ═══════════════════════════════════════════════════════════════
// System Management
// ═══════════════════════════════════════════════════════════════

TEST_F(EcsAPIFixture, AddSystem_RunsEachUpdate) {
    int callCount = 0;
    api->addSystem("Counter", [&](float) { ++callCount; return true; });

    systemManager.update(registry, 0.016f);
    systemManager.update(registry, 0.016f);

    EXPECT_EQ(callCount, 2);
}

TEST_F(EcsAPIFixture, AddSystem_DuplicateName_Fails) {
    EXPECT_TRUE(api->addSystem("Foo", [](float) { return true; }));
    EXPECT_FALSE(api->addSystem("Foo", [](float) { return true; }));
}

TEST_F(EcsAPIFixture, RemoveSystem_StopsExecution) {
    int callCount = 0;
    api->addSystem("Counter", [&](float) { ++callCount; return true; });
    EXPECT_TRUE(api->removeSystem("Counter"));

    systemManager.update(registry, 0.016f);
    EXPECT_EQ(callCount, 0);
}

TEST_F(EcsAPIFixture, RemoveSystem_NotFound_ReturnsFalse) {
    EXPECT_FALSE(api->removeSystem("Nonexistent"));
}

TEST_F(EcsAPIFixture, SetSystemEnabled_DisablesExecution) {
    int callCount = 0;
    api->addSystem("Counter", [&](float) { ++callCount; return true; });
    EXPECT_TRUE(api->setSystemEnabled("Counter", false));

    systemManager.update(registry, 0.016f);
    EXPECT_EQ(callCount, 0);
    EXPECT_FALSE(api->isSystemEnabled("Counter"));
}

TEST_F(EcsAPIFixture, System_AutoDisablesAfterMaxConsecutiveFailures) {
    api->addSystem("Flaky", [](float) { return false; }, /*priority=*/0, /*maxConsecutiveFailures=*/3);

    EXPECT_TRUE(api->isSystemEnabled("Flaky"));
    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 1);
    EXPECT_TRUE(api->isSystemEnabled("Flaky"));

    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 2);
    EXPECT_TRUE(api->isSystemEnabled("Flaky"));

    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 3);
    EXPECT_FALSE(api->isSystemEnabled("Flaky"));

    // Disabled systems are skipped by SystemManager::update, so a further
    // update should not increase the failure count any further.
    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 3);
}

TEST_F(EcsAPIFixture, System_SuccessResetsFailureCount) {
    bool shouldFail = true;
    api->addSystem("SometimesFlaky", [&](float) { return !shouldFail; },
                   /*priority=*/0, /*maxConsecutiveFailures=*/5);

    systemManager.update(registry, 0.016f);
    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("SometimesFlaky"), 2);

    shouldFail = false;
    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("SometimesFlaky"), 0);
}

TEST_F(EcsAPIFixture, System_MaxFailuresZero_NeverAutoDisables) {
    api->addSystem("AlwaysFails", [](float) { return false; }, /*priority=*/0, /*maxConsecutiveFailures=*/0);

    for (int i = 0; i < 10; ++i) {
        systemManager.update(registry, 0.016f);
    }

    EXPECT_TRUE(api->isSystemEnabled("AlwaysFails"));
    EXPECT_EQ(api->getSystemFailureCount("AlwaysFails"), 10);
}

TEST_F(EcsAPIFixture, ResetSystemFailureCount_ClearsCounter) {
    api->addSystem("Flaky", [](float) { return false; }, 0, 10);
    systemManager.update(registry, 0.016f);
    systemManager.update(registry, 0.016f);
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 2);

    api->resetSystemFailureCount("Flaky");
    EXPECT_EQ(api->getSystemFailureCount("Flaky"), 0);
}

TEST_F(EcsAPIFixture, SystemPriority_ControlsExecutionOrder) {
    std::vector<std::string> order;
    api->addSystem("Second", [&](float) { order.push_back("Second"); return true; }, 10);
    api->addSystem("First", [&](float) { order.push_back("First"); return true; }, 0);

    systemManager.update(registry, 0.016f);

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "First");
    EXPECT_EQ(order[1], "Second");
}
