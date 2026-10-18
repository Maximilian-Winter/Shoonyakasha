//
// Facade/UIAPI.h - The canvas UI for scripting bindings
//
// No Vulkan, no EnTT in this header.
//
// A canvas is an entity; its UI elements are entities below it. Element
// positions and sizes are in canvas units, with the origin at the top-left of
// the parent's rect and y pointing down. Colours are sRGB with straight alpha.
// Destroying an element with SceneAPI::destroyEntity destroys its children.
//
// Pointer state (hovered, pressed, clicked, value changed) is updated once per
// frame, after onUpdate, so onUpdate sees the frame before's: clicked and
// valueChanged are true for one frame.
//

#pragma once

#include "Facade/FacadeTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

class Sprite2DManager;
namespace ECS { class Scene; }
namespace UI { class UIContext; }

namespace Facade {

class UIAPI {
public:
    /// Construct over a scene and its UI context (used by EngineAPI). Textures
    /// load through `textures`; with null, texture paths are ignored.
    UIAPI(ECS::Scene& scene, UI::UIContext& context, Sprite2DManager* textures);

    // Test-only: wrap a raw registry without a Scene, and without textures.
    // Include <entt/entt.hpp> BEFORE this header.
#ifdef SHOONYAKASHA_TESTING
    UIAPI(entt::registry& registry, UI::UIContext& context);
#endif

    ~UIAPI();

