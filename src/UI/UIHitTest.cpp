//
// UIHitTest.cpp - Which element of which canvas is under the pointer
//

#include "UI/UIHitTest.h"
#include "UI/CanvasLayout.h"

#include "ECS/Core.h"

#include <algorithm>
#include <vector>

namespace Shoonyakasha {
namespace UI {

bool isRaycastTarget(const entt::registry& registry, entt::entity element) {
    if (registry.any_of<UIInteractable, UIScrollView>(element)) return true;
    if (const auto* image = registry.try_get<UIImage>(element); image && image->raycastTarget) return true;
    if (const auto* panel = registry.try_get<UIPanel>(element); panel && panel->raycastTarget) return true;
    if (const auto* cache = registry.try_get<UITextCache>(element); cache && !cache->links.empty()) return true;
    return false;
}

entt::entity hitTestCanvas(const entt::registry& registry, entt::entity canvas, glm::vec2 point) {
    entt::entity best = entt::null;
    uint32_t bestOrder = 0;
    for (auto [entity, element] : registry.view<const UIRect>().each()) {
        if (element.canvas != canvas || !element.visibleInHierarchy) continue;
        if (best != entt::null && element.drawOrder < bestOrder) continue;
        if (!intersect(element.rect, element.clip).contains(point)) continue;
        if (!isRaycastTarget(registry, entity)) continue;
        best = entity;
        bestOrder = element.drawOrder;
    }
    return best;
}

entt::entity interactableFor(const entt::registry& registry, entt::entity element) {
    for (entt::entity e = element; e != entt::null && registry.valid(e);) {
        if (registry.all_of<UICanvas>(e)) break;
        if (registry.all_of<UIInteractable>(e)) return e;
        const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e);
        e = hierarchy ? hierarchy->parent : entt::null;
    }
    return entt::null;
}

namespace {

std::optional<ECS::QuadHit> worldHit(const entt::registry& registry, entt::entity canvas, const ECS::Ray& ray,
                                     bool insideOnly) {
    const auto* transform = registry.try_get<ECS::TransformComponent>(canvas);
    if (!transform) return std::nullopt;
    return insideOnly ? ECS::intersectUnitQuad(ray, transform->worldMatrix)
                      : ECS::intersectQuadPlane(ray, transform->worldMatrix);
}

} // namespace

std::optional<PointerHit> findPointerHit(const entt::registry& registry, glm::vec2 pixel,
                                         const std::optional<ECS::Ray>& ray, float maxWorldDistance) {
    // Screen overlays, the one drawn last first.
    std::vector<std::pair<int, entt::entity>> overlays;
    for (auto [entity, canvas] : registry.view<const UICanvas>().each()) {
        if (canvas.mode == UICanvas::Mode::ScreenOverlay) overlays.emplace_back(canvas.sortOrder, entity);
    }
    std::stable_sort(overlays.begin(), overlays.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [order, entity] : overlays) {
        const auto& canvas = registry.get<UICanvas>(entity);
        const glm::vec2 position = pixel / canvas.scale;
        const entt::entity element = hitTestCanvas(registry, entity, position);
        if (element != entt::null) return PointerHit{entity, position, element};
    }

    if (!ray) return std::nullopt;

    // World canvases: the nearest front face the ray meets.
    std::optional<PointerHit> nearest;
    float nearestDistance = 0.0f;
    for (auto [entity, canvas] : registry.view<const UICanvas>().each()) {
        if (canvas.mode != UICanvas::Mode::WorldTexture) continue;
        const auto hit = worldHit(registry, entity, *ray, true);
        if (!hit || !hit->frontFacing || hit->distance > maxWorldDistance) continue;
        if (nearest && hit->distance >= nearestDistance) continue;
        const glm::vec2 position = hit->uv * canvas.size;
        nearest = PointerHit{entity, position, hitTestCanvas(registry, entity, position)};
        nearestDistance = hit->distance;
    }
    return nearest;
}

std::optional<glm::vec2> canvasPosition(const entt::registry& registry, entt::entity canvas, glm::vec2 pixel,
                                        const std::optional<ECS::Ray>& ray) {
    const auto* c = registry.try_get<UICanvas>(canvas);
    if (!c) return std::nullopt;
    if (c->mode == UICanvas::Mode::ScreenOverlay) return pixel / c->scale;
    if (!ray) return std::nullopt;
    const auto hit = worldHit(registry, canvas, *ray, false);
    if (!hit) return std::nullopt;
    return hit->uv * c->size;
}

std::optional<glm::vec2> screenPosition(const entt::registry& registry, entt::entity canvas, glm::vec2 point,
                                        glm::vec2 screenSize) {
    const auto* c = registry.try_get<UICanvas>(canvas);
    if (!c) return std::nullopt;
    if (c->mode == UICanvas::Mode::ScreenOverlay) return point * c->scale;

    const auto* transform = registry.try_get<ECS::TransformComponent>(canvas);
    if (!transform || c->size.x <= 0.0f || c->size.y <= 0.0f) return std::nullopt;
    for (auto [entity, camera] : registry.view<const ECS::CameraComponent>().each()) {
        if (!camera.isMainCamera) continue;
        const glm::vec2 uv = point / c->size;
        const glm::vec4 clip = camera.projectionMatrix * camera.viewMatrix * transform->worldMatrix *
                               glm::vec4(uv.x - 0.5f, 0.5f - uv.y, 0.0f, 1.0f);
        if (clip.w <= 0.0f) return std::nullopt;   // behind the camera
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        return glm::vec2((ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f) * screenSize;
    }
    return std::nullopt;
}

std::optional<Rect> textInputRect(const entt::registry& registry, entt::entity field, glm::vec2 screenSize,
                                  glm::vec2 windowSize) {
    const auto* state = registry.try_get<UITextField>(field);
    const auto* caret = state ? registry.try_get<UIRect>(state->caretElement) : nullptr;
    if (!caret || caret->canvas == entt::null || screenSize.x <= 0.0f || screenSize.y <= 0.0f) return std::nullopt;

    Rect pixels;
    const glm::vec2 corners[] = {caret->rect.min, {caret->rect.max.x, caret->rect.min.y}, caret->rect.max,
                                 {caret->rect.min.x, caret->rect.max.y}};
    for (size_t i = 0; i < 4; ++i) {
        const auto p = screenPosition(registry, caret->canvas, corners[i], screenSize);
        if (!p) return std::nullopt;
        pixels.min = i == 0 ? *p : glm::min(pixels.min, *p);
        pixels.max = i == 0 ? *p : glm::max(pixels.max, *p);
    }
    if (pixels.max.x < 0.0f || pixels.max.y < 0.0f || pixels.min.x > screenSize.x || pixels.min.y > screenSize.y) {
        return std::nullopt;   // off the screen
    }
    const glm::vec2 toWindow = windowSize.x > 0.0f && windowSize.y > 0.0f ? windowSize / screenSize : glm::vec2(1.0f);
    return Rect{pixels.min * toWindow, pixels.max * toWindow};
}

} // namespace UI
} // namespace Shoonyakasha
