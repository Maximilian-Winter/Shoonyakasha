# UIAPI

Access: `engine.getUI()`, from the `setOnInit` callback on. [Other language reference](../python/ui.md).

Include `Facade/UIAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/UIAPI.h).

The canvas UI through entity handles: screen canvases drawn over the image, world canvases drawn into a texture on a quad, and their elements and widgets. The [Python reference](../python/ui.md) describes units, layout, input and world canvases. The behaviour is the same in C++, with `glm` vectors in place of tuples and UTF-8 `std::string`s for text.

Below the facade, the canvas UI is plain ECS components (`UI/UIComponents.h`) and systems (`UI/UISystems.h`). `ApplicationBase` registers them and owns a `UI::UIContext` (`getUIContext()`). `UI/UIWidgets.h` builds widgets straight into a registry. A program that derives from `ApplicationBase` can use these directly; [examples/cpp/rendering/canvas_ui](../../../examples/cpp/rendering/canvas_ui/main.cpp) does.

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
UIAPI(ECS::Scene& scene, UI::UIContext& context, Sprite2DManager* textures);
~UIAPI();
uint32_t loadFont(const std::string& path);
uint32_t getDefaultFont();
void setDefaultFont(uint32_t font);
EntityHandle createCanvas(const glm::vec2& referenceSize = glm::vec2(1920.0f, 1080.0f), CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreen, int sortOrder = 0);
EntityHandle createWorldCanvas(const glm::vec2& pixelSize, const glm::vec2& worldSize, float emission = 1.0f);
void setCanvasScaling(EntityHandle canvas, CanvasScaleMode mode, const glm::vec2& referenceSize, float scaleFactor = 1.0f, float match = 0.5f);
void setCanvasSortOrder(EntityHandle canvas, int sortOrder);
void setCanvasPixelSize(EntityHandle canvas, const glm::vec2& pixelSize);
void setCanvasClearColor(EntityHandle canvas, const glm::vec4& color);
glm::vec2 getCanvasSize(EntityHandle canvas) const;
EntityHandle createElement(EntityHandle parent, const glm::vec2& size);
EntityHandle createPanel(EntityHandle parent, const glm::vec2& size, const glm::vec4& color = glm::vec4(1.0f), const std::string& texturePath = "", const glm::vec4& border = glm::vec4(0.0f));
EntityHandle createImage(EntityHandle parent, const glm::vec2& size, const std::string& texturePath = "", const glm::vec4& color = glm::vec4(1.0f));
EntityHandle createText(EntityHandle parent, const std::string& text, float fontSize = 24.0f, const glm::vec4& color = glm::vec4(1.0f), uint32_t font = 0);
EntityHandle createButton(EntityHandle parent, const std::string& label, const glm::vec2& size = glm::vec2(160.0f, 40.0f));
EntityHandle createToggle(EntityHandle parent, const std::string& label, bool isOn = false, const glm::vec2& size = glm::vec2(200.0f, 28.0f));
EntityHandle createSlider(EntityHandle parent, float minValue, float maxValue, float value, const glm::vec2& size = glm::vec2(200.0f, 24.0f));
void setParent(EntityHandle element, EntityHandle parent);
void setRect(EntityHandle element, const glm::vec2& anchorMin, const glm::vec2& anchorMax, const glm::vec2& pivot, const glm::vec2& position, const glm::vec2& size);
void setAnchor(EntityHandle element, const glm::vec2& anchor);
void setPosition(EntityHandle element, const glm::vec2& position);
void setSize(EntityHandle element, const glm::vec2& size);
glm::vec4 getRect(EntityHandle element) const;
void setVisible(EntityHandle element, bool visible);
bool isVisible(EntityHandle element) const;
void setClip(EntityHandle element, bool clip);
void setText(EntityHandle element, const std::string& text);
std::string getText(EntityHandle element) const;
void setFont(EntityHandle element, uint32_t font);
void setFontSize(EntityHandle element, float fontSize);
void setTextAlign(EntityHandle element, TextHAlign horizontal, TextVAlign vertical = TextVAlign::Top);
void setTextWrap(EntityHandle element, bool wrap);
void setColor(EntityHandle element, const glm::vec4& color);
bool setTexture(EntityHandle element, const std::string& path);
void setPanelBorder(EntityHandle element, const glm::vec4& border, float borderScale = 1.0f);
void setRaycastTarget(EntityHandle element, bool target);
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
bool isPointerOverUI() const;
EntityHandle getPointerCanvas() const;
glm::vec2 getPointerPosition() const;
EntityHandle getHoveredElement() const;
```

<!-- END SOURCE API -->
