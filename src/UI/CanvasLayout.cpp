//
// CanvasLayout.cpp - Resolving UIRect placement for a canvas tree
//
// Each axis is laid out in two passes: layout sizes bottom up, then rects top
// down. The horizontal axis goes first, so that text knows its width when its
// height is measured.
//

#include "UI/CanvasLayout.h"
#include "UI/FontLibrary.h"
#include "UI/TextLayout.h"
#include "UI/UISystems.h"
#include "UI/Utf8.h"

#include "ECS/Core.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

void distributeSizes(const std::vector<LayoutSizes>& children, float available, bool forceExpand,
                     std::vector<float>& sizes) {
    sizes.resize(children.size());
    float sumMin = 0.0f, sumPreferred = 0.0f, sumFlexible = 0.0f;
    for (const auto& child : children) {
        sumMin += child.min;
        sumPreferred += child.preferred;
        sumFlexible += forceExpand ? std::max(child.flexible, 1.0f) : child.flexible;
    }
    for (size_t i = 0; i < children.size(); ++i) {
        const auto& child = children[i];
        if (available <= sumMin) {
            sizes[i] = child.min;
        } else if (available <= sumPreferred) {
            const float t = (available - sumMin) / (sumPreferred - sumMin);
            sizes[i] = child.min + (child.preferred - child.min) * t;
        } else if (sumFlexible > 0.0f) {
            const float flexible = forceExpand ? std::max(child.flexible, 1.0f) : child.flexible;
            sizes[i] = child.preferred + (available - sumPreferred) * flexible / sumFlexible;
        } else {
            sizes[i] = child.preferred;
        }
    }
}

int gridColumns(const UIGridLayout& grid, float innerWidth, size_t count) {
    if (grid.columns > 0) return grid.columns;
    const float step = grid.cellSize.x + grid.spacing.x;
    const int fit = step > 0.0f ? static_cast<int>(std::floor((innerWidth + grid.spacing.x) / step)) : 1;
    return std::clamp(fit, 1, std::max(1, static_cast<int>(count)));
}

namespace {

/// Padding before and after the content along `axis`.
glm::vec2 paddingAlong(const glm::vec4& padding, int axis) {
    return axis == 0 ? glm::vec2(padding.x, padding.z) : glm::vec2(padding.y, padding.w);
}

struct LayoutWalk {
    entt::registry& registry;
    entt::entity canvas;
    const FontLibrary* fonts;
    uint32_t nextOrder = 0;

    /// The UIRect children of `parent`, in order; nested canvases are not entered.
    template<typename F>
    void forEachChild(entt::entity parent, F&& f) {
        const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(parent);
        if (!hierarchy) return;
        for (const entt::entity child : hierarchy->children) {
            if (!registry.valid(child) || registry.all_of<UICanvas>(child)) continue;
            if (auto* element = registry.try_get<UIRect>(child)) f(child, *element);
        }
    }

    bool isGroup(entt::entity e) const {
        return registry.any_of<UIHorizontalLayout, UIVerticalLayout, UIGridLayout>(e);
    }

    /// Does `child` take part in its parent's layout group?
    bool participates(entt::entity child, const UIRect& element) const {
        if (!element.visible) return false;
        const auto* layout = registry.try_get<UILayoutElement>(child);
        return !(layout && layout->ignoreLayout);
    }

    std::vector<entt::entity> participants(entt::entity group) {
        std::vector<entt::entity> out;
        forEachChild(group, [&](entt::entity child, UIRect& element) {
            if (participates(child, element)) out.push_back(child);
        });
        return out;
    }

    const UILinearLayout* linearLayout(entt::entity e, int& mainAxis) const {
        if (const auto* h = registry.try_get<UIHorizontalLayout>(e)) { mainAxis = 0; return h; }
        if (const auto* v = registry.try_get<UIVerticalLayout>(e)) { mainAxis = 1; return v; }
        return nullptr;
    }

    // ── Text measurement ──

    UITextMeasure* measure(entt::entity e, const UIText& text) {
        if (!fonts) return nullptr;
        std::vector<uint32_t> chain = fonts->chain(text.font);
        if (chain.empty()) return nullptr;
        auto& m = registry.get_or_emplace<UITextMeasure>(e);
        if (m.text != text.text || m.fonts != chain || m.fontSize != text.fontSize ||
            m.lineSpacing != text.lineSpacing) {
            m.text = text.text;
            m.fonts = std::move(chain);
            m.fontSize = text.fontSize;
            m.lineSpacing = text.lineSpacing;
            TextStyle style;
            style.fontSize = text.fontSize;
            style.lineSpacing = text.lineSpacing;
            style.wrap = false;
            m.naturalSize = layoutText(fonts->resolve(m.fonts), decodeUtf8(text.text), style, glm::vec2(1e9f)).size;
            m.wrapWidth = -1.0f;
        }
        return &m;
    }

