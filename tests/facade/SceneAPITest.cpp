//
// SceneAPITest.cpp - Tests for the SceneAPI facade
//
// Tier 2: Uses entt::registry + ComponentRegistry directly (no GPU)
//
// SHOONYAKASHA_TESTING is defined by CMake, enabling the test constructor.
//

#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include "ECS/Core.h"
#include "ECS/RenderComponents.h"
#include "Facade/SceneAPI.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;
using namespace Shoonyakasha::ECS;
using namespace Shoonyakasha::Facade;

// ═══════════════════════════════════════════════════════════════
// Test Fixture — standalone registry with ComponentRegistry
// ═══════════════════════════════════════════════════════════════

class SceneAPIFixture : public testing::Test {
protected:
    void SetUp() override {
        registerAllComponents(compReg);
        api = std::make_unique<SceneAPI>(registry, compReg);
    }

    entt::registry registry;
    ComponentRegistry compReg;
    std::unique_ptr<SceneAPI> api;
};

// ═══════════════════════════════════════════════════════════════
// Entity Lifecycle
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, CreateEntity_ReturnsValidHandle) {
    auto entity = api->createEntity("Player");
    EXPECT_TRUE(api->isValid(entity));
    EXPECT_NE(entity, NullEntity);
}

TEST_F(SceneAPIFixture, CreateEntity_HasTransformAndActive) {
    auto entity = api->createEntity("Test");
    auto e = static_cast<entt::entity>(entity);
    EXPECT_TRUE(registry.all_of<TransformComponent>(e));
    EXPECT_TRUE(registry.all_of<ActiveComponent>(e));
}

TEST_F(SceneAPIFixture, CreateEntity_WithName) {
    auto entity = api->createEntity("MyEntity");
    EXPECT_EQ(api->getName(entity), "MyEntity");
}

TEST_F(SceneAPIFixture, CreateEntity_NoName_EmptyName) {
    auto entity = api->createEntity();
    // No NameComponent unless name is provided
    auto e = static_cast<entt::entity>(entity);
    EXPECT_FALSE(registry.all_of<NameComponent>(e));
}

TEST_F(SceneAPIFixture, DestroyEntity_InvalidatesHandle) {
    auto entity = api->createEntity("Temp");
    EXPECT_TRUE(api->isValid(entity));
    api->destroyEntity(entity);
    EXPECT_FALSE(api->isValid(entity));
}

TEST_F(SceneAPIFixture, DestroyEntity_NullEntity_NoOp) {
    api->destroyEntity(NullEntity);  // Should not crash
}

TEST_F(SceneAPIFixture, GetEntityCount) {
    EXPECT_EQ(api->getEntityCount(), 0u);
    api->createEntity("A");
    api->createEntity("B");
    EXPECT_EQ(api->getEntityCount(), 2u);
}

TEST_F(SceneAPIFixture, IsValid_NullEntity_False) {
    EXPECT_FALSE(api->isValid(NullEntity));
}

// ═══════════════════════════════════════════════════════════════
// Entity Queries
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, FindEntityByName_Found) {
    auto entity = api->createEntity("Target");
    EXPECT_EQ(api->findEntityByName("Target"), entity);
}

TEST_F(SceneAPIFixture, FindEntityByName_NotFound) {
    api->createEntity("Other");
    EXPECT_EQ(api->findEntityByName("Missing"), NullEntity);
}

TEST_F(SceneAPIFixture, FindEntitiesWithTag) {
    auto e1 = api->createEntity("A");
    api->setTag(e1, "Enemy");
    auto e2 = api->createEntity("B");
    api->setTag(e2, "Enemy");
    auto e3 = api->createEntity("C");
    api->setTag(e3, "Player");

    auto enemies = api->findEntitiesWithTag("Enemy");
    EXPECT_EQ(enemies.size(), 2u);
}

TEST_F(SceneAPIFixture, GetAllEntities) {
    api->createEntity("A");
    api->createEntity("B");
    api->createEntity("C");
    auto all = api->getAllEntities();
    EXPECT_EQ(all.size(), 3u);
}

