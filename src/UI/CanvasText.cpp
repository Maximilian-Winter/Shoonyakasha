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
#include <optional>

namespace Shoonyakasha {
namespace UI {

namespace {

bool sameStyle(const TextStyle& a, const TextStyle& b) {
    return a.fontSize == b.fontSize && a.alignH == b.alignH && a.alignV == b.alignV &&
           a.wrap == b.wrap && a.lineSpacing == b.lineSpacing && a.direction == b.direction &&
           a.orientation == b.orientation && a.uprightDigits == b.uprightDigits;
}

/// The paragraph direction `text` is laid out with: FromLayout takes its
/// element's layout direction, and Auto falls back to it.
TextDirection resolvedDirection(const UIText& text, const UIRect& element) {
    switch (text.direction) {
    case TextDirection::FromLayout:
        return element.rightToLeft ? TextDirection::RightToLeft : TextDirection::LeftToRight;
    case TextDirection::Auto:
        return element.rightToLeft ? TextDirection::AutoRightToLeft : TextDirection::Auto;
    default:
        return text.direction;
    }
}

struct Span {
    size_t line = 0;
    float left = 0.0f;
    float right = 0.0f;
};

/// The selected stretches of laid-out text: one for each run of selected
/// clusters shown side by side on a line.
std::vector<Span> selectionSpans(const TextLayoutResult& layout, size_t start, size_t end) {
    std::vector<Span> spans;
    for (const TextCluster& c : layout.clusters) {   // line by line, left to right
        if (c.firstCodepoint < start || c.firstCodepoint >= end) continue;
        if (!spans.empty() && spans.back().line == c.line && std::abs(spans.back().right - c.x) < 1e-3f) {
            spans.back().right = c.x + c.width;
        } else {
            spans.push_back({c.line, c.x, c.x + c.width});
        }
    }
    return spans;
}

/// Shows each field's text, mask or placeholder in its text element, wrapped
/// from the top for many lines and centred on one.
void showTextFields(entt::registry& registry) {
    for (auto [entity, field] : registry.view<UITextField>().each()) {
        const size_t length = decodeUtf8(field.text).size();
        field.caret = std::min(field.caret, length);
        field.selectionAnchor = std::min(field.selectionAnchor, length);
        if (auto* text = registry.try_get<UIText>(field.textElement)) {
            text->text = field.displayText();
            text->color = field.text.empty() && field.composition.empty() ? field.placeholderColor : field.textColor;
            text->wrap = field.multiLine;
            text->alignV = field.multiLine ? TextAlignV::Top : TextAlignV::Middle;
            text->richText = false;   // caret indices are into the text as typed,
            text->orientation = TextOrientation::Horizontal;   // and caret positions are on lines across
        }
    }
}

const UIRect* parentRect(const entt::registry& registry, entt::entity e) {
    const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e);
    return hierarchy ? registry.try_get<UIRect>(hierarchy->parent) : nullptr;
}

/// Puts `e` at canvas `min`, `size` big: its rect for this frame, and its
/// anchors for the layout of the next.
void placeBar(entt::registry& registry, entt::entity e, glm::vec2 min, glm::vec2 size,
              const glm::vec4& color, bool visible) {
    auto* rect = registry.try_get<UIRect>(e);
    const UIRect* parent = parentRect(registry, e);
    if (!rect || !parent) return;
    rect->anchorMin = rect->anchorMax = rect->pivot = {0.0f, 0.0f};
    rect->anchoredPosition = min - parent->rect.min;
    rect->sizeDelta = size;
    rect->visible = visible;
    rect->rect.min = min;
    rect->rect.max = min + size;
    rect->visibleInHierarchy = visible && parent->visibleInHierarchy;
    if (auto* image = registry.try_get<UIImage>(e)) image->color = color;
}

/// Moves `rect`, placed by anchors, by `offset` from where its anchors put
/// it: for this frame, and for the layout of the next.
void shiftText(UIRect& rect, glm::vec2 offset) {
    const glm::vec2 shift = offset - rect.anchoredPosition;
    rect.anchoredPosition = offset;
    rect.rect.min += shift;
    rect.rect.max += shift;
}

/// Moves each field's text to keep the caret in view, and places its caret,
/// selection and composition underlines.
void placeTextFields(entt::registry& registry, float deltaTime) {
    for (auto [entity, field] : registry.view<UITextField>().each()) {
        auto* textRect = registry.try_get<UIRect>(field.textElement);
        const auto* text = registry.try_get<UIText>(field.textElement);
        if (!textRect || !text || textRect->canvas == entt::null) continue;
        const auto* cache = registry.try_get<UITextCache>(field.textElement);
        const auto* interactable = registry.try_get<UIInteractable>(entity);
        const bool focused = interactable && interactable->focused;
        field.blinkTime += deltaTime;

        const bool composing = !field.composition.empty();
        const bool laidOut = (!field.text.empty() || composing) && cache;
        const glm::vec2 view = textRect->rect.size();
        // An empty right-to-left field shows its caret at the right.
        const TextDirection direction = resolvedDirection(*text, *textRect);
        const bool emptyRightToLeft = direction == TextDirection::RightToLeft ||
                                      direction == TextDirection::AutoRightToLeft;
        const float emptyCaret = emptyRightToLeft ? view.x - field.caretWidth : 0.0f;
        // While composing, the caret is the input method's, in the composition.
        const size_t compositionStart = field.compositionStart();
        const size_t caretIndex = composing ? compositionStart + field.composition.caret : field.caret;
        const CaretPosition caret = laidOut ? caretPosition(cache->layout, caretIndex) : CaretPosition{emptyCaret, 0};

        // The caret's line, top to bottom, relative to the text's box: on one
        // line the font's height centred in it, on many the laid-out line.
        auto lineBand = [&](size_t line) -> glm::vec2 {
            if (field.multiLine && laidOut && line < cache->layout.lines.size()) {
                return {cache->layout.lines[line].top, cache->layout.lines[line].bottom};
            }
            if (field.multiLine) return {0.0f, text->fontSize};
            const float top = (view.y - text->fontSize) * 0.5f;
            return {top, top + text->fontSize};
        };
        const glm::vec2 caretBand = lineBand(caret.line);

        glm::vec2 offset(0.0f);
        if (field.multiLine) {
            // Moved up no further than needed to show the caret after it
            // moved, and no further than the text reaches.
            field.scroll = 0.0f;
            field.contentHeight = laidOut ? cache->layout.size.y : 0.0f;
            field.viewportHeight = view.y;
            float scrollY = field.scrollY;
            if (field.revealCaret) {
                scrollY = std::min(scrollY, caretBand.x);
                scrollY = std::max(scrollY, caretBand.y - view.y);
                field.revealCaret = false;
            }
            field.scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, field.contentHeight - view.y));
            offset.y = -field.scrollY;
        } else {
            // The text's extent: from 0, or further left where right-aligned
            // text runs past the box's left edge, to its right end.
            float low = 0.0f, high = 0.0f;
            if (laidOut) {
                for (const TextCluster& c : cache->layout.clusters) {
                    low = std::min(low, c.x);
                    high = std::max(high, c.x + c.width);
                }
            }
            // Moved sideways no further than needed to show the caret, and no
            // further than the text reaches.
            float scroll = field.scroll;
            scroll = std::min(scroll, caret.x);
            scroll = std::max(scroll, caret.x + field.caretWidth - view.x);
            field.scroll = std::clamp(scroll, low, std::max(low, high + field.caretWidth - view.x));
            field.scrollY = 0.0f;
            offset.x = -field.scroll;
        }
        shiftText(*textRect, offset);
        const glm::vec2 origin = textRect->rect.min;
        // Many lines run on below the box the text is laid out in. Its rect
        // covers them for this frame, so it is still drawn when scrolled
        // further than a viewport.
        if (field.multiLine) textRect->rect.max.y = std::max(textRect->rect.max.y, origin.y + field.contentHeight);