    float wrappedHeight(const UIText& text, UITextMeasure& m, float width) {
        if (!text.wrap || width >= m.naturalSize.x) return m.naturalSize.y;
        if (m.wrapWidth != width) {
            TextStyle style;
            style.fontSize = text.fontSize;
            style.lineSpacing = text.lineSpacing;
            style.wrap = true;
            m.wrappedHeight = layoutText(fonts->resolve(m.fonts), decodeUtf8(text.text), style,
                                         glm::vec2(std::max(width, 0.0f), 1e9f)).size.y;
            m.wrapWidth = width;
        }
        return m.wrappedHeight;
    }

    // ── Pass 1: layout sizes, bottom up ──

    /// Only the sizes of a group's participants, and of an element with a
    /// size fitter, are used; others report their sizeDelta without
    /// measuring their content.
    void computeSizes(entt::entity e, int axis, bool used = true) {
        const bool group = e != canvas && isGroup(e);
        forEachChild(e, [&](entt::entity child, UIRect& element) {
            computeSizes(child, axis, (group && participates(child, element)) ||
                                      registry.all_of<UIContentSizeFitter>(child));
        });
        if (e == canvas) return;

        auto& element = registry.get<UIRect>(e);
        LayoutSizes sizes;
        if (group) {
            sizes = groupSizes(e, axis);
        } else if (used) {
            sizes = contentSizes(e, element, axis);
        } else {
            sizes.min = sizes.preferred = std::max(element.sizeDelta[axis], 0.0f);
        }
        if (const auto* layout = registry.try_get<UILayoutElement>(e)) {
            if (layout->minSize[axis] >= 0.0f) sizes.min = layout->minSize[axis];
            if (layout->preferredSize[axis] >= 0.0f) sizes.preferred = layout->preferredSize[axis];
            if (layout->flexibleSize[axis] >= 0.0f) sizes.flexible = layout->flexibleSize[axis];
        }
        sizes.preferred = std::max(sizes.preferred, sizes.min);
        element.minSize[axis] = sizes.min;
        element.preferredSize[axis] = sizes.preferred;
        element.flexibleSize[axis] = sizes.flexible;
    }

    LayoutSizes contentSizes(entt::entity e, const UIRect& element, int axis) {
        if (const auto* text = registry.try_get<UIText>(e)) {
            if (auto* m = measure(e, *text)) {
                if (axis == 0) return {0.0f, m->naturalSize.x, 0.0f};
                const float height = wrappedHeight(*text, *m, element.rect.size().x);
                return {height, height, 0.0f};
            }
            return {};
        }
        if (const auto* image = registry.try_get<UIImage>(e); image && image->texture.view != VK_NULL_HANDLE) {
            const glm::vec2 size(static_cast<float>(image->texture.width), static_cast<float>(image->texture.height));
            return {0.0f, size[axis], 0.0f};
        }
        const float size = std::max(element.sizeDelta[axis], 0.0f);
        return {size, size, 0.0f};
    }

    /// A child's sizes as its group sees them along `axis`.
    LayoutSizes childSizes(const UIRect& child, bool controlled, int axis) const {
        if (!controlled) {
            const float size = std::max(child.sizeDelta[axis], 0.0f);
            return {size, size, 0.0f};
        }
        return {child.minSize[axis], child.preferredSize[axis], child.flexibleSize[axis]};
    }

