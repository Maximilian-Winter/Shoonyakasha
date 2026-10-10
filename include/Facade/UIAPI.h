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
    /// pixels. Its material emits the canvas at `emission` times its colour.
    /// Move and turn it with SceneAPI::setPosition and setRotation.
    EntityHandle createWorldCanvas(const glm::vec2& pixelSize, const glm::vec2& worldSize,
                                   float emission = 1.0f);

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
    // Content
    //
    // The text functions act on the element's own text or, for a button or
    // toggle, on its label.
    // ═══════════════════════════════════════════════════════════

    void setText(EntityHandle element, const std::string& text);
    std::string getText(EntityHandle element) const;
    void setFont(EntityHandle element, uint32_t font);
    void setFontSize(EntityHandle element, float fontSize);
    void setTextAlign(EntityHandle element, TextHAlign horizontal, TextVAlign vertical = TextVAlign::Top);
    void setTextWrap(EntityHandle element, bool wrap);

    /// The colour of the element's own text, image and panel.
    void setColor(EntityHandle element, const glm::vec4& color);

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

    bool isHovered(EntityHandle element) const;
    bool isPressed(EntityHandle element) const;
    bool wasClicked(EntityHandle element) const;
    bool valueChanged(EntityHandle element) const;

    bool getToggle(EntityHandle toggle) const;
    void setToggle(EntityHandle toggle, bool isOn);

    float getSliderValue(EntityHandle slider) const;
    void setSliderValue(EntityHandle slider, float value);
    void setSliderRange(EntityHandle slider, float minValue, float maxValue, bool wholeNumbers = false);

    /// Is the pointer over a UI element, or pressing one? Check it before
    /// treating a click as one in the scene.
    bool isPointerOverUI() const;
    /// The canvas under the pointer, or NullEntity.
    EntityHandle getPointerCanvas() const;
    /// The pointer on getPointerCanvas(), in canvas units.
    glm::vec2 getPointerPosition() const;
    /// The interactable element under the pointer, or NullEntity.
    EntityHandle getHoveredElement() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
