//
// CameraHistoryTest.cpp - scene.camera.prevViewProjection
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "ECS/Core.h"
#include "ECS/Systems.h"

using namespace Shoonyakasha;

TEST(CameraHistory, PreviousViewProjectionLagsOneUpdate) {
    entt::registry registry;
    auto camera = registry.create();
    registry.emplace<ECS::TransformComponent>(camera).position = glm::vec3(0.0f, 1.0f, 5.0f);
    registry.emplace<ECS::CameraComponent>(camera).isMainCamera = true;
    ECS::TransformSystem transforms;
    ECS::CameraSystem cameras;
    SceneContext scene;
    DotPathResolver resolver;

    transforms.update(registry, 0.016f);
    cameras.update(registry, 0.016f);
    scene.updateFromRegistry(registry);
    const glm::mat4 first = scene.cameraViewProjection;
    // No earlier frame: the current one.
    EXPECT_EQ(scene.cameraPrevViewProjection, first);

    auto& t = registry.get<ECS::TransformComponent>(camera);
    t.position.x += 1.0f;
    t.isDirty = true;
    transforms.update(registry, 0.016f);
    cameras.update(registry, 0.016f);
    scene.updateFromRegistry(registry);
    EXPECT_NE(scene.cameraViewProjection, first);
    EXPECT_EQ(scene.cameraPrevViewProjection, first);
    EXPECT_EQ(resolver.resolveScene("scene.camera.prevViewProjection", scene).as<glm::mat4>(), first);
}