    LayoutSizes groupSizes(entt::entity group, int axis) {
        const auto children = participants(group);
        int mainAxis = 0;
        if (const auto* linear = linearLayout(group, mainAxis)) {
            const glm::vec2 pad = paddingAlong(linear->padding, axis);
            LayoutSizes out;
            for (const entt::entity child : children) {
                LayoutSizes s = childSizes(registry.get<UIRect>(child), linear->controlChildSize[axis], axis);
                if (axis == mainAxis && linear->childForceExpand[axis]) s.flexible = std::max(s.flexible, 1.0f);
                if (axis == mainAxis) {
                    out.min += s.min;
                    out.preferred += s.preferred;
                    out.flexible += s.flexible;
                } else {
                    out.min = std::max(out.min, s.min);
                    out.preferred = std::max(out.preferred, s.preferred);
                    out.flexible = std::max(out.flexible, s.flexible);
                }
            }
            if (axis == mainAxis && children.size() > 1) {
                const float gaps = linear->spacing * static_cast<float>(children.size() - 1);
                out.min += gaps;
                out.preferred += gaps;
            }
            out.min += pad.x + pad.y;
            out.preferred += pad.x + pad.y;
            return out;
        }

        const auto& grid = registry.get<UIGridLayout>(group);
        const glm::vec2 pad = paddingAlong(grid.padding, axis);
        const size_t count = children.size();
        if (count == 0) return {pad.x + pad.y, pad.x + pad.y, 0.0f};
        auto span = [&](size_t cells) {
            return static_cast<float>(cells) * grid.cellSize[axis] +
                   static_cast<float>(cells - 1) * grid.spacing[axis] + pad.x + pad.y;
        };
        if (axis == 0) {
            const size_t preferredColumns = grid.columns > 0
                ? static_cast<size_t>(grid.columns)
                : static_cast<size_t>(std::ceil(std::sqrt(static_cast<float>(count))));
            return {span(grid.columns > 0 ? preferredColumns : 1), span(preferredColumns), 0.0f};
        }
        const auto& element = registry.get<UIRect>(group);
        const float innerWidth = element.rect.size().x - grid.padding.x - grid.padding.z;
        const size_t columns = static_cast<size_t>(gridColumns(grid, innerWidth, count));
        const float height = span((count + columns - 1) / columns);
        return {height, height, 0.0f};
    }

    // ── Pass 2: rects, top down ──

    void placeByAnchors(UIRect& element, const Rect& parent, int axis, const UIContentSizeFitter* fitter) {
        const float parentMin = parent.min[axis];
        const float parentSize = parent.max[axis] - parent.min[axis];
        const float anchorMin = parentMin + element.anchorMin[axis] * parentSize;
        const float anchorMax = parentMin + element.anchorMax[axis] * parentSize;

        float size = (anchorMax - anchorMin) + element.sizeDelta[axis];
        if (fitter) {
            const auto fit = axis == 0 ? fitter->horizontal : fitter->vertical;
            if (fit == UIContentSizeFitter::Fit::Min) size = element.minSize[axis];
            if (fit == UIContentSizeFitter::Fit::Preferred) size = element.preferredSize[axis];
        }
        const float pivotPoint = anchorMin + (anchorMax - anchorMin) * element.pivot[axis]
                               + element.anchoredPosition[axis];
        element.rect.min[axis] = pivotPoint - size * element.pivot[axis];
        element.rect.max[axis] = element.rect.min[axis] + size;
    }

    void place(entt::entity parent, const Rect& parentRect, int axis) {
        const bool group = isGroup(parent);
        auto* scrollView = registry.try_get<UIScrollView>(parent);
        forEachChild(parent, [&](entt::entity child, UIRect& element) {
            if (axis == 0) element.drivenByLayout = false;
            if (!group || !participates(child, element)) {
                placeByAnchors(element, parentRect, axis, registry.try_get<UIContentSizeFitter>(child));
                if (scrollView && child == scrollView->content) scroll(*scrollView, element, parentRect, axis);
            }
        });
        if (group) placeGroupChildren(parent, parentRect, axis);
        forEachChild(parent, [&](entt::entity child, UIRect& element) { place(child, element.rect, axis); });
    }

    /// Records the content and viewport sizes and moves the content, placed by
    /// its anchors, by the scroll offset along `axis`.
    void scroll(UIScrollView& view, UIRect& content, const Rect& viewport, int axis) {
        view.contentSize[axis] = content.rect.max[axis] - content.rect.min[axis];
        view.viewportSize[axis] = viewport.max[axis] - viewport.min[axis];
        const bool enabled = axis == 0 ? view.horizontal : view.vertical;
        view.scrollPosition[axis] = std::clamp(view.scrollPosition[axis], 0.0f, 1.0f);
        const float offset = enabled ? view.scrollPosition[axis] * view.range()[axis] : 0.0f;
        content.rect.min[axis] -= offset;
        content.rect.max[axis] -= offset;
    }