// ═══════════════════════════════════════════════════════════════
// Component Management (string-based)
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, AddComponent_Camera) {
    auto entity = api->createEntity("Cam");
    EXPECT_FALSE(api->hasComponent(entity, "Camera"));
    EXPECT_TRUE(api->addComponent(entity, "Camera"));
    EXPECT_TRUE(api->hasComponent(entity, "Camera"));
}

TEST_F(SceneAPIFixture, RemoveComponent) {
    auto entity = api->createEntity("Test");
    api->addComponent(entity, "Light");
    EXPECT_TRUE(api->hasComponent(entity, "Light"));
    api->removeComponent(entity, "Light");
    EXPECT_FALSE(api->hasComponent(entity, "Light"));
}

TEST_F(SceneAPIFixture, AddComponent_UnknownName_ReturnsFalse) {
    auto entity = api->createEntity("Test");
    EXPECT_FALSE(api->addComponent(entity, "Nonexistent"));
}

TEST_F(SceneAPIFixture, HasComponent_InvalidEntity_ReturnsFalse) {
    EXPECT_FALSE(api->hasComponent(NullEntity, "Transform"));
}

TEST_F(SceneAPIFixture, GetComponentNames_ContainsExpected) {
    auto names = api->getComponentNames();
    EXPECT_GE(names.size(), 9u);  // Tag, Name, Hierarchy, Transform, Camera, Light, RigidBody, Collider, Active

    auto has = [&](const std::string& n) {
        return std::find(names.begin(), names.end(), n) != names.end();
    };
    EXPECT_TRUE(has("Transform"));
    EXPECT_TRUE(has("Camera"));
    EXPECT_TRUE(has("Light"));
}

// ═══════════════════════════════════════════════════════════════
// Name / Tag / Active
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, SetGetName) {
    auto entity = api->createEntity("Original");
    api->setName(entity, "Renamed");
    EXPECT_EQ(api->getName(entity), "Renamed");
}

TEST_F(SceneAPIFixture, SetName_NoNameComponent_CreatesOne) {
    auto entity = api->createEntity();  // No name
    api->setName(entity, "Added");
    EXPECT_EQ(api->getName(entity), "Added");
}

TEST_F(SceneAPIFixture, SetGetTag) {
    auto entity = api->createEntity("Test");
    api->setTag(entity, "Player");
    EXPECT_EQ(api->getTag(entity), "Player");
}

TEST_F(SceneAPIFixture, IsActive_Default_True) {
    auto entity = api->createEntity("Test");
    EXPECT_TRUE(api->isActive(entity));
}

TEST_F(SceneAPIFixture, SetActive_False) {
    auto entity = api->createEntity("Test");
    api->setActive(entity, false);
    EXPECT_FALSE(api->isActive(entity));
}

