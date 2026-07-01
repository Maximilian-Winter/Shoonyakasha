//
// UILayoutSystem.h - Resolves UIAnchorComponent into pixel-space TransformComponent
//
// Runs once per frame, before TransformSystem (lower priority number), so
// that world matrices reflect the resolved anchor position the same frame.
// Screen size is supplied by a pointer the owner (ApplicationBase) updates
// each frame from the current swapchain/render extent.
//

#pragma once

#include "ECS/Systems.h"
#include "ECS/Sprite2DComponents.h"
#include <glm/glm.hpp>

namespace Shoonyakasha {
namespace ECS {

class UILayoutSystem : public ISystem {
public:
    // screenSize is read each update() call - the owner updates *screenSize
    // (e.g. from the current swapchain extent) before scene->update() runs.
    explicit UILayoutSystem(const glm::vec2* screenSize) : m_screenSize(screenSize) {}

    void update(entt::registry& registry, float deltaTime) override {
        if (!enabled || !m_screenSize) return;

        auto view = registry.view<Shoonyakasha::UIAnchorComponent, TransformComponent>();
        for (auto entity : view) {
            auto& anchor = view.get<Shoonyakasha::UIAnchorComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            glm::vec2 resolved = anchor.resolve(m_screenSize->x, m_screenSize->y);
            glm::vec3 newPos(resolved, transform.position.z);

            if (newPos != transform.position) {
                transform.position = newPos;
                transform.isDirty = true;
            }
        }
    }

private:
    const glm::vec2* m_screenSize;
};

} // namespace ECS
} // namespace Shoonyakasha