        // Fills `element` with one rect per part: a span's left and right,
        // and a top and bottom relative to the text's box. Places it around
        // them.
        auto placeRects = [&](entt::entity element, const std::vector<std::pair<Span, glm::vec2>>& parts,
                              const glm::vec4& color) {
            if (element == entt::null) return;
            auto& rects = registry.get_or_emplace<UIImageRects>(element).rects;
            rects.clear();
            Rect bounds{glm::vec2(0.0f), glm::vec2(0.0f)};
            for (const auto& [s, extent] : parts) {
                const Rect r{origin + glm::vec2(s.left, extent.x), origin + glm::vec2(s.right, extent.y)};
                bounds = rects.empty() ? r : Rect{glm::min(bounds.min, r.min), glm::max(bounds.max, r.max)};
                rects.push_back(r);
            }
            placeBar(registry, element, bounds.min, bounds.size(), color, !rects.empty());
        };

        // The selection, hidden while a composition is shown in its place.
        std::vector<std::pair<Span, glm::vec2>> selection;
        if (laidOut && focused && !composing && field.caret != field.selectionAnchor) {
            for (const Span& s : selectionSpans(cache->layout, std::min(field.caret, field.selectionAnchor),
                                                std::max(field.caret, field.selectionAnchor))) {
                selection.push_back({s, lineBand(s.line)});
            }
        }
        placeRects(field.selectionElement, selection, field.selectionColor);