// ═══════════════════════════════════════════════════════════════
// Transform Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, SetGetPosition) {
    auto entity = api->createEntity("Test");
    api->setPosition(entity, glm::vec3(1.0f, 2.0f, 3.0f));
    TestHelpers::ExpectVec3Near(api->getPosition(entity), glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(SceneAPIFixture, SetGetRotation) {
    auto entity = api->createEntity("Test");
    glm::vec3 rot(0.1f, 0.2f, 0.3f);
    api->setRotation(entity, rot);
    TestHelpers::ExpectVec3Near(api->getRotation(entity), rot);
}

TEST_F(SceneAPIFixture, SetGetScale) {
    auto entity = api->createEntity("Test");
    api->setScale(entity, glm::vec3(2.0f, 3.0f, 4.0f));
    TestHelpers::ExpectVec3Near(api->getScale(entity), glm::vec3(2.0f, 3.0f, 4.0f));
}

TEST_F(SceneAPIFixture, SetPosition_MarksDirty) {
    auto entity = api->createEntity("Test");
    auto e = static_cast<entt::entity>(entity);
    registry.get<TransformComponent>(e).isDirty = false;  // Clear dirty
    api->setPosition(entity, glm::vec3(5.0f, 0.0f, 0.0f));
    EXPECT_TRUE(registry.get<TransformComponent>(e).isDirty);
}

TEST_F(SceneAPIFixture, GetForward_Default_NegativeZ) {
    auto entity = api->createEntity("Test");
    TestHelpers::ExpectVec3Near(api->getForward(entity), glm::vec3(0.0f, 0.0f, -1.0f));
}

TEST_F(SceneAPIFixture, GetPosition_InvalidEntity_ReturnsZero) {
    TestHelpers::ExpectVec3Near(api->getPosition(NullEntity), glm::vec3(0.0f));
}

TEST_F(SceneAPIFixture, GetScale_Default_One) {
    auto entity = api->createEntity("Test");
    TestHelpers::ExpectVec3Near(api->getScale(entity), glm::vec3(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Camera Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, Camera_SetGetFov) {
    auto entity = api->createEntity("Cam");
    api->addComponent(entity, "Camera");
    api->setCameraFov(entity, 90.0f);
    EXPECT_FLOAT_EQ(api->getCameraFov(entity), 90.0f);
}

TEST_F(SceneAPIFixture, Camera_SetGetType) {
    auto entity = api->createEntity("Cam");
    api->addComponent(entity, "Camera");
    api->setCameraType(entity, CameraType::Orthographic);
    EXPECT_EQ(api->getCameraType(entity), CameraType::Orthographic);
}

TEST_F(SceneAPIFixture, Camera_SetGetNearFar) {
    auto entity = api->createEntity("Cam");
    api->addComponent(entity, "Camera");
    api->setCameraNear(entity, 0.5f);
    api->setCameraFar(entity, 500.0f);
    EXPECT_FLOAT_EQ(api->getCameraNear(entity), 0.5f);
    EXPECT_FLOAT_EQ(api->getCameraFar(entity), 500.0f);
}

TEST_F(SceneAPIFixture, Camera_SetMainCamera) {
    auto entity = api->createEntity("Cam");
    api->addComponent(entity, "Camera");
    EXPECT_FALSE(api->isCameraMain(entity));
    api->setCameraMain(entity, true);
    EXPECT_TRUE(api->isCameraMain(entity));
}

TEST_F(SceneAPIFixture, Camera_GetMainCamera) {
    auto cam1 = api->createEntity("Cam1");
    api->addComponent(cam1, "Camera");
    auto cam2 = api->createEntity("Cam2");
    api->addComponent(cam2, "Camera");
    api->setCameraMain(cam2, true);

    EXPECT_EQ(api->getMainCamera(), cam2);
}

TEST_F(SceneAPIFixture, Camera_Fov_InvalidEntity_Default) {
    EXPECT_FLOAT_EQ(api->getCameraFov(NullEntity), 45.0f);
}

// ═══════════════════════════════════════════════════════════════
// Light Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, Light_SetGetColor) {
    auto entity = api->createEntity("Light");
    api->addComponent(entity, "Light");
    api->setLightColor(entity, glm::vec3(1.0f, 0.5f, 0.0f));
    TestHelpers::ExpectVec3Near(api->getLightColor(entity), glm::vec3(1.0f, 0.5f, 0.0f));
}

TEST_F(SceneAPIFixture, Light_SetGetIntensity) {
    auto entity = api->createEntity("Light");
    api->addComponent(entity, "Light");
    api->setLightIntensity(entity, 5.0f);
    EXPECT_FLOAT_EQ(api->getLightIntensity(entity), 5.0f);
}

TEST_F(SceneAPIFixture, Light_SetGetType) {
    auto entity = api->createEntity("Light");
    api->addComponent(entity, "Light");
    api->setLightType(entity, LightType::Directional);
    EXPECT_EQ(api->getLightType(entity), LightType::Directional);
}

TEST_F(SceneAPIFixture, Light_SetGetRange) {
    auto entity = api->createEntity("Light");
    api->addComponent(entity, "Light");
    api->setLightRange(entity, 25.0f);
    EXPECT_FLOAT_EQ(api->getLightRange(entity), 25.0f);
}

TEST_F(SceneAPIFixture, Light_SetGetCastShadows) {
    auto entity = api->createEntity("Light");
    api->addComponent(entity, "Light");
    EXPECT_FALSE(api->getLightCastShadows(entity));
    api->setLightCastShadows(entity, true);
    EXPECT_TRUE(api->getLightCastShadows(entity));
}

// ═══════════════════════════════════════════════════════════════
// Material Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, Material_SetGetFloat) {
    auto entity = api->createEntity("Mesh");
    auto e = static_cast<entt::entity>(entity);
    registry.emplace<MaterialComponentV5>(e);

    api->setMaterialFloat(entity, "roughness", 0.7f);
    EXPECT_FLOAT_EQ(api->getMaterialFloat(entity, "roughness"), 0.7f);
}

TEST_F(SceneAPIFixture, Material_SetGetVec4) {
    auto entity = api->createEntity("Mesh");
    auto e = static_cast<entt::entity>(entity);
    registry.emplace<MaterialComponentV5>(e);

    glm::vec4 color(1.0f, 0.0f, 0.0f, 1.0f);
    api->setMaterialVec4(entity, "baseColorFactor", color);
    TestHelpers::ExpectVec4Near(api->getMaterialVec4(entity, "baseColorFactor"), color);
}

TEST_F(SceneAPIFixture, Material_HasParam) {
    auto entity = api->createEntity("Mesh");
    auto e = static_cast<entt::entity>(entity);
    registry.emplace<MaterialComponentV5>(e);

    EXPECT_FALSE(api->hasMaterialParam(entity, "metallic"));
    api->setMaterialFloat(entity, "metallic", 1.0f);
    EXPECT_TRUE(api->hasMaterialParam(entity, "metallic"));
}

TEST_F(SceneAPIFixture, Material_GetFloat_Missing_ReturnsDefault) {
    auto entity = api->createEntity("Mesh");
    auto e = static_cast<entt::entity>(entity);
    registry.emplace<MaterialComponentV5>(e);

    EXPECT_FLOAT_EQ(api->getMaterialFloat(entity, "missing", 42.0f), 42.0f);
}

TEST_F(SceneAPIFixture, Material_InvalidEntity_ReturnsDefault) {
    EXPECT_FLOAT_EQ(api->getMaterialFloat(NullEntity, "roughness", 0.5f), 0.5f);
    EXPECT_FALSE(api->hasMaterialParam(NullEntity, "anything"));
}

// ═══════════════════════════════════════════════════════════════
// Renderable Tag Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, Renderable_SetGetVisible) {
    auto entity = api->createEntity("Mesh");
    auto e = static_cast<entt::entity>(entity);
    registry.emplace<RenderableTagComponent>(e);

    EXPECT_TRUE(api->isVisible(entity));
    api->setVisible(entity, false);
    EXPECT_FALSE(api->isVisible(entity));
}

