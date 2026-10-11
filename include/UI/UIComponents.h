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
#include "UI/Utf8.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
    bool drivenByLayout = false;               // placed by its parent's layout group, not its anchors
    glm::vec2 minSize{0.0f};                   // its layout sizes, see UILayoutElement
    glm::vec2 preferredSize{0.0f};
    glm::vec2 flexibleSize{0.0f};
};

/// The element's descendants are clipped to its rect.
struct UIClip {};

// ─── Layout ──────────────────────────────────────────────────────
//
// A layout group places its children itself, ignoring their anchors, from
// each child's layout sizes along each axis:
// - min: it never gets less, unless the group has less than the sum of mins;
// - preferred: what it gets when there is room;
// - flexible: its share of the room left once everyone has their preferred size.
// Without a UILayoutElement an element reports its content:
// - text: preferred width unwrapped, min width 0, and as min and preferred
//   height its height when wrapped at its width;
// - an image with a texture: the texture's size as preferred, min 0;
// - anything else: its sizeDelta as both min and preferred.
// A layout group reports its children's sizes added up along its axis and
// the largest across it, with padding and spacing. Content is only measured
// for a group's children and for elements with a UIContentSizeFitter. Hidden
// children, and children with UILayoutElement::ignoreLayout, take no part and
// keep their anchors.

/// Overrides the layout sizes an element reports. -1 keeps its own.
struct UILayoutElement {
    glm::vec2 minSize{-1.0f};
    glm::vec2 preferredSize{-1.0f};
    glm::vec2 flexibleSize{-1.0f};
    bool ignoreLayout = false;
};

/// Settings shared by the horizontal and vertical layout groups.
struct UILinearLayout {
    glm::vec4 padding{0.0f};                  // left, top, right, bottom
    float spacing = 0.0f;                     // between children along the axis
    glm::vec2 childAlignment{0.0f};           // where children sit in spare room: (0, 0) top-left, (0.5, 0.5) centre
    glm::bvec2 controlChildSize{true, true};  // size children from their layout sizes; else they keep their sizeDelta
    glm::bvec2 childForceExpand{false, false};// along the axis: every child flexible; across it: fill the group
};

/// Places its children in a row, left to right.
struct UIHorizontalLayout : UILinearLayout {};

/// Places its children in a column, top to bottom.
struct UIVerticalLayout : UILinearLayout {};

/// Places its children in cells of cellSize, row by row from the top-left.
struct UIGridLayout {
    glm::vec4 padding{0.0f};                  // left, top, right, bottom
    glm::vec2 cellSize{100.0f, 100.0f};
    glm::vec2 spacing{0.0f};
    int columns = 0;                          // 0: as many as fit the width
    glm::vec2 childAlignment{0.0f};           // where the block of cells sits in spare room
};

/// Sizes the element to its own layout sizes, per axis, in place of its
/// sizeDelta. For an element placed by its anchors, not by a layout group.
struct UIContentSizeFitter {
    enum class Fit : uint8_t { Unconstrained, Min, Preferred };
    Fit horizontal = Fit::Unconstrained;
    Fit vertical = Fit::Unconstrained;
};

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
// an element with UIInteractable or UIScrollView, or with a UIImage or
// UIPanel whose raycastTarget is set. The element's nearest UIInteractable, itself or an
// ancestor, receives it, so a button's label and background act as the
// button. CanvasInputSystem writes the state below once per frame, from the
// rects of the frame before.

/// An element the pointer can hover, press and click, and the keyboard or a
/// gamepad can focus and activate.
struct UIInteractable {
    bool enabled = true;
    bool navigable = true;          // Tab, the arrow keys and the D-pad can focus it
    bool capturesKeyboard = false;  // while focused, keys go to it rather than the camera

    // Multiplied into the element's own UIPanel and UIImage colours, and into
    // the UIPanel, UIImage and UIText colours of each tint target, by state.
    glm::vec4 normalColor{1.0f};
    glm::vec4 hoverColor{0.92f, 0.92f, 0.92f, 1.0f};
    glm::vec4 pressedColor{0.75f, 0.75f, 0.75f, 1.0f};
    glm::vec4 disabledColor{0.6f, 0.6f, 0.6f, 0.5f};
    glm::vec4 focusedColor{0.85f, 0.92f, 1.0f, 1.0f};

    // Other elements that take the state colour, such as a button's label.
    std::vector<entt::entity> tintTargets;

