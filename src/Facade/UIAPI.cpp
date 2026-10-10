//
// Facade/UIAPI.cpp - The canvas UI for scripting bindings
//

#include <entt/entt.hpp>

#include "Facade/UIAPI.h"
#include "FacadeInternal.h"

#include "Core/AssetPaths.h"
#include "ECS/Core.h"
#include "ECS/Scene.h"
#include "Resources/Sprite2DManager.h"
#include "UI/UIComponents.h"
#include "UI/UIContext.h"
#include "UI/UIWidgets.h"

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

namespace {

UI::UICanvas::ScaleMode toScaleMode(CanvasScaleMode mode) {
    return mode == CanvasScaleMode::ConstantPixel ? UI::UICanvas::ScaleMode::ConstantPixel
                                                  : UI::UICanvas::ScaleMode::ScaleWithScreen;
}

UI::TextAlignH toAlign(TextHAlign align) {
    switch (align) {
    case TextHAlign::Center: return UI::TextAlignH::Center;
    case TextHAlign::Right:  return UI::TextAlignH::Right;
    default:                 return UI::TextAlignH::Left;
    }
}

UI::TextAlignV toAlign(TextVAlign align) {
    switch (align) {
    case TextVAlign::Middle: return UI::TextAlignV::Middle;
    case TextVAlign::Bottom: return UI::TextAlignV::Bottom;
    default:                 return UI::TextAlignV::Top;
    }
}

} // namespace

struct UIAPI::Impl {
    entt::registry& registry;
    UI::UIContext& context;
    Sprite2DManager* textures;
    uint32_t defaultFont = 0;

    Impl(entt::registry& r, UI::UIContext& c, Sprite2DManager* t) : registry(r), context(c), textures(t) {}

    bool valid(EntityHandle h) const { return h != NullEntity && registry.valid(toEntt(h)); }

    template<typename T>
    T* get(EntityHandle h) { return valid(h) ? registry.try_get<T>(toEntt(h)) : nullptr; }
    template<typename T>
    const T* get(EntityHandle h) const { return valid(h) ? registry.try_get<T>(toEntt(h)) : nullptr; }

    /// The element's own text, or its first child's: a button's or toggle's label.
    UI::UIText* text(EntityHandle h) {
        if (auto* own = get<UI::UIText>(h)) return own;
        if (const auto* hierarchy = get<ECS::HierarchyComponent>(h)) {
            for (const entt::entity child : hierarchy->children) {
                if (auto* label = registry.try_get<UI::UIText>(child)) return label;
            }
        }
        return nullptr;
    }
    const UI::UIText* text(EntityHandle h) const { return const_cast<Impl*>(this)->text(h); }

    GPUTexture load(const std::string& path) {
        if (!textures || path.empty()) return GPUTexture{};
        return textures->loadTexture(AssetPaths::locate(path).string());
    }

    entt::entity parentOf(EntityHandle parent) const { return valid(parent) ? toEntt(parent) : entt::null; }
};

UIAPI::UIAPI(ECS::Scene& scene, UI::UIContext& context, Sprite2DManager* textures)
    : m_impl(std::make_unique<Impl>(scene.getRegistry(), context, textures)) {}

#ifdef SHOONYAKASHA_TESTING
UIAPI::UIAPI(entt::registry& registry, UI::UIContext& context)
    : m_impl(std::make_unique<Impl>(registry, context, nullptr)) {}
#endif

UIAPI::~UIAPI() = default;

// ─── Fonts ──────────────────────────────────────────────────────

uint32_t UIAPI::loadFont(const std::string& path) {
    return m_impl->context.fonts().load(AssetPaths::locate(path).string());
}

uint32_t UIAPI::getDefaultFont() {
    if (m_impl->defaultFont == 0) m_impl->defaultFont = loadFont("fonts/Roboto-Regular.ttf");
    return m_impl->defaultFont;
}

void UIAPI::setDefaultFont(uint32_t font) { m_impl->defaultFont = font; }

// ─── Canvases ───────────────────────────────────────────────────

