//
// CameraControllerTest.cpp - the free and first-person camera controllers
//
// Tier 2: ECS integration, no GPU.
//

#include <gtest/gtest.h>
#include "ECS/Systems.h"
#include "ECS/CameraController.h"

using namespace Shoonyakasha::ECS;

namespace {

struct ControlledCamera {
    entt::registry registry;
    entt::entity camera;
    CameraControllerSystem system;

    explicit ControlledCamera(CameraControllerComponent::Mode mode) {
        registry.emplace<InputStateComponent>(registry.create());
        camera = registry.create();
        auto& t = registry.emplace<TransformComponent>(camera);
        t.rotation = glm::vec3(0.1f, 0.2f, 0.0f);
        registry.emplace<CameraControllerComponent>(camera).mode = mode;
    }

    TransformComponent& transform() { return registry.get<TransformComponent>(camera); }
    void update() { system.update(registry, 0.016f); }
};

} // namespace

TEST(CameraController, FreeCameraKeepsItsRotationWithoutInput) {
    ControlledCamera c(CameraControllerComponent::Mode::Free);
    c.update();
    c.update();
    EXPECT_FLOAT_EQ(c.transform().rotation.x, 0.1f);
    EXPECT_FLOAT_EQ(c.transform().rotation.y, 0.2f);
}

TEST(CameraController, ResumesFromARotationSetElsewhere) {
    for (auto mode : {CameraControllerComponent::Mode::Free, CameraControllerComponent::Mode::FirstPerson}) {
        ControlledCamera c(mode);
        c.update();
        // A script's camera path turns the camera between updates.
        c.transform().rotation = glm::vec3(-0.3f, 1.4f, 0.0f);
        c.update();
        // The controller carries on from there instead of snapping back.
        EXPECT_FLOAT_EQ(c.transform().rotation.x, -0.3f);
        EXPECT_FLOAT_EQ(c.transform().rotation.y, 1.4f);
        c.update();
        EXPECT_FLOAT_EQ(c.transform().rotation.y, 1.4f);
    }
}

TEST(CameraController, MouseLookStillTurnsTheCamera) {
    ControlledCamera c(CameraControllerComponent::Mode::Free);
    c.update();
    auto& input = c.registry.get<InputStateComponent>(*c.registry.view<InputStateComponent>().begin());
    input.mouseCaptured = true;
    input.mouseDelta = glm::vec2(10.0f, 0.0f);
    c.update();
    EXPECT_LT(c.transform().rotation.y, 0.2f);
    const float turned = c.transform().rotation.y;
    input.mouseDelta = glm::vec2(0.0f);
    c.update();
    EXPECT_FLOAT_EQ(c.transform().rotation.y, turned);
}