    // Written by CanvasInputSystem.
    bool hovered = false;       // the pointer is over it, or it is pressed and the pointer is over it
    bool pressed = false;       // pressed on it and not yet released
    bool focused = false;       // the keyboard and gamepad act on it
    bool clicked = false;       // this frame: released over it after being pressed on it, or activated
    bool valueChanged = false;  // this frame: a UIToggle or UISlider value changed by the user

    const glm::vec4& stateColor() const {
        if (!enabled) return disabledColor;
        if (pressed) return pressedColor;
        if (hovered) return hoverColor;
        if (focused) return focusedColor;
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

/// A single line of editable text. Its elements are made by createTextField:
/// a viewport that clips, holding the selection, the text and the caret,
/// which CanvasTextSystem places each frame after laying out the text. The
/// text is moved sideways to keep the caret in view.
///
/// While focused it takes the keyboard (give its UIInteractable
/// capturesKeyboard): typed characters replace the selection; Backspace and
/// Delete remove a cluster, or with Ctrl a word; Left and Right move by
/// cluster, or with Ctrl by word; Home and End go to either end; Shift with a
/// move extends the selection; Ctrl+A selects all, Ctrl+C, X and V copy, cut
/// and paste through UIClipboard; Enter submits. Pressing places the caret,
/// dragging selects, and a double-click selects a word. There is no input
/// method composition, so text that needs an IME cannot be typed.
struct UITextField {
    std::string text;                        // UTF-8, one line
    std::string placeholder;                 // shown in placeholderColor while text is empty
    size_t caret = 0;                        // code-point index into text
    size_t selectionAnchor = 0;              // the selection runs between it and the caret
    size_t maxLength = 0;                    // in code points; 0: no limit
    bool password = false;                   // drawn as one maskCharacter per code point
    char32_t maskCharacter = 0x2022;         // bullet
    bool readOnly = false;                   // the user can select and copy, not edit

    glm::vec4 textColor{1.0f};
    glm::vec4 placeholderColor{1.0f, 1.0f, 1.0f, 0.4f};
    glm::vec4 selectionColor{0.35f, 0.62f, 0.95f, 0.6f};
    glm::vec4 caretColor{1.0f};
    float caretWidth = 2.0f;                 // canvas units
    float blinkPeriod = 1.0f;                // seconds per on and off; 0: no blink

    entt::entity textElement = entt::null;       // has the UIText
    entt::entity selectionElement = entt::null;  // has a UIImage
    entt::entity caretElement = entt::null;      // has a UIImage

    // Written by CanvasInputSystem, for this frame.
    bool changed = false;                    // the user changed the text
    bool submitted = false;                  // the user pressed Enter
    // Written by CanvasInputSystem and CanvasTextSystem.
    float blinkTime = 0.0f;                  // seconds since the caret last moved
    float scroll = 0.0f;                     // how far the text is moved left

    /// What the text element shows: the text, or its mask, or the
    /// placeholder while the text is empty.
    std::string displayText() const {
        if (text.empty()) return placeholder;
        if (!password) return text;
        return encodeUtf8(std::u32string(decodeUtf8(text).size(), maskCharacter));
    }
};

/// Scrolls its content element inside its own rect, which it clips (give it
/// UIClip). The content is placed by its anchors and then moved by the
/// scroll offset; it is usually a column sized to its children. The view
/// takes the pointer: the wheel scrolls the innermost view under it that can
/// still move, and dragging the content scrolls it once the pointer has moved
/// kDragThreshold canvas units from the press, cancelling the press on a
/// button under it. A slider being dragged keeps the pointer.
///
/// An Elastic view can be dragged, or coast, past its ends, with resistance
/// that grows the farther it goes, and springs back once released. The wheel
/// and the scrollbars never take it past an end.
struct UIScrollView {
    static constexpr float kDragThreshold = 8.0f;

    enum class Movement { Clamped, Elastic };

    entt::entity content = entt::null;
    bool horizontal = false;
    bool vertical = true;
    glm::vec2 scrollPosition{0.0f};      // 0 at the start (left, top), 1 at the end, per axis; past them while an elastic view overshoots
    float wheelSpeed = 48.0f;            // canvas units per wheel step
    bool inertia = true;                 // keep moving after a drag is released
    float decelerationRate = 0.135f;     // velocity kept after one second of inertia
    Movement movement = Movement::Clamped;
    float elasticity = 0.1f;             // seconds an elastic view takes to spring back, roughly
    entt::entity horizontalScrollbar = entt::null;
    entt::entity verticalScrollbar = entt::null;

    // Written by CanvasLayoutSystem.
    glm::vec2 contentSize{0.0f};
    glm::vec2 viewportSize{0.0f};
    // Written by CanvasInputSystem.
    glm::vec2 velocity{0.0f};            // canvas units per second, while coasting

    /// How far the content can move per axis, in canvas units.
    glm::vec2 range() const { return glm::max(contentSize - viewportSize, glm::vec2(0.0f)); }

    /// How far past an end the view is along `axis`, in canvas units; 0
    /// within its range.
    float overshoot(int axis) const {
        const float offset = scrollPosition[axis] * range()[axis];
        if (offset < 0.0f) return -offset;
        return std::max(offset - range()[axis], 0.0f);
    }
};

/// A bar showing a scroll view's position. Its handle's length is the
/// visible fraction; dragging it scrolls, and pressing the track beside it
/// pages towards the pointer. Created by createScrollView; its value follows
/// the view each frame.
struct UIScrollbar {
    bool vertical = true;
    entt::entity scrollView = entt::null;
    entt::entity handle = entt::null;
    bool autoHide = true;                // hidden while the content fits

    // Written by CanvasInputSystem.
    float value = 0.0f;                  // the view's scroll position on this axis
    float size = 1.0f;                   // the visible fraction of the content
};

/// Which element the keyboard and gamepad act on. Owned by UIContext and
/// written by CanvasInputSystem; code may set `focused` too, and an element
/// that cannot take focus (disabled, hidden, not interactable) loses it on
/// the next update.
///
/// Pressing an interactable focuses it, and pressing anywhere else clears
/// focus. Tab and Shift+Tab move through the navigable interactables in draw
/// order; the arrow keys and the D-pad move to the nearest one in that
/// direction. Both stay on the focused element's canvas, or, with nothing
/// focused, on the topmost screen canvas that has a navigable element, so an
/// open dialog keeps navigation to itself. Enter, Space and gamepad A click a
/// focused button or flip a toggle; Left and Right step a focused slider.
/// Escape and gamepad B clear focus.
struct UIFocusState {
    entt::entity focused = entt::null;
    bool keyboardCaptured = false;       // the focused element captures the keyboard
};

/// The clipboard text fields copy to and paste from. Owned by UIContext; the
/// application connects it to the system clipboard. Without `get` and `set`
/// it keeps the text itself.
struct UIClipboard {
    std::function<std::string()> get;
    std::function<void(const std::string&)> set;
    std::string local;

    std::string text() const { return get ? get() : local; }
    void setText(const std::string& text) {
        if (set) set(text);
        else local = text;
    }
};

/// Where the pointer is, for the application. Owned by UIContext and written
/// by CanvasInputSystem.
struct UIPointerState {
    glm::vec2 pixel{0.0f};                  // in screen pixels, top-left origin
    bool overUI = false;                    // over a raycast target, or pressing an element
    entt::entity canvas = entt::null;       // the canvas it is over
    glm::vec2 position{0.0f};               // on that canvas, in canvas units
    entt::entity hovered = entt::null;      // the UIInteractable it is over
    entt::entity pressed = entt::null;      // the UIInteractable it pressed, until released
};

/// The scene's depth under the pointer, read back from the depth buffer of
/// an earlier frame. World-canvas hits farther away than it are hidden by
/// what is in front. Written by the application each frame; `valid` is false
/// when the pipeline names no depth image.
struct UIPointerDepth {
    bool valid = false;
    float depth = 1.0f;                     // the depth buffer's value, 0..1; 1 is nothing drawn
};

/// Measurements of a UIText for layout, and the inputs they were made from.
/// Written by CanvasLayoutSystem for text whose layout sizes are needed.
struct UITextMeasure {
    std::string text;
    std::vector<uint32_t> fonts;          // the font chain, see FontLibrary::chain
    float fontSize = 0.0f;
    float lineSpacing = 0.0f;
    glm::vec2 naturalSize{0.0f};          // unwrapped
    float wrapWidth = -1.0f;              // the width wrappedHeight was measured at
    float wrappedHeight = 0.0f;
};

/// The layout of a UIText, and the inputs it was made from. Written by
/// CanvasTextSystem, which lays the text out again when an input changes.
struct UITextCache {
    std::string text;
    std::vector<uint32_t> fonts;          // the font chain; PositionedGlyph::font indexes it
    TextStyle style;
    glm::vec2 boxSize{0.0f};
    TextLayoutResult layout;
};

} // namespace UI
} // namespace Shoonyakasha
