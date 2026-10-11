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
#include "UI/Utf8.h"

#include <algorithm>

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
    case TextHAlign::Start:  return UI::TextAlignH::Start;
    case TextHAlign::End:    return UI::TextAlignH::End;
    default:                 return UI::TextAlignH::Left;
    }
}

UI::TextDirection toDirection(TextDirection direction) {
    switch (direction) {
    case TextDirection::LeftToRight: return UI::TextDirection::LeftToRight;
    case TextDirection::RightToLeft: return UI::TextDirection::RightToLeft;
    default:                         return UI::TextDirection::Auto;
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
        if (const auto* field = get<UI::UITextField>(h)) return registry.try_get<UI::UIText>(field->textElement);
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

void UIAPI::setFontFallbacks(uint32_t font, const std::vector<uint32_t>& fallbacks) {
    m_impl->context.fonts().setFallbacks(font, fallbacks);
}

void UIAPI::setDefaultFontFallbacks(const std::vector<uint32_t>& fallbacks) {
    m_impl->context.fonts().setDefaultFallbacks(fallbacks);
}

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

EntityHandle UIAPI::createWorldCanvas(const glm::vec2& pixelSize, const glm::vec2& worldSize, float emission,
                                      bool lit) {
    const entt::entity e = m_impl->context.createWorldCanvas(m_impl->registry, pixelSize, worldSize, emission, lit);
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

EntityHandle UIAPI::createTextField(EntityHandle parent, const std::string& placeholder, const glm::vec2& size) {
    return toHandle(UI::createTextField(m_impl->registry, m_impl->parentOf(parent), placeholder, getDefaultFont(),
                                        size));
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

// ─── Layout groups ──────────────────────────────────────────────

namespace {

template<typename Layout>
EntityHandle createLinear(entt::registry& registry, entt::entity parent, const glm::vec2& size, float spacing,
                          const glm::vec4& padding) {
    const entt::entity e = UI::createElement(registry, parent, size);
    auto& layout = registry.emplace<Layout>(e);
    layout.spacing = spacing;
    layout.padding = padding;
    return toHandle(e);
}

UI::UIContentSizeFitter::Fit toFit(SizeFit fit) {
    switch (fit) {
    case SizeFit::Min:       return UI::UIContentSizeFitter::Fit::Min;
    case SizeFit::Preferred: return UI::UIContentSizeFitter::Fit::Preferred;
    default:                 return UI::UIContentSizeFitter::Fit::Unconstrained;
    }
}

} // namespace

EntityHandle UIAPI::createHorizontalLayout(EntityHandle parent, const glm::vec2& size, float spacing,
                                           const glm::vec4& padding) {
    return createLinear<UI::UIHorizontalLayout>(m_impl->registry, m_impl->parentOf(parent), size, spacing, padding);
}

EntityHandle UIAPI::createVerticalLayout(EntityHandle parent, const glm::vec2& size, float spacing,
                                         const glm::vec4& padding) {
    return createLinear<UI::UIVerticalLayout>(m_impl->registry, m_impl->parentOf(parent), size, spacing, padding);
}

EntityHandle UIAPI::createGridLayout(EntityHandle parent, const glm::vec2& size, const glm::vec2& cellSize,
                                     int columns, const glm::vec2& spacing, const glm::vec4& padding) {
    const entt::entity e = UI::createElement(m_impl->registry, m_impl->parentOf(parent), size);
    auto& grid = m_impl->registry.emplace<UI::UIGridLayout>(e);
    grid.cellSize = cellSize;
    grid.columns = columns;
    grid.spacing = spacing;
    grid.padding = padding;
    return toHandle(e);
}

void UIAPI::setLayoutAlignment(EntityHandle group, const glm::vec2& alignment) {
    if (auto* h = m_impl->get<UI::UIHorizontalLayout>(group)) h->childAlignment = alignment;
    if (auto* v = m_impl->get<UI::UIVerticalLayout>(group)) v->childAlignment = alignment;
    if (auto* g = m_impl->get<UI::UIGridLayout>(group)) g->childAlignment = alignment;
}

void UIAPI::setLayoutChildSizing(EntityHandle group, bool controlWidth, bool controlHeight,
                                 bool expandWidth, bool expandHeight) {
    UI::UILinearLayout* layout = m_impl->get<UI::UIHorizontalLayout>(group);
    if (!layout) layout = m_impl->get<UI::UIVerticalLayout>(group);
    if (!layout) return;
    layout->controlChildSize = {controlWidth, controlHeight};
    layout->childForceExpand = {expandWidth, expandHeight};
}

void UIAPI::setLayoutElement(EntityHandle element, const glm::vec2& minSize, const glm::vec2& preferredSize,
                             const glm::vec2& flexibleSize, bool ignoreLayout) {
    if (!m_impl->get<UI::UIRect>(element)) return;
    auto& layout = m_impl->registry.emplace_or_replace<UI::UILayoutElement>(toEntt(element));
    layout.minSize = minSize;
    layout.preferredSize = preferredSize;
    layout.flexibleSize = flexibleSize;
    layout.ignoreLayout = ignoreLayout;
}

void UIAPI::setSizeToContent(EntityHandle element, SizeFit horizontal, SizeFit vertical) {
    if (!m_impl->get<UI::UIRect>(element)) return;
    const entt::entity e = toEntt(element);
    if (horizontal == SizeFit::Unconstrained && vertical == SizeFit::Unconstrained) {
        m_impl->registry.remove<UI::UIContentSizeFitter>(e);
        return;
    }
    auto& fitter = m_impl->registry.emplace_or_replace<UI::UIContentSizeFitter>(e);
    fitter.horizontal = toFit(horizontal);
    fitter.vertical = toFit(vertical);
}

glm::vec2 UIAPI::getPreferredSize(EntityHandle element) const {
    const auto* rect = m_impl->get<UI::UIRect>(element);
    return rect ? rect->preferredSize : glm::vec2(0.0f);
}

// ─── Scroll views ───────────────────────────────────────────────

EntityHandle UIAPI::createScrollView(EntityHandle parent, const glm::vec2& size, bool horizontal, bool vertical) {
    return toHandle(UI::createScrollView(m_impl->registry, m_impl->parentOf(parent), size, horizontal, vertical));
}

EntityHandle UIAPI::getScrollContent(EntityHandle view) const {
    const auto* v = m_impl->get<UI::UIScrollView>(view);
    return v && v->content != entt::null ? toHandle(v->content) : NullEntity;
}

glm::vec2 UIAPI::getScrollPosition(EntityHandle view) const {
    const auto* v = m_impl->get<UI::UIScrollView>(view);
    return v ? v->scrollPosition : glm::vec2(0.0f);
}

void UIAPI::setScrollPosition(EntityHandle view, const glm::vec2& position) {
    if (auto* v = m_impl->get<UI::UIScrollView>(view)) {
        v->scrollPosition = glm::clamp(position, glm::vec2(0.0f), glm::vec2(1.0f));
        v->velocity = glm::vec2(0.0f);
    }
}

void UIAPI::scrollTo(EntityHandle view, EntityHandle element) {
    auto* v = m_impl->get<UI::UIScrollView>(view);
    const auto* target = m_impl->get<UI::UIRect>(element);
    const auto* content = v ? m_impl->registry.try_get<UI::UIRect>(v->content) : nullptr;
    if (!target || !content) return;
    const glm::vec2 range = v->range();
    for (int axis = 0; axis < 2; ++axis) {
        if (range[axis] <= 0.0f || !(axis == 0 ? v->horizontal : v->vertical)) continue;
        // Offsets within the content, which the scrolling moves with it.
        const float start = target->rect.min[axis] - content->rect.min[axis];
        const float end = target->rect.max[axis] - content->rect.min[axis];
        float offset = v->scrollPosition[axis] * range[axis];
        if (start < offset) {
            offset = start;
        } else if (end > offset + v->viewportSize[axis]) {
            offset = end - v->viewportSize[axis];
        }
        v->scrollPosition[axis] = std::clamp(offset / range[axis], 0.0f, 1.0f);
        v->velocity[axis] = 0.0f;
    }
}

void UIAPI::setScrollSettings(EntityHandle view, float wheelSpeed, bool inertia) {
    if (auto* v = m_impl->get<UI::UIScrollView>(view)) {
        v->wheelSpeed = wheelSpeed;
        v->inertia = inertia;
    }
}

void UIAPI::setScrollElastic(EntityHandle view, bool elastic, float elasticity) {
    if (auto* v = m_impl->get<UI::UIScrollView>(view)) {
        v->movement = elastic ? UI::UIScrollView::Movement::Elastic : UI::UIScrollView::Movement::Clamped;
        v->elasticity = std::max(elasticity, 0.01f);
    }
}

// ─── Content ────────────────────────────────────────────────────

void UIAPI::setText(EntityHandle element, const std::string& text) {
    if (auto* field = m_impl->get<UI::UITextField>(element)) {
        field->text = text;
        field->caret = field->selectionAnchor = UI::decodeUtf8(text).size();
        return;
    }
    if (auto* t = m_impl->text(element)) t->text = text;
}

std::string UIAPI::getText(EntityHandle element) const {
    if (const auto* field = m_impl->get<UI::UITextField>(element)) return field->text;
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

void UIAPI::setTextDirection(EntityHandle element, TextDirection direction) {
    if (auto* t = m_impl->text(element)) t->direction = toDirection(direction);
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

void UIAPI::addTintTarget(EntityHandle interactable, EntityHandle target) {
    if (!m_impl->valid(interactable) || !m_impl->valid(target)) return;
    auto& targets = m_impl->registry.get_or_emplace<UI::UIInteractable>(toEntt(interactable)).tintTargets;
    if (std::find(targets.begin(), targets.end(), toEntt(target)) == targets.end()) targets.push_back(toEntt(target));
}

void UIAPI::removeTintTarget(EntityHandle interactable, EntityHandle target) {
    auto* i = m_impl->get<UI::UIInteractable>(interactable);
    if (!i) return;
    i->tintTargets.erase(std::remove(i->tintTargets.begin(), i->tintTargets.end(), toEntt(target)),
                         i->tintTargets.end());
}

std::vector<EntityHandle> UIAPI::getTintTargets(EntityHandle interactable) const {
    std::vector<EntityHandle> result;
    if (const auto* i = m_impl->get<UI::UIInteractable>(interactable)) {
        for (const entt::entity e : i->tintTargets) {
            if (m_impl->registry.valid(e)) result.push_back(toHandle(e));
        }
    }
    return result;
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

void UIAPI::focus(EntityHandle element) {
    m_impl->context.focus().focused = m_impl->valid(element) ? toEntt(element) : entt::null;
}

EntityHandle UIAPI::getFocusedElement() const {
    const entt::entity focused = m_impl->context.focus().focused;
    return focused != entt::null && m_impl->registry.valid(focused) ? toHandle(focused) : NullEntity;
}

bool UIAPI::isKeyboardCaptured() const { return m_impl->context.focus().keyboardCaptured; }

void UIAPI::setNavigable(EntityHandle element, bool navigable) {
    if (auto* i = m_impl->get<UI::UIInteractable>(element)) i->navigable = navigable;
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

// ─── Text fields ────────────────────────────────────────────────

bool UIAPI::textSubmitted(EntityHandle field) const {
    const auto* f = m_impl->get<UI::UITextField>(field);
    return f && f->submitted;
}

bool UIAPI::textChanged(EntityHandle field) const {
    const auto* f = m_impl->get<UI::UITextField>(field);
    return f && f->changed;
}

void UIAPI::selectAll(EntityHandle field) {
    if (auto* f = m_impl->get<UI::UITextField>(field)) {
        f->selectionAnchor = 0;
        f->caret = UI::decodeUtf8(f->text).size();
    }
}

void UIAPI::setTextFieldOptions(EntityHandle field, uint32_t maxLength, bool password, bool readOnly) {
    if (auto* f = m_impl->get<UI::UITextField>(field)) {
        f->maxLength = maxLength;
        f->password = password;
        f->readOnly = readOnly;
    }
}

} // namespace Facade
} // namespace Shoonyakasha
