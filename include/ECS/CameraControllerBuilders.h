//
// ECS/CameraControllerBuilders.h - EntityBuilder Extensions for Camera Controllers
//
// Include this header AFTER Core.h and CameraController.h to enable
// the fluent camera controller builder methods.
//

#pragma once

#include "Core.h"
#include "CameraController.h"

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// EntityBuilder Camera Controller Methods Implementation
// ═══════════════════════════════════════════════════════════════

inline EntityBuilder& EntityBuilder::withFreeCameraController(float moveSpeed) {
    auto ctrl = CameraControllerComponent::createFreeCamera(moveSpeed);
    m_registry.emplace<CameraControllerComponent>(m_entity, ctrl);
    return *this;
}

inline EntityBuilder& EntityBuilder::withOrbitCameraController(
    const glm::vec3& target,
    float distance,
    bool autoRotate
) {
    auto ctrl = CameraControllerComponent::createOrbitCamera(target, distance, autoRotate);
    m_registry.emplace<CameraControllerComponent>(m_entity, ctrl);
    return *this;
}

inline EntityBuilder& EntityBuilder::withFirstPersonController(float eyeHeight) {
    auto ctrl = CameraControllerComponent::createFirstPersonCamera(eyeHeight);
    m_registry.emplace<CameraControllerComponent>(m_entity, ctrl);
    return *this;
}

inline EntityBuilder& EntityBuilder::withThirdPersonController(
    entt::entity target,
    const glm::vec3& offset
) {
    auto ctrl = CameraControllerComponent::createThirdPersonCamera(target, offset);
    m_registry.emplace<CameraControllerComponent>(m_entity, ctrl);
    return *this;
}

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;