    void placeGroupChildren(entt::entity group, const Rect& rect, int axis) {
        const auto children = participants(group);
        if (children.empty()) return;

        int mainAxis = 0;
        if (const auto* linear = linearLayout(group, mainAxis)) {
            const glm::vec2 pad = paddingAlong(linear->padding, axis);
            const float innerMin = rect.min[axis] + pad.x;
            const float inner = std::max(rect.max[axis] - pad.y - innerMin, 0.0f);
            const bool controlled = linear->controlChildSize[axis];

            if (axis == mainAxis) {
                std::vector<LayoutSizes> sizes;
                for (const entt::entity child : children) {
                    sizes.push_back(childSizes(registry.get<UIRect>(child), controlled, axis));
                }
                const float gaps = linear->spacing * static_cast<float>(children.size() - 1);
                std::vector<float> lengths;
                distributeSizes(sizes, inner - gaps, linear->childForceExpand[axis] && controlled, lengths);
                float total = gaps;
                for (const float length : lengths) total += length;
                float position = innerMin + std::max(inner - total, 0.0f) * linear->childAlignment[axis];
                for (size_t i = 0; i < children.size(); ++i) {
                    auto& element = registry.get<UIRect>(children[i]);
                    element.rect.min[axis] = position;
                    element.rect.max[axis] = position + lengths[i];
                    element.drivenByLayout = true;
                    position += lengths[i] + linear->spacing;
                }
            } else {
                for (const entt::entity child : children) {
                    auto& element = registry.get<UIRect>(child);
                    const LayoutSizes s = childSizes(element, controlled, axis);
                    const float length = controlled && linear->childForceExpand[axis]
                        ? std::max(inner, s.min)
                        : controlled ? std::max(std::min(s.preferred, inner), s.min) : s.preferred;
                    const float position = innerMin + (inner - length) * linear->childAlignment[axis];
                    element.rect.min[axis] = position;
                    element.rect.max[axis] = position + length;
                    element.drivenByLayout = true;
                }
            }
            return;
        }

        const auto& grid = registry.get<UIGridLayout>(group);
        const glm::vec2 pad = paddingAlong(grid.padding, axis);
        const float innerMin = rect.min[axis] + pad.x;
        const float inner = rect.max[axis] - pad.y - innerMin;
        const float innerWidth = rect.size().x - grid.padding.x - grid.padding.z;
        const size_t columns = static_cast<size_t>(gridColumns(grid, innerWidth, children.size()));
        const size_t rows = (children.size() + columns - 1) / columns;
        const size_t cells = axis == 0 ? std::min(columns, children.size()) : rows;
        const float block = static_cast<float>(cells) * grid.cellSize[axis] +
                            static_cast<float>(cells - 1) * grid.spacing[axis];
        const float start = innerMin + std::max(inner - block, 0.0f) * grid.childAlignment[axis];
        for (size_t i = 0; i < children.size(); ++i) {
            const size_t cell = axis == 0 ? i % columns : i / columns;
            auto& element = registry.get<UIRect>(children[i]);
            element.rect.min[axis] = start + static_cast<float>(cell) * (grid.cellSize[axis] + grid.spacing[axis]);
            element.rect.max[axis] = element.rect.min[axis] + grid.cellSize[axis];
            element.drivenByLayout = true;
        }
    }

    // ── Pass 3: clip, visibility and draw order ──

    void finish(entt::entity parent, const Rect& clip, bool visible) {
        forEachChild(parent, [&](entt::entity child, UIRect& element) {
            element.clip = clip;
            element.visibleInHierarchy = visible && element.visible;
            element.canvas = canvas;
            element.drawOrder = nextOrder++;
            const Rect childClip = registry.all_of<UIClip>(child) ? intersect(clip, element.rect) : clip;
            finish(child, childClip, element.visibleInHierarchy);
        });
    }
};

} // namespace

void layoutCanvas(entt::registry& registry, entt::entity canvasEntity, const glm::vec2& screenSize,
                  const FontLibrary* fonts) {
    auto& canvas = registry.get<UICanvas>(canvasEntity);

    canvas.targetSize = canvas.mode == UICanvas::Mode::ScreenOverlay ? screenSize : canvas.pixelSize;
    canvas.scale = canvasScale(canvas, canvas.targetSize);
    canvas.size = glm::max(canvas.targetSize, glm::vec2(0.0f)) / canvas.scale;

    const Rect canvasRect{glm::vec2(0.0f), canvas.size};
    LayoutWalk walk{registry, canvasEntity, fonts};
    for (int axis = 0; axis < 2; ++axis) {
        walk.computeSizes(canvasEntity, axis);
        walk.place(canvasEntity, canvasRect, axis);
    }
    walk.finish(canvasEntity, canvasRect, true);
}

CanvasLayoutSystem::CanvasLayoutSystem(const glm::vec2* screenSize, const FontLibrary* fonts)
    : m_screenSize(screenSize), m_fonts(fonts) {
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
        layoutCanvas(registry, canvas, screenSize, m_fonts);
    }
}

} // namespace UI
} // namespace Shoonyakasha