EntityHandle UIAPI::createCanvas(const glm::vec2& referenceSize, CanvasScaleMode scaleMode, int sortOrder) {
    const entt::entity e = m_impl->registry.create();
    UI::UICanvas canvas;
    canvas.referenceSize = referenceSize;
    canvas.scaleMode = toScaleMode(scaleMode);
    canvas.sortOrder = sortOrder;
    m_impl->registry.emplace<UI::UICanvas>(e, canvas);
    return toHandle(e);
}

EntityHandle UIAPI::createWorldCanvas(const glm::vec2& pixelSize, const glm::vec2& worldSize, float emission) {
    const entt::entity e = m_impl->context.createWorldCanvas(m_impl->registry, pixelSize, worldSize, emission);
    auto& canvas = m_impl->registry.get<UI::UICanvas>(e);
    canvas.scaleMode = UI::UICanvas::ScaleMode::ScaleWithScreen;
    canvas.referenceSize = pixelSize;
    return toHandle(e);
}

void UIAPI::setCanvasScaling(EntityHandle canvas, CanvasScaleMode mode, const glm::vec2& referenceSize,
                             float scaleFactor, float match) {
    if (auto* c = m_impl->get<UI::UICanvas>(canvas)) {
        c->scaleMode = toScaleMode(mode);
        c->referenceSize = referenceSize;
        c->scaleFactor = scaleFactor;
        c->match = match;
    }
}

void UIAPI::setCanvasSortOrder(EntityHandle canvas, int sortOrder) {
    if (auto* c = m_impl->get<UI::UICanvas>(canvas)) c->sortOrder = sortOrder;
}

void UIAPI::setCanvasPixelSize(EntityHandle canvas, const glm::vec2& pixelSize) {
    if (auto* c = m_impl->get<UI::UICanvas>(canvas)) c->pixelSize = pixelSize;
}

void UIAPI::setCanvasClearColor(EntityHandle canvas, const glm::vec4& color) {
    if (auto* c = m_impl->get<UI::UICanvas>(canvas)) c->clearColor = color;
}

glm::vec2 UIAPI::getCanvasSize(EntityHandle canvas) const {
    const auto* c = m_impl->get<UI::UICanvas>(canvas);
    return c ? c->size : glm::vec2(0.0f);
}

// ─── Elements ───────────────────────────────────────────────────

EntityHandle UIAPI::createElement(EntityHandle parent, const glm::vec2& size) {
    return toHandle(UI::createElement(m_impl->registry, m_impl->parentOf(parent), size));
}

EntityHandle UIAPI::createPanel(EntityHandle parent, const glm::vec2& size, const glm::vec4& color,
                                const std::string& texturePath, const glm::vec4& border) {
    const entt::entity e = UI::createElement(m_impl->registry, m_impl->parentOf(parent), size);
    auto& panel = m_impl->registry.emplace<UI::UIPanel>(e);
    panel.color = color;
    panel.texture = m_impl->load(texturePath);
    panel.border = border;
    return toHandle(e);
}

EntityHandle UIAPI::createImage(EntityHandle parent, const glm::vec2& size, const std::string& texturePath,
                                const glm::vec4& color) {
    const entt::entity e = UI::createElement(m_impl->registry, m_impl->parentOf(parent), size);
    auto& image = m_impl->registry.emplace<UI::UIImage>(e);
    image.color = color;
    image.texture = m_impl->load(texturePath);
    return toHandle(e);
}

EntityHandle UIAPI::createText(EntityHandle parent, const std::string& text, float fontSize,
                               const glm::vec4& color, uint32_t font) {
    const entt::entity e = UI::createElement(m_impl->registry, m_impl->parentOf(parent), glm::vec2(0.0f));
    auto& rect = m_impl->registry.get<UI::UIRect>(e);
    rect.anchorMin = {0.0f, 0.0f};
    rect.anchorMax = {1.0f, 1.0f};
    auto& label = m_impl->registry.emplace<UI::UIText>(e);
    label.text = text;
    label.fontSize = fontSize;
    label.color = color;
    label.font = font != 0 ? font : getDefaultFont();
    return toHandle(e);
}

