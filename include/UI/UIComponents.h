//
// UIComponents.h - Canvas UI components
//
// A canvas is an entity with UICanvas. Its UI elements are descendants
// (through HierarchyComponent) that carry UIRect. Child order is draw order:
// later children draw over earlier ones.
//
// Positions are in canvas units with the origin at the canvas's top-left
// corner and y pointing down. One canvas unit is UICanvas::scale target
// pixels.
//

#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>

namespace Shoonyakasha {
namespace UI {

/// Axis-aligned rectangle, y down.
struct Rect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};

    glm::vec2 size() const { return max - min; }
    bool empty() const { return max.x <= min.x || max.y <= min.y; }
    bool contains(const glm::vec2& p) const {
        return p.x >= min.x && p.x < max.x && p.y >= min.y && p.y < max.y;
    }
};

struct UICanvas {
    enum class Mode { ScreenOverlay, WorldTexture };
    enum class ScaleMode { ConstantPixel, ScaleWithScreen };

    Mode mode = Mode::ScreenOverlay;
    int sortOrder = 0;                         // overlay canvases draw in ascending order

    ScaleMode scaleMode = ScaleMode::ConstantPixel;
    float scaleFactor = 1.0f;                  // ConstantPixel: target pixels per canvas unit
    glm::vec2 referenceSize{1920.0f, 1080.0f}; // ScaleWithScreen: canvas size at scale 1
    float match = 0.5f;                        // ScaleWithScreen: 0 follows width, 1 follows height

    glm::vec2 pixelSize{1024.0f, 1024.0f};     // WorldTexture: render target size in pixels

    // Written by CanvasLayoutSystem.
    glm::vec2 targetSize{0.0f};                // screen or render target, in pixels
    glm::vec2 size{0.0f};                      // in canvas units
    float scale = 1.0f;                        // target pixels per canvas unit
};

/// Placement of a UI element inside its parent's rect.
///
/// The anchors are fractions of the parent rect, (0, 0) at its top-left. With
/// equal anchors the element has a fixed size, sizeDelta; with differing
/// anchors it stretches, and sizeDelta is added to the anchored span.
/// anchoredPosition offsets the pivot from its point between the anchors.
struct UIRect {
    glm::vec2 anchorMin{0.5f};
    glm::vec2 anchorMax{0.5f};
    glm::vec2 pivot{0.5f};
    glm::vec2 anchoredPosition{0.0f};
    glm::vec2 sizeDelta{100.0f, 100.0f};
    bool visible = true;

    // Written by CanvasLayoutSystem.
    Rect rect;                                 // in canvas units
    Rect clip;                                 // ancestor clips intersected with the canvas rect
    bool visibleInHierarchy = false;           // this and every ancestor visible, inside a canvas
    entt::entity canvas = entt::null;          // the canvas the element was laid out in
    uint32_t drawOrder = 0;                    // depth-first position within its canvas
};

/// The element's descendants are clipped to its rect.
struct UIClip {};

} // namespace UI
} // namespace Shoonyakasha