    UIAPI(const UIAPI&) = delete;
    UIAPI& operator=(const UIAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Fonts
    // ═══════════════════════════════════════════════════════════

    /// Id of the font at `path` (resolved like other asset paths), loading it
    /// on first use. 0 when it cannot be loaded.
    uint32_t loadFont(const std::string& path);

    /// The font that text uses when it names none. Until set, the first use
    /// loads fonts/Roboto-Regular.ttf.
    uint32_t getDefaultFont();
    void setDefaultFont(uint32_t font);

    /// The fonts, in order, that text in `font` draws code points from when
    /// `font` lacks them: Devanagari or Tibetan inside Latin text, say.
    void setFontFallbacks(uint32_t font, const std::vector<uint32_t>& fallbacks);
    /// Fallbacks for every font that has none of its own.
    void setDefaultFontFallbacks(const std::vector<uint32_t>& fallbacks);

    /// The name rich text's <font=name> finds `font` by. A font is also
    /// named after its file without the extension, such as Roboto-Regular.
    void setFontName(uint32_t font, const std::string& name);
    /// The fonts rich text draws `font` with in bold, in italic and in both;
    /// 0 for none, in which case the text is drawn thicker or slanted.
    void setFontStyles(uint32_t font, uint32_t bold, uint32_t italic = 0, uint32_t boldItalic = 0);

    // ═══════════════════════════════════════════════════════════
    // Canvases
    // ═══════════════════════════════════════════════════════════

    /// A canvas drawn over the screen. Canvases with a higher sortOrder draw
    /// over, and take the pointer before, those with a lower one.
    EntityHandle createCanvas(const glm::vec2& referenceSize = glm::vec2(1920.0f, 1080.0f),
                              CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreen,
                              int sortOrder = 0);

    /// A canvas of `pixelSize` pixels shown on a quad of `worldSize` units in
    /// the scene, facing +Z from the entity's position, laid out in its own
    /// pixels. Unlit, its material emits the canvas at `emission` times its
    /// colour, at a brightness exposure does not change. Lit, the canvas is
    /// the material's base colour, lit by the scene, and `emission` is not
    /// used. Move and turn it with SceneAPI::setPosition and setRotation.
    EntityHandle createWorldCanvas(const glm::vec2& pixelSize, const glm::vec2& worldSize,
                                   float emission = 1.0f, bool lit = false);

    /// ConstantPixel: `scaleFactor` pixels per canvas unit. ScaleWithScreen:
    /// scaled so `referenceSize` fits the screen or texture, `match` 0
    /// following its width, 1 its height and values between a blend.
    void setCanvasScaling(EntityHandle canvas, CanvasScaleMode mode, const glm::vec2& referenceSize,
                          float scaleFactor = 1.0f, float match = 0.5f);
    void setCanvasSortOrder(EntityHandle canvas, int sortOrder);
    /// World canvases: the texture's size, and the colour it is cleared to.
    void setCanvasPixelSize(EntityHandle canvas, const glm::vec2& pixelSize);
    void setCanvasClearColor(EntityHandle canvas, const glm::vec4& color);
    /// The canvas's size in canvas units, as last laid out.
    glm::vec2 getCanvasSize(EntityHandle canvas) const;

    // ═══════════════════════════════════════════════════════════
    // Elements
    //
    // Each is added as the last child of `parent` (a canvas or an element),
    // so it draws over its earlier siblings, centred in it at `size`.
    // ═══════════════════════════════════════════════════════════

    EntityHandle createElement(EntityHandle parent, const glm::vec2& size);

    /// A filled rectangle, or a 9-slice of the texture at `texturePath` with
    /// `border` (left, top, right, bottom texture pixels) kept unstretched.
    EntityHandle createPanel(EntityHandle parent, const glm::vec2& size, const glm::vec4& color = glm::vec4(1.0f),
                             const std::string& texturePath = "", const glm::vec4& border = glm::vec4(0.0f));

    /// The texture at `texturePath` stretched over the rect, times `color`;
    /// without a texture, `color` alone.
    EntityHandle createImage(EntityHandle parent, const glm::vec2& size, const std::string& texturePath = "",
                             const glm::vec4& color = glm::vec4(1.0f));

    /// Text filling its parent; `font` 0 is the default font. `text` is UTF-8.
    EntityHandle createText(EntityHandle parent, const std::string& text, float fontSize = 24.0f,
                            const glm::vec4& color = glm::vec4(1.0f), uint32_t font = 0);

    EntityHandle createButton(EntityHandle parent, const std::string& label,
                              const glm::vec2& size = glm::vec2(160.0f, 40.0f));
    EntityHandle createToggle(EntityHandle parent, const std::string& label, bool isOn = false,
                              const glm::vec2& size = glm::vec2(200.0f, 28.0f));
    EntityHandle createSlider(EntityHandle parent, float minValue, float maxValue, float value,
                              const glm::vec2& size = glm::vec2(200.0f, 24.0f));

    /// A single-line text field showing `placeholder` while empty. Pressing
    /// it focuses it; while focused it takes the keyboard. See Text fields.
    EntityHandle createTextField(EntityHandle parent, const std::string& placeholder = "",
                                 const glm::vec2& size = glm::vec2(240.0f, 36.0f));

    /// Moves `element` to the end of `parent`'s children.
    void setParent(EntityHandle element, EntityHandle parent);

    // ═══════════════════════════════════════════════════════════
    // Layout
    // ═══════════════════════════════════════════════════════════

    /// Anchors are fractions of the parent's rect, (0, 0) its top-left. With
    /// equal anchors `size` is the size; with different ones it is added to
    /// the span between them. `position` offsets the pivot, a fraction of the
    /// element's own rect, from its point between the anchors.
    void setRect(EntityHandle element, const glm::vec2& anchorMin, const glm::vec2& anchorMax,
                 const glm::vec2& pivot, const glm::vec2& position, const glm::vec2& size);

    /// Sets both anchors and the pivot to `anchor`: (0, 0) places the element
    /// by its top-left corner from its parent's top-left, (1, 1) by its
    /// bottom-right from the parent's bottom-right.
    void setAnchor(EntityHandle element, const glm::vec2& anchor);
    void setPosition(EntityHandle element, const glm::vec2& position);
    void setSize(EntityHandle element, const glm::vec2& size);

    /// x, y, width, height in canvas units, as last laid out.
    glm::vec4 getRect(EntityHandle element) const;

    void setVisible(EntityHandle element, bool visible);
    bool isVisible(EntityHandle element) const;

    /// Clips the element's descendants to its rect.
    void setClip(EntityHandle element, bool clip);

    // ═══════════════════════════════════════════════════════════
    // Layout groups
    //
    // A layout group places its children itself, in order, ignoring their
    // anchors. Each child is sized from its layout sizes: its min, its
    // preferred size when there is room, and a share of what is left by its
    // flexible size. Text reports its measured size, an image its texture's,
    // anything else its own size. Hidden children take no space.
    // ═══════════════════════════════════════════════════════════

    /// A row, left to right, or a column, top to bottom, of `size`, with
    /// `spacing` between children and `padding` (left, top, right, bottom)
    /// inside its edges.
    EntityHandle createHorizontalLayout(EntityHandle parent, const glm::vec2& size, float spacing = 0.0f,
                                        const glm::vec4& padding = glm::vec4(0.0f));
    EntityHandle createVerticalLayout(EntityHandle parent, const glm::vec2& size, float spacing = 0.0f,
                                      const glm::vec4& padding = glm::vec4(0.0f));

    /// Cells of `cellSize`, row by row from the top-left; `columns` 0 fits as
    /// many as the width allows.
    EntityHandle createGridLayout(EntityHandle parent, const glm::vec2& size, const glm::vec2& cellSize,
                                  int columns = 0, const glm::vec2& spacing = glm::vec2(0.0f),
                                  const glm::vec4& padding = glm::vec4(0.0f));

    /// Where a group puts its children in room they do not use: (0, 0) the
    /// top-left, (0.5, 0.5) the centre.
    void setLayoutAlignment(EntityHandle group, const glm::vec2& alignment);

    /// For a row or column, per axis: `control` sizes children from their
    /// layout sizes (else they keep their own size); `expand` along the group's
    /// axis gives every child a share of spare room, and across it makes each
    /// child fill the group.
    void setLayoutChildSizing(EntityHandle group, bool controlWidth, bool controlHeight,
                              bool expandWidth, bool expandHeight);

    /// Overrides the layout sizes `element` reports; -1 keeps its own on that
    /// axis. With `ignoreLayout` its group leaves it to its anchors.
    void setLayoutElement(EntityHandle element, const glm::vec2& minSize, const glm::vec2& preferredSize,
                          const glm::vec2& flexibleSize, bool ignoreLayout = false);

    /// Sizes an element placed by its anchors to its own layout sizes, per
    /// axis: a column that grows with its children, a label as wide as its
    /// text.
    void setSizeToContent(EntityHandle element, SizeFit horizontal, SizeFit vertical);

    /// The element's preferred layout size, as last measured. Measured only
    /// for children of a group and elements sized to content; others report
    /// their own size.
    glm::vec2 getPreferredSize(EntityHandle element) const;

    // ═══════════════════════════════════════════════════════════
    // Scroll views
    //
    // A scroll view clips a column and scrolls it: by the wheel over it, by
    // dragging the column (a press on a button inside that turns into a drag
    // is not a click), and by its scrollbars, which hide while everything
    // fits. Positions run from 0 at the start (top, left) to 1 at the end.
    // ═══════════════════════════════════════════════════════════

    /// A scroll view of `size`. Add children to getScrollContent(view): they
    /// stack top to bottom, spanning the view's width when it scrolls only
    /// vertically.
    EntityHandle createScrollView(EntityHandle parent, const glm::vec2& size, bool horizontal = false,
                                  bool vertical = true);

    /// The column inside the view that holds its children.
    EntityHandle getScrollContent(EntityHandle view) const;

    glm::vec2 getScrollPosition(EntityHandle view) const;
    /// Moves the view, stopping any drift from a released drag.
    void setScrollPosition(EntityHandle view, const glm::vec2& position);

    /// Scrolls the least distance that brings `element`, inside the view's
    /// content, fully into view, as last laid out.
    void scrollTo(EntityHandle view, EntityHandle element);

    /// Canvas units per wheel step, and whether a released drag keeps moving.
    void setScrollSettings(EntityHandle view, float wheelSpeed, bool inertia = true);

    /// Whether dragging, and coasting after a drag, can take the view past
    /// its ends, with growing resistance, before it springs back in about
    /// `elasticity` seconds. Off for a new view. While past an end,
    /// getScrollPosition is below 0 or above 1.
    void setScrollElastic(EntityHandle view, bool elastic, float elasticity = 0.1f);

    // ═══════════════════════════════════════════════════════════
    // Content
    //
    // The text functions act on the element's own text or, for a button or
    // toggle, on its label. On a text field, setText and getText act on what
    // the user edits, and the font functions on how it is drawn.
    // ═══════════════════════════════════════════════════════════

    void setText(EntityHandle element, const std::string& text);
    std::string getText(EntityHandle element) const;
    void setFont(EntityHandle element, uint32_t font);
    void setFontSize(EntityHandle element, float fontSize);
    void setTextAlign(EntityHandle element, TextHAlign horizontal, TextVAlign vertical = TextVAlign::Top);
    /// The direction of each paragraph of the text: Auto, the default, takes
    /// it from the paragraph's first letter with a direction, or from the
    /// element's layout direction without one; FromLayout always from the
    /// layout direction. It sets where Start and End align, and the order of
    /// words of the two directions.
    void setTextDirection(EntityHandle element, TextDirection direction);
    void setTextWrap(EntityHandle element, bool wrap);

    /// Lines across, or columns down: Chinese, Japanese and Korean stand
    /// upright with their vertical forms, other characters (Latin, digits,
    /// Mongolian) are turned clockwise, and columns wrap at the rect's
    /// height. The vertical alignment places each column's text along it,
    /// and the horizontal alignment the columns, Start being the side they
    /// start from. With `uprightDigits`, runs of one or two digits stand
    /// upright side by side. Links, underlines and strikethroughs are drawn
    /// only in horizontal text, and text fields are always horizontal.
    void setTextOrientation(EntityHandle element, TextOrientation orientation, bool uprightDigits = false);

    /// What screen readers are told of the element (on Windows, through UI
    /// Automation). `name` replaces the name it has without it (a button's
    /// or toggle's label, a text's own text, a field's placeholder; sliders
    /// and scroll views have none), and is read out with `description`. An
    /// element given a name becomes a group if it has no role of its own.
    /// `hidden` leaves it and its descendants out, for decoration. `role`
    /// replaces the role its components give it. World canvases are not
    /// described.
    void setAccessible(EntityHandle element, const std::string& name, const std::string& description = "",
                       bool hidden = false, AccessibleRole role = AccessibleRole::Default);

    /// Reads the text as markup: `<color=#RRGGBB>` or `<color=#RRGGBBAA>`, `<b>`,
    /// `<i>`, `<size=N>` in canvas units, `<font=name>`, `<link=id>`, `<u>` and `<s>`,
    /// each closed by `</name>`, with `&lt;` and `&gt;` for the brackets. A
    /// tag that is not one of these is shown as text.
    void setRichText(EntityHandle element, bool richText = true);
    /// The colour of a link in rich text while the pointer is over it.
    void setLinkColor(EntityHandle element, const glm::vec4& color);
    /// The id of the link in the text the user clicked this frame, or of
    /// the link under the pointer; empty for none.
    std::string clickedLink(EntityHandle element) const;
    std::string hoveredLink(EntityHandle element) const;

    /// Which way a canvas or an element lays out its children: right to left
    /// mirrors their placement, the order of layout groups, sliders, toggles
    /// and scroll views. Elements inherit it; a canvas lays out left to right
    /// unless set.
    void setLayoutDirection(EntityHandle element, LayoutDirection direction);
    /// Whether the element lays out right to left, as last laid out, or for a
    /// canvas, as set.
    bool isRightToLeft(EntityHandle element) const;
    /// Flips the element's image left for right where it lays out right to
    /// left, for an arrow or other directional icon.
    void setMirrorInRightToLeft(EntityHandle element, bool mirror);

    /// The colour of the element's own text, image and panel.
    void setColor(EntityHandle element, const glm::vec4& color);
    /// Replace solid triangles. Invalid data throws without changing old geometry.
    void setGeometry(EntityHandle element, const std::vector<glm::vec2>& positions,
                     const std::vector<uint32_t>& indices, const std::vector<glm::vec4>& colors);
    /// Parts: surface, text, track, fill, handle. An absent part is ignored.
    void setWidgetPartColor(EntityHandle element, const std::string& part, const glm::vec4& color);
    void setInteractionColors(EntityHandle element, const glm::vec4& normal, const glm::vec4& hover,
                              const glm::vec4& pressed, const glm::vec4& focused, const glm::vec4& disabled);

    /// The texture of the element's image or panel. False when it cannot be
    /// loaded.
    bool setTexture(EntityHandle element, const std::string& path);
    void setPanelBorder(EntityHandle element, const glm::vec4& border, float borderScale = 1.0f);

    /// Whether the element's image or panel stops the pointer.
    void setRaycastTarget(EntityHandle element, bool target);

    // ═══════════════════════════════════════════════════════════
    // Interaction
    // ═══════════════════════════════════════════════════════════

    /// Makes the element take the pointer, or stops it (disabled). Widgets
    /// are interactable from creation.
    void setInteractable(EntityHandle element, bool enabled);

    /// `target`'s panel, image and text colours take the state colour of
    /// `interactable` (hover, pressed, focused, disabled), which is made
    /// interactable if it is not. Buttons, toggles and sliders list their
    /// label and parts from creation.
    void addTintTarget(EntityHandle interactable, EntityHandle target);
    void removeTintTarget(EntityHandle interactable, EntityHandle target);
    std::vector<EntityHandle> getTintTargets(EntityHandle interactable) const;

    bool isHovered(EntityHandle element) const;
    bool isPressed(EntityHandle element) const;
    bool wasClicked(EntityHandle element) const;
    bool valueChanged(EntityHandle element) const;

    bool getToggle(EntityHandle toggle) const;
    void setToggle(EntityHandle toggle, bool isOn);

    float getSliderValue(EntityHandle slider) const;
    void setSliderValue(EntityHandle slider, float value);
    void setSliderRange(EntityHandle slider, float minValue, float maxValue, bool wholeNumbers = false);

    /// Gives `element` keyboard and gamepad focus; NullEntity clears it. An
    /// element that cannot take focus (disabled, hidden, not interactable)
    /// loses it on the next frame. Pressing an element focuses it too, and
    /// Tab, the arrow keys and the D-pad move focus; Enter, Space or gamepad
    /// A activate it, Escape or gamepad B clear it.
    void focus(EntityHandle element);
    EntityHandle getFocusedElement() const;

    /// Does the focused element take the keyboard (a text field)? Then the
    /// camera sees no keys and onKeyPressed is not called.
    bool isKeyboardCaptured() const;

    /// Whether Tab, the arrow keys and the D-pad can reach `element`.
    void setNavigable(EntityHandle element, bool navigable);

    /// Is the pointer over a UI element, or pressing one? Check it before
    /// treating a click as one in the scene.
    bool isPointerOverUI() const;
    /// The canvas under the pointer, or NullEntity.
    EntityHandle getPointerCanvas() const;
    /// The pointer on getPointerCanvas(), in canvas units.
    glm::vec2 getPointerPosition() const;
    /// The interactable element under the pointer, or NullEntity.
    EntityHandle getHoveredElement() const;

    // ═══════════════════════════════════════════════════════════
    // Text fields
    //
    // A focused field takes typed text, Backspace and Delete (with Ctrl, a
    // word), Left and Right (with Ctrl, by word), Home and End, Shift to
    // select, Ctrl+A, C, X and V with the system clipboard, and Enter to
    // submit. Pressing places the caret, dragging selects and a double-click
    // selects a word. setText puts the caret at the end. There is no input
    // method (IME) composition.
    // ═══════════════════════════════════════════════════════════

    /// True for one frame after the user pressed Enter in the field.
    bool textSubmitted(EntityHandle field) const;
    /// True for one frame after the user changed the field's text.
    bool textChanged(EntityHandle field) const;
    void selectAll(EntityHandle field);

    /// `maxLength` in characters, 0 for no limit; `password` draws a dot
    /// per character and stops copying; `readOnly` keeps selecting and
    /// copying but not editing.
    void setTextFieldOptions(EntityHandle field, uint32_t maxLength = 0, bool password = false,
                             bool readOnly = false);

    /// Lets the field hold many lines: its text wraps at its width and
    /// scrolls up and down, Up and Down move between lines, and Enter starts
    /// a new line while Ctrl+Enter submits. With `submitOnEnter`, Enter
    /// submits and Shift+Enter starts a new line. False goes back to one
    /// line; line breaks already in the text stay.
    void setTextFieldMultiLine(EntityHandle field, bool multiLine = true, bool submitOnEnter = false);

    /// What an input method is composing in the field, as UTF-8: shown in
    /// the field but not yet part of its text. Empty when nothing is being
    /// composed there.
    std::string getTextFieldComposition(EntityHandle field) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