EntityHandle UIAPI::createButton(EntityHandle parent, const std::string& label, const glm::vec2& size) {
    return toHandle(UI::createButton(m_impl->registry, m_impl->parentOf(parent), label, getDefaultFont(), size));
}

EntityHandle UIAPI::createToggle(EntityHandle parent, const std::string& label, bool isOn, const glm::vec2& size) {
    return toHandle(UI::createToggle(m_impl->registry, m_impl->parentOf(parent), label, getDefaultFont(), isOn, size));
}

EntityHandle UIAPI::createSlider(EntityHandle parent, float minValue, float maxValue, float value,
                                 const glm::vec2& size) {
    return toHandle(UI::createSlider(m_impl->registry, m_impl->parentOf(parent), minValue, maxValue, value, size));
}

void UIAPI::setParent(EntityHandle element, EntityHandle parent) {
    if (!m_impl->valid(element)) return;
    UI::setParent(m_impl->registry, toEntt(element), m_impl->parentOf(parent));
}

// ─── Layout ─────────────────────────────────────────────────────

void UIAPI::setRect(EntityHandle element, const glm::vec2& anchorMin, const glm::vec2& anchorMax,
                    const glm::vec2& pivot, const glm::vec2& position, const glm::vec2& size) {
    if (auto* rect = m_impl->get<UI::UIRect>(element)) {
        rect->anchorMin = anchorMin;
        rect->anchorMax = anchorMax;
        rect->pivot = pivot;
        rect->anchoredPosition = position;
        rect->sizeDelta = size;
    }
}

void UIAPI::setAnchor(EntityHandle element, const glm::vec2& anchor) {
    if (auto* rect = m_impl->get<UI::UIRect>(element)) rect->anchorMin = rect->anchorMax = rect->pivot = anchor;
}

void UIAPI::setPosition(EntityHandle element, const glm::vec2& position) {
    if (auto* rect = m_impl->get<UI::UIRect>(element)) rect->anchoredPosition = position;
}

void UIAPI::setSize(EntityHandle element, const glm::vec2& size) {
    if (auto* rect = m_impl->get<UI::UIRect>(element)) rect->sizeDelta = size;
}

glm::vec4 UIAPI::getRect(EntityHandle element) const {
    const auto* rect = m_impl->get<UI::UIRect>(element);
    if (!rect) return glm::vec4(0.0f);
    return {rect->rect.min, rect->rect.size()};
}

void UIAPI::setVisible(EntityHandle element, bool visible) {
    if (auto* rect = m_impl->get<UI::UIRect>(element)) rect->visible = visible;
}

bool UIAPI::isVisible(EntityHandle element) const {
    const auto* rect = m_impl->get<UI::UIRect>(element);
    return rect && rect->visible;
}

void UIAPI::setClip(EntityHandle element, bool clip) {
    if (!m_impl->valid(element)) return;
    if (clip) {
        m_impl->registry.emplace_or_replace<UI::UIClip>(toEntt(element));
    } else {
        m_impl->registry.remove<UI::UIClip>(toEntt(element));
    }
}

// ─── Content ────────────────────────────────────────────────────

void UIAPI::setText(EntityHandle element, const std::string& text) {
    if (auto* t = m_impl->text(element)) t->text = text;
}

std::string UIAPI::getText(EntityHandle element) const {
    const auto* t = m_impl->text(element);
    return t ? t->text : std::string();
}

void UIAPI::setFont(EntityHandle element, uint32_t font) {
    if (auto* t = m_impl->text(element)) t->font = font;
}

void UIAPI::setFontSize(EntityHandle element, float fontSize) {
    if (auto* t = m_impl->text(element)) t->fontSize = fontSize;
}

void UIAPI::setTextAlign(EntityHandle element, TextHAlign horizontal, TextVAlign vertical) {
    if (auto* t = m_impl->text(element)) {
        t->alignH = toAlign(horizontal);
        t->alignV = toAlign(vertical);
    }
}

