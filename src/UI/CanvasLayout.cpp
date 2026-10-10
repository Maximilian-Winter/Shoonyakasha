//
// CanvasLayout.cpp - Resolving UIRect placement for a canvas tree
//

#include "UI/CanvasLayout.h"
#include "UI/UISystems.h"

#include "ECS/Core.h"

#include <cmath>

namespace Shoonyakasha {
namespace UI {

Rect intersect(const Rect& a, const Rect& b) {
    Rect out;
    out.min = glm::max(a.min, b.min);
    out.max = glm::max(out.min, glm::min(a.max, b.max));
    return out;
}

Rect resolveRect(const Rect& parent, const UIRect& element) {
    const glm::vec2 parentSize = parent.size();
    const glm::vec2 anchorMin = parent.min + element.anchorMin * parentSize;
    const glm::vec2 anchorMax = parent.min + element.anchorMax * parentSize;

    const glm::vec2 size = (anchorMax - anchorMin) + element.sizeDelta;
    const glm::vec2 pivotPoint = anchorMin + (anchorMax - anchorMin) * element.pivot
                               + element.anchoredPosition;

    Rect out;
    out.min = pivotPoint - size * element.pivot;
    out.max = out.min + size;
    return out;
}

float canvasScale(const UICanvas& canvas, const glm::vec2& targetSize) {
    if (targetSize.x <= 0.0f || targetSize.y <= 0.0f) return 1.0f;

    if (canvas.scaleMode == UICanvas::ScaleMode::ScaleWithScreen &&
        canvas.referenceSize.x > 0.0f && canvas.referenceSize.y > 0.0f) {
        // Interpolate in log space so that halving one side and doubling the
        // other at match = 0.5 gives scale 1.
        const float logWidth = std::log2(targetSize.x / canvas.referenceSize.x);
        const float logHeight = std::log2(targetSize.y / canvas.referenceSize.y);
        return std::exp2(logWidth + (logHeight - logWidth) * canvas.match);
    }

    return canvas.scaleFactor > 0.0f ? canvas.scaleFactor : 1.0f;
}

namespace {

struct LayoutWalk {
    entt::registry& registry;
    entt::entity canvas;
    uint32_t nextOrder = 0;

    void children(entt::entity parent, const Rect& parentRect, const Rect& clip, bool visible) {
        const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(parent);
        if (!hierarchy) return;

        for (const entt::entity child : hierarchy->children) {
            if (!registry.valid(child) || registry.all_of<UICanvas>(child)) continue;
            auto* element = registry.try_get<UIRect>(child);
            if (!element) continue;

            element->rect = resolveRect(parentRect, *element);
            element->clip = clip;
            element->visibleInHierarchy = visible && element->visible;
            element->canvas = canvas;
            element->drawOrder = nextOrder++;

            const Rect childClip = registry.all_of<UIClip>(child)
                ? intersect(clip, element->rect)
                : clip;
            children(child, element->rect, childClip, element->visibleInHierarchy);
        }
    }
};

} // namespace

void layoutCanvas(entt::registry& registry, entt::entity canvasEntity, const glm::vec2& screenSize) {
    auto& canvas = registry.get<UICanvas>(canvasEntity);

    canvas.targetSize = canvas.mode == UICanvas::Mode::ScreenOverlay ? screenSize : canvas.pixelSize;
    canvas.scale = canvasScale(canvas, canvas.targetSize);
    canvas.size = glm::max(canvas.targetSize, glm::vec2(0.0f)) / canvas.scale;

    const Rect canvasRect{glm::vec2(0.0f), canvas.size};
    LayoutWalk walk{registry, canvasEntity};
    walk.children(canvasEntity, canvasRect, canvasRect, true);
}

CanvasLayoutSystem::CanvasLayoutSystem(const glm::vec2* screenSize)
    : m_screenSize(screenSize) {
    priority = kPriority;
    name = "CanvasLayoutSystem";
}

void CanvasLayoutSystem::update(entt::registry& registry, float /*deltaTime*/) {
    if (!enabled) return;

    for (auto [entity, element] : registry.view<UIRect>().each()) {
        element.visibleInHierarchy = false;
        element.canvas = entt::null;
    }

    const glm::vec2 screenSize = m_screenSize ? *m_screenSize : glm::vec2(0.0f);
    for (const entt::entity canvas : registry.view<UICanvas>()) {
        layoutCanvas(registry, canvas, screenSize);
    }
}

} // namespace UI
} // namespace Shoonyakasha
