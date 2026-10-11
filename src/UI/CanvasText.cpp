//
// CanvasText.cpp - Laying out UIText into UITextCache, and placing text fields
//

#include "UI/UISystems.h"
#include "UI/FontLibrary.h"
#include "UI/TextEditing.h"
#include "UI/UIComponents.h"
#include "UI/Utf8.h"

#include "ECS/Core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Shoonyakasha {
namespace UI {

namespace {

bool sameStyle(const TextStyle& a, const TextStyle& b) {
    return a.fontSize == b.fontSize && a.alignH == b.alignH && a.alignV == b.alignV &&
           a.wrap == b.wrap && a.lineSpacing == b.lineSpacing;
}

/// Shows each field's text, mask or placeholder in its text element.
void showTextFields(entt::registry& registry) {
    for (auto [entity, field] : registry.view<UITextField>().each()) {
        const size_t length = decodeUtf8(field.text).size();
        field.caret = std::min(field.caret, length);
        field.selectionAnchor = std::min(field.selectionAnchor, length);
        if (auto* text = registry.try_get<UIText>(field.textElement)) {
            text->text = field.displayText();
            text->color = field.text.empty() ? field.placeholderColor : field.textColor;
        }
    }
}

const UIRect* parentRect(const entt::registry& registry, entt::entity e) {
    const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e);
    return hierarchy ? registry.try_get<UIRect>(hierarchy->parent) : nullptr;
}

/// Puts `e` at canvas x `left`, `width` wide and `height` high, centred
/// vertically in its parent: its rect for this frame, and its anchors for
/// the layout of the next.
void placeBar(entt::registry& registry, entt::entity e, float left, float width, float height,
              const glm::vec4& color, bool visible) {
    auto* rect = registry.try_get<UIRect>(e);
    const UIRect* parent = parentRect(registry, e);
    if (!rect || !parent) return;
    rect->anchorMin = rect->anchorMax = rect->pivot = {0.0f, 0.5f};
    rect->anchoredPosition = {left - parent->rect.min.x, 0.0f};
    rect->sizeDelta = {width, height};
    rect->visible = visible;
    rect->rect.min = {left, parent->rect.min.y + (parent->rect.size().y - height) * 0.5f};
    rect->rect.max = rect->rect.min + glm::vec2(width, height);
    rect->visibleInHierarchy = visible && parent->visibleInHierarchy;
    if (auto* image = registry.try_get<UIImage>(e)) image->color = color;
}

/// Moves each field's text to keep the caret in view, and places its caret
/// and selection.
void placeTextFields(entt::registry& registry, float deltaTime) {
    for (auto [entity, field] : registry.view<UITextField>().each()) {
        auto* textRect = registry.try_get<UIRect>(field.textElement);
        const auto* text = registry.try_get<UIText>(field.textElement);
        if (!textRect || !text || textRect->canvas == entt::null) continue;
        const auto* cache = registry.try_get<UITextCache>(field.textElement);
        const auto* interactable = registry.try_get<UIInteractable>(entity);
        const bool focused = interactable && interactable->focused;
        field.blinkTime += deltaTime;

        const bool laidOut = !field.text.empty() && cache;
        const float caret = laidOut ? caretX(cache->layout, field.caret) : 0.0f;
        const float anchor = laidOut ? caretX(cache->layout, field.selectionAnchor) : 0.0f;
        const float end = laidOut ? caretX(cache->layout, std::numeric_limits<size_t>::max()) : 0.0f;

        // Scrolled no further than needed to show the caret, and no further
        // than the end of the text.
        const float width = textRect->rect.size().x;
        float scroll = field.scroll;
        scroll = std::min(scroll, caret);
        scroll = std::max(scroll, caret + field.caretWidth - width);
        scroll = std::clamp(scroll, 0.0f, std::max(0.0f, end + field.caretWidth - width));
        field.scroll = scroll;

        const float shift = -scroll - textRect->anchoredPosition.x;
        textRect->anchoredPosition.x = -scroll;
        textRect->rect.min.x += shift;
        textRect->rect.max.x += shift;

        const float left = textRect->rect.min.x;
        const float height = text->fontSize;
        const bool selected = focused && field.caret != field.selectionAnchor;
        placeBar(registry, field.selectionElement, left + std::min(caret, anchor), std::abs(caret - anchor), height,
                 field.selectionColor, selected);

        const bool blinkOn = field.blinkPeriod <= 0.0f ||
                             std::fmod(field.blinkTime, field.blinkPeriod) < field.blinkPeriod * 0.5f;
        placeBar(registry, field.caretElement, left + caret, field.caretWidth, height, field.caretColor,
                 focused && blinkOn);
    }
}

} // namespace

CanvasTextSystem::CanvasTextSystem(const FontLibrary* fonts)
    : m_fonts(fonts) {
    priority = kPriority;
    name = "CanvasTextSystem";
}

void CanvasTextSystem::update(entt::registry& registry, float deltaTime) {
    if (!enabled || !m_fonts) return;

    showTextFields(registry);

    for (auto [entity, text, element] : registry.view<UIText, UIRect>().each()) {
        if (element.canvas == entt::null) continue;

        TextStyle style;
        style.fontSize = text.fontSize;
        style.alignH = text.alignH;
        style.alignV = text.alignV;
        style.wrap = text.wrap;
        style.lineSpacing = text.lineSpacing;
        const glm::vec2 box = element.rect.size();

        const std::vector<uint32_t> fonts = m_fonts->chain(text.font);
        auto* cache = registry.try_get<UITextCache>(entity);
        if (cache && cache->text == text.text && cache->fonts == fonts &&
            sameStyle(cache->style, style) && cache->boxSize == box) {
            continue;
        }
        if (!cache) cache = &registry.emplace<UITextCache>(entity);

        cache->text = text.text;
        cache->fonts = fonts;
        cache->style = style;
        cache->boxSize = box;
        cache->layout = fonts.empty() ? TextLayoutResult{}
                                      : layoutText(m_fonts->resolve(fonts), decodeUtf8(text.text), style, box);
    }

    placeTextFields(registry, deltaTime);
}

} // namespace UI
} // namespace Shoonyakasha
