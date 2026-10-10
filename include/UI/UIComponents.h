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

#include "GPU/GPUTypes.h"
#include "UI/TextLayout.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>

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

    // WorldTexture: the canvas renders into a texture of pixelSize, cleared to
    // clearColor (sRGB, straight alpha). The entity's MaterialComponentV5, if
    // it has one, gets the texture in textureSlot.
    glm::vec2 pixelSize{1024.0f, 1024.0f};
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    std::string textureSlot = "emissiveMap";

    // Written by CanvasLayoutSystem.
    glm::vec2 targetSize{0.0f};                // screen or render target, in pixels
    glm::vec2 size{0.0f};                      // in canvas units
    float scale = 1.0f;                        // target pixels per canvas unit

    // Written by the renderer: the WorldTexture canvas's texture, valid from
    // the frame after the canvas first renders. Owned by the renderer.
    GPUTexture target;
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

// Colours are sRGB-encoded with straight alpha, as colour pickers give them.

/// Fills the element's rect with a texture, or with `color` when the texture
/// has no view.
struct UIImage {
    GPUTexture texture;
    glm::vec4 color{1.0f};
    glm::vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};  // x, y, width, height in texture UVs
    bool raycastTarget = true;                  // stops the pointer reaching what is under it
};

/// A 9-slice image: the corners keep their size, the edges stretch along one
/// axis and the centre along both.
struct UIPanel {
    GPUTexture texture;                          // no view: a flat `color`
    glm::vec4 color{1.0f};
    glm::vec4 border{0.0f};                      // left, top, right, bottom, in texture pixels
    float borderScale = 1.0f;                    // canvas units per border texture pixel
    bool fillCenter = true;
    bool raycastTarget = true;                   // stops the pointer reaching what is under it
};

struct UIText {
    std::string text;                            // UTF-8
    uint32_t font = 0;                           // FontLibrary id
    float fontSize = 24.0f;                      // canvas units, ascent to descent
    glm::vec4 color{1.0f};
    TextAlignH alignH = TextAlignH::Left;
    TextAlignV alignV = TextAlignV::Top;
    bool wrap = true;
    float lineSpacing = 1.0f;
};

// ─── Pointer input ───────────────────────────────────────────────
//
// The pointer goes to the topmost element under it that is a raycast target:
// an element with UIInteractable, or with a UIImage or UIPanel whose
// raycastTarget is set. The element's nearest UIInteractable, itself or an
// ancestor, receives it, so a button's label and background act as the
// button. CanvasInputSystem writes the state below once per frame, from the
// rects of the frame before.

/// An element the pointer can hover, press and click.
struct UIInteractable {
    bool enabled = true;

    // Multiplied into the element's own UIPanel and UIImage colours, by state.
    glm::vec4 normalColor{1.0f};
    glm::vec4 hoverColor{0.92f, 0.92f, 0.92f, 1.0f};
    glm::vec4 pressedColor{0.75f, 0.75f, 0.75f, 1.0f};
    glm::vec4 disabledColor{0.6f, 0.6f, 0.6f, 0.5f};

    // Written by CanvasInputSystem.
    bool hovered = false;       // the pointer is over it, or it is pressed and the pointer is over it
    bool pressed = false;       // pressed on it and not yet released
    bool clicked = false;       // this frame: released over it after being pressed on it
    bool valueChanged = false;  // this frame: a UIToggle or UISlider value changed by the pointer

    const glm::vec4& stateColor() const {
        if (!enabled) return disabledColor;
        if (pressed) return pressedColor;
        if (hovered) return hoverColor;
        return normalColor;
    }
};

/// A button: a click is UIInteractable::clicked.
struct UIButton {};

/// A click flips isOn. The checkmark element is visible while it is on.
struct UIToggle {
    bool isOn = false;
    entt::entity checkmark = entt::null;
};

/// A horizontal slider. Pressing or dragging sets the value from the pointer's
/// position across the element's rect. The fill element stretches from the
/// left to the value, and the handle element is centred on it.
struct UISlider {
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.0f;
    bool wholeNumbers = false;
    entt::entity fill = entt::null;
    entt::entity handle = entt::null;

    float normalized() const {
        return maxValue > minValue ? (value - minValue) / (maxValue - minValue) : 0.0f;
    }
};

/// Where the pointer is, for the application. Owned by UIContext and written
/// by CanvasInputSystem.
struct UIPointerState {
    bool overUI = false;                    // over a raycast target, or pressing an element
    entt::entity canvas = entt::null;       // the canvas it is over
    glm::vec2 position{0.0f};               // on that canvas, in canvas units
    entt::entity hovered = entt::null;      // the UIInteractable it is over
    entt::entity pressed = entt::null;      // the UIInteractable it pressed, until released
};

/// The layout of a UIText, and the inputs it was made from. Written by
/// CanvasTextSystem, which lays the text out again when an input changes.
struct UITextCache {
    std::string text;
    uint32_t font = 0;
    TextStyle style;
    glm::vec2 boxSize{0.0f};
    TextLayoutResult layout;
};

} // namespace UI
} // namespace Shoonyakasha
