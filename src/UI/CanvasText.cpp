//
// CanvasText.cpp - Laying out UIText into UITextCache
//

#include "UI/UISystems.h"
#include "UI/FontLibrary.h"
#include "UI/UIComponents.h"
#include "UI/Utf8.h"

namespace Shoonyakasha {
namespace UI {

namespace {

bool sameStyle(const TextStyle& a, const TextStyle& b) {
    return a.fontSize == b.fontSize && a.alignH == b.alignH && a.alignV == b.alignV &&
           a.wrap == b.wrap && a.lineSpacing == b.lineSpacing;
}

} // namespace

CanvasTextSystem::CanvasTextSystem(const FontLibrary* fonts)
    : m_fonts(fonts) {
    priority = kPriority;
    name = "CanvasTextSystem";
}

void CanvasTextSystem::update(entt::registry& registry, float /*deltaTime*/) {
    if (!enabled || !m_fonts) return;

    for (auto [entity, text, element] : registry.view<UIText, UIRect>().each()) {
        if (element.canvas == entt::null) continue;

        TextStyle style;
        style.fontSize = text.fontSize;
        style.alignH = text.alignH;
        style.alignV = text.alignV;
        style.wrap = text.wrap;
        style.lineSpacing = text.lineSpacing;
        const glm::vec2 box = element.rect.size();

        auto* cache = registry.try_get<UITextCache>(entity);
        if (cache && cache->text == text.text && cache->font == text.font &&
            sameStyle(cache->style, style) && cache->boxSize == box) {
            continue;
        }
        if (!cache) cache = &registry.emplace<UITextCache>(entity);

        cache->text = text.text;
        cache->font = text.font;
        cache->style = style;
        cache->boxSize = box;
        const SdfFont* font = m_fonts->get(text.font);
        cache->layout = font ? layoutText(*font, decodeUtf8(text.text), style, box) : TextLayoutResult{};
    }
}

} // namespace UI
} // namespace Shoonyakasha