TEST_F(SceneAPIFixture, Renderable_NoTag_NotVisible) {
    auto entity = api->createEntity("Plain");
    EXPECT_FALSE(api->isVisible(entity));
}

// ═══════════════════════════════════════════════════════════════
// Hierarchy Access
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, Hierarchy_SetGetParent) {
    auto parent = api->createEntity("Parent");
    auto child = api->createEntity("Child");

    api->setParent(child, parent);
    EXPECT_EQ(api->getParent(child), parent);
}

TEST_F(SceneAPIFixture, Hierarchy_GetChildren) {
    auto parent = api->createEntity("Parent");
    auto child1 = api->createEntity("Child1");
    auto child2 = api->createEntity("Child2");

    api->setParent(child1, parent);
    api->setParent(child2, parent);

    auto children = api->getChildren(parent);
    EXPECT_EQ(children.size(), 2u);
}

TEST_F(SceneAPIFixture, Hierarchy_NoParent_ReturnsNull) {
    auto entity = api->createEntity("Root");
    EXPECT_EQ(api->getParent(entity), NullEntity);
}

TEST_F(SceneAPIFixture, Hierarchy_NoChildren_ReturnsEmpty) {
    auto entity = api->createEntity("Leaf");
    EXPECT_TRUE(api->getChildren(entity).empty());
}

// ═══════════════════════════════════════════════════════════════
// Serialization (test mode: no Scene, returns false)
// ═══════════════════════════════════════════════════════════════

TEST_F(SceneAPIFixture, SaveToFile_TestMode_ReturnsFalse) {
    EXPECT_FALSE(api->saveToFile("test_scene.json"));
}

TEST_F(SceneAPIFixture, LoadFromFile_TestMode_ReturnsFalse) {
    EXPECT_FALSE(api->loadFromFile("test_scene.json"));
}