        // The composition underlined at the foot of its line: each clause, or
        // all of it without clauses, the active clause twice as thick.
        std::vector<std::pair<Span, glm::vec2>> underlines;
        if (laidOut && composing) {
            const float thin = std::max(1.0f, text->fontSize * 0.05f);
            auto underline = [&](size_t begin, size_t end, bool active) {
                const float thickness = active ? thin * 2.0f : thin;
                for (Span s : selectionSpans(cache->layout, compositionStart + begin, compositionStart + end)) {
                    // A gap between clauses that touch.
                    s.right = std::max(s.left, s.right - thin);
                    const float bottom = lineBand(s.line).y;
                    underlines.push_back({s, glm::vec2(bottom - thickness, bottom)});
                }
            };
            const auto& clauses = field.composition.clauses;
            if (clauses.empty()) underline(0, field.composition.text.size(), false);
            for (const auto& clause : clauses) underline(clause.begin, clause.end, clause.active);
        }
        placeRects(field.compositionElement, underlines, field.textColor);

        const bool blinkOn = field.blinkPeriod <= 0.0f ||
                             std::fmod(field.blinkTime, field.blinkPeriod) < field.blinkPeriod * 0.5f;
        placeBar(registry, field.caretElement, origin + glm::vec2(caret.x, caretBand.x),
                 {field.caretWidth, caretBand.y - caretBand.x}, field.caretColor, focused && blinkOn);
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
        style.direction = resolvedDirection(text, element);
        style.orientation = text.orientation;
        style.uprightDigits = text.uprightDigits;
        const glm::vec2 box = element.rect.size();

        // Rich text's fonts are known only once its markup is read.
        std::optional<PreparedText> prepared;
        if (text.richText) prepared = prepareText(text.text, true, text.font, text.fontSize, *m_fonts);
        const std::vector<uint32_t> fonts = prepared ? prepared->fonts : m_fonts->chain(text.font);
        auto* cache = registry.try_get<UITextCache>(entity);
        if (cache && cache->text == text.text && cache->richText == text.richText && cache->fonts == fonts &&
            sameStyle(cache->style, style) && cache->boxSize == box) {
            continue;
        }
        if (!cache) cache = &registry.emplace<UITextCache>(entity);
        if (!prepared) prepared = prepareText(text.text, false, text.font, text.fontSize, *m_fonts);

        cache->text = text.text;
        cache->richText = text.richText;
        cache->fonts = fonts;
        cache->style = style;
        cache->boxSize = box;
        cache->layout = fonts.empty() ? TextLayoutResult{}
                                      : layoutText(m_fonts->resolve(fonts), prepared->text, style, box, prepared->spans);
        cache->runs = std::move(prepared->runs);
        cache->links = std::move(prepared->links);
    }

    placeTextFields(registry, deltaTime);
}

} // namespace UI
} // namespace Shoonyakasha
