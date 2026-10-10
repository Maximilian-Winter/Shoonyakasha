//
// UISystems.h - ECS systems of the canvas UI
//
// They run after TransformSystem and CameraSystem (priority 0). Each sets its
// priority in its constructor, before SystemManager::addSystem sorts.
//

#pragma once

#include "ECS/Systems.h"

#include <glm/glm.hpp>

namespace Shoonyakasha {
namespace UI {

/// Lays out every canvas each frame. Elements no longer under a canvas have
/// visibleInHierarchy cleared and canvas set to null.
class CanvasLayoutSystem : public ECS::ISystem {
public:
    static constexpr int kPriority = 51;

    /// `screenSize` is read on each update; the owner keeps it current.
    explicit CanvasLayoutSystem(const glm::vec2* screenSize);

    void update(entt::registry& registry, float deltaTime) override;

private:
    const glm::vec2* m_screenSize;
};

} // namespace UI
} // namespace Shoonyakasha