void UIAPI::setTextWrap(EntityHandle element, bool wrap) {
    if (auto* t = m_impl->text(element)) t->wrap = wrap;
}

void UIAPI::setColor(EntityHandle element, const glm::vec4& color) {
    if (auto* t = m_impl->get<UI::UIText>(element)) t->color = color;
    if (auto* image = m_impl->get<UI::UIImage>(element)) image->color = color;
    if (auto* panel = m_impl->get<UI::UIPanel>(element)) panel->color = color;
}

bool UIAPI::setTexture(EntityHandle element, const std::string& path) {
    const GPUTexture texture = m_impl->load(path);
    if (texture.view == VK_NULL_HANDLE) return false;
    bool set = false;
    if (auto* image = m_impl->get<UI::UIImage>(element)) { image->texture = texture; set = true; }
    if (auto* panel = m_impl->get<UI::UIPanel>(element)) { panel->texture = texture; set = true; }
    return set;
}

void UIAPI::setPanelBorder(EntityHandle element, const glm::vec4& border, float borderScale) {
    if (auto* panel = m_impl->get<UI::UIPanel>(element)) {
        panel->border = border;
        panel->borderScale = borderScale;
    }
}

void UIAPI::setRaycastTarget(EntityHandle element, bool target) {
    if (auto* image = m_impl->get<UI::UIImage>(element)) image->raycastTarget = target;
    if (auto* panel = m_impl->get<UI::UIPanel>(element)) panel->raycastTarget = target;
}

// ─── Interaction ────────────────────────────────────────────────

void UIAPI::setInteractable(EntityHandle element, bool enabled) {
    if (!m_impl->valid(element)) return;
    m_impl->registry.get_or_emplace<UI::UIInteractable>(toEntt(element)).enabled = enabled;
}

bool UIAPI::isHovered(EntityHandle element) const {
    const auto* i = m_impl->get<UI::UIInteractable>(element);
    return i && i->hovered;
}

bool UIAPI::isPressed(EntityHandle element) const {
    const auto* i = m_impl->get<UI::UIInteractable>(element);
    return i && i->pressed;
}

bool UIAPI::wasClicked(EntityHandle element) const {
    const auto* i = m_impl->get<UI::UIInteractable>(element);
    return i && i->clicked;
}

bool UIAPI::valueChanged(EntityHandle element) const {
    const auto* i = m_impl->get<UI::UIInteractable>(element);
    return i && i->valueChanged;
}

bool UIAPI::getToggle(EntityHandle toggle) const {
    const auto* t = m_impl->get<UI::UIToggle>(toggle);
    return t && t->isOn;
}

void UIAPI::setToggle(EntityHandle toggle, bool isOn) {
    if (auto* t = m_impl->get<UI::UIToggle>(toggle)) t->isOn = isOn;
}

float UIAPI::getSliderValue(EntityHandle slider) const {
    const auto* s = m_impl->get<UI::UISlider>(slider);
    return s ? s->value : 0.0f;
}

void UIAPI::setSliderValue(EntityHandle slider, float value) {
    if (auto* s = m_impl->get<UI::UISlider>(slider)) s->value = value;
}

void UIAPI::setSliderRange(EntityHandle slider, float minValue, float maxValue, bool wholeNumbers) {
    if (auto* s = m_impl->get<UI::UISlider>(slider)) {
        s->minValue = minValue;
        s->maxValue = maxValue;
        s->wholeNumbers = wholeNumbers;
    }
}

bool UIAPI::isPointerOverUI() const { return m_impl->context.pointer().overUI; }

EntityHandle UIAPI::getPointerCanvas() const {
    const entt::entity canvas = m_impl->context.pointer().canvas;
    return canvas == entt::null ? NullEntity : toHandle(canvas);
}

glm::vec2 UIAPI::getPointerPosition() const { return m_impl->context.pointer().position; }

EntityHandle UIAPI::getHoveredElement() const {
    const entt::entity hovered = m_impl->context.pointer().hovered;
    return hovered == entt::null ? NullEntity : toHandle(hovered);
}

} // namespace Facade
} // namespace Shoonyakasha
