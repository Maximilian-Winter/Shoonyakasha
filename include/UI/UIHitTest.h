//
// UIHitTest.h - Which element of which canvas is under the pointer
//
// Reads the rects CanvasLayoutSystem wrote. Screen overlay canvases are tested
// first, the one drawn on top first; then world canvases, by the nearest quad
// the pointer's ray hits. A world canvas's quad is the unit quad of its
// entity's TransformComponent::worldMatrix, as UIContext::createWorldCanvas
// makes it.
//

#pragma once

#include "ECS/CameraRay.h"
#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <optional>

namespace Shoonyakasha {
namespace UI {

/// Is the element a raycast target: interactable, or with a UIImage or
/// UIPanel whose raycastTarget is set?
bool isRaycastTarget(const entt::registry& registry, entt::entity element);

/// The topmost visible raycast target of `canvas` whose rect, clipped, holds
/// `point` (canvas units). Null when there is none.
entt::entity hitTestCanvas(const entt::registry& registry, entt::entity canvas, glm::vec2 point);

/// `element` or its nearest ancestor with UIInteractable, up to its canvas.
/// Null when there is none.
entt::entity interactableFor(const entt::registry& registry, entt::entity element);

struct PointerHit {
    entt::entity canvas = entt::null;
    glm::vec2 position{0.0f};          // on the canvas, in canvas units
    entt::entity element = entt::null; // topmost raycast target there, or null
};

/// The canvas under the pointer and the element hit on it. `pixel` is in
/// screen pixels; `ray` is the pointer's ray from the main camera, if there
/// is one. A screen canvas counts only where it has a raycast target, and a
/// world canvas wherever the ray meets its quad's front, so the nearest quad
/// hides those behind it.
std::optional<PointerHit> findPointerHit(const entt::registry& registry, glm::vec2 pixel,
                                         const std::optional<ECS::Ray>& ray);

/// Where `pixel` or `ray` meets `canvas`, in canvas units, also outside it:
/// for a world canvas, on its quad's plane. Nothing when they cannot meet.
std::optional<glm::vec2> canvasPosition(const entt::registry& registry, entt::entity canvas, glm::vec2 pixel,
                                        const std::optional<ECS::Ray>& ray);

} // namespace UI
} // namespace Shoonyakasha
