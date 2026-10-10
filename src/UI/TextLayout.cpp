//
// TextLayout.cpp - Breaking text into lines and placing glyph quads
//

#include "UI/TextLayout.h"

#include <algorithm>
#include <utility>

namespace Shoonyakasha {
namespace UI {

namespace {

constexpr size_t kNone = static_cast<size_t>(-1);

bool isSpace(char32_t c) {
    return c == U' ' || c == U'\t';
}

bool isSkipped(char32_t c) {
    return (c < 0x20 && c != U'\n' && c != U'\t') || c == 0x7F;
}

} // namespace

TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize) {
    TextLayoutResult result;
    if (!font.valid() || text.empty()) return result;

    const float scale = style.fontSize / SdfFont::kBakeSize;
    const size_t n = text.size();

    // Glyph and scaled advance per code point; -1 for '\n' and skipped ones.
    // Tabs are drawn as spaces.
    std::vector<int> glyphs(n, -1);
    std::vector<float> advances(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        if (text[i] == U'\n' || isSkipped(text[i])) continue;
        glyphs[i] = font.glyphForCodepoint(isSpace(text[i]) ? U' ' : text[i]);
        advances[i] = font.advance(glyphs[i]) * scale;
    }

    auto kern = [&](int left, int right) {
        return left >= 0 ? font.kerning(left, right) * scale : 0.0f;
    };
    auto lastGlyphBefore = [&](size_t begin, size_t end) {
        for (size_t j = end; j > begin; --j) {
            if (glyphs[j - 1] >= 0) return glyphs[j - 1];
        }
        return -1;
    };
    auto measure = [&](size_t begin, size_t end) {
        float x = 0.0f;
        int previous = -1;
        for (size_t j = begin; j < end; ++j) {
            if (glyphs[j] < 0) continue;
            x += kern(previous, glyphs[j]) + advances[j];
            previous = glyphs[j];
        }
        return x;
    };

    // Line breaking.
    const bool wrap = style.wrap && boxSize.x > 0.0f;
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t start = 0;
    size_t lastSpace = kNone;
    float penX = 0.0f;
    int previous = -1;

    for (size_t i = 0; i < n; ++i) {
        if (text[i] == U'\n') {
            ranges.emplace_back(start, i);
            start = i + 1;
            lastSpace = kNone;
            penX = 0.0f;
            previous = -1;
            continue;
        }
        const int glyph = glyphs[i];
        if (glyph < 0) continue;

        float width = kern(previous, glyph) + advances[i];
        while (wrap && !isSpace(text[i]) && i > start && penX + width > boxSize.x) {
            if (lastSpace != kNone) {
                ranges.emplace_back(start, lastSpace);
                start = lastSpace + 1;
            } else {
                ranges.emplace_back(start, i);
                start = i;
            }
            lastSpace = kNone;
            penX = measure(start, i);
            previous = lastGlyphBefore(start, i);
            width = kern(previous, glyph) + advances[i];
        }

        if (isSpace(text[i])) lastSpace = i;
        penX += width;
        previous = glyph;
    }
    ranges.emplace_back(start, n);

    // Vertical placement.
    const float lineHeight = (font.ascent() - font.descent() + font.lineGap()) * scale * style.lineSpacing;
    const float textHeight = (font.ascent() - font.descent()) * scale +
                             static_cast<float>(ranges.size() - 1) * lineHeight;
    float top = 0.0f;
    if (style.alignV == TextAlignV::Middle) top = (boxSize.y - textHeight) * 0.5f;
    else if (style.alignV == TextAlignV::Bottom) top = boxSize.y - textHeight;

    result.lines.reserve(ranges.size());
    float widest = 0.0f;

    for (size_t k = 0; k < ranges.size(); ++k) {
        const auto [begin, end] = ranges[k];

        size_t trimmed = end;
        while (trimmed > begin && (glyphs[trimmed - 1] < 0 || isSpace(text[trimmed - 1]))) --trimmed;

        TextLine line;
        line.firstCodepoint = begin;
        line.codepointCount = end - begin;
        line.width = measure(begin, trimmed);
        line.baseline = top + font.ascent() * scale + static_cast<float>(k) * lineHeight;
        widest = std::max(widest, line.width);

        float x = 0.0f;
        if (style.alignH == TextAlignH::Center) x = (boxSize.x - line.width) * 0.5f;
        else if (style.alignH == TextAlignH::Right) x = boxSize.x - line.width;

        int prev = -1;
        for (size_t j = begin; j < end; ++j) {
            const int glyph = glyphs[j];
            if (glyph < 0) continue;
            x += kern(prev, glyph);

            const SdfFont::GlyphBox box = font.glyphBox(glyph);
            if (!box.empty()) {
                PositionedGlyph placed;
                placed.glyph = glyph;
                placed.min = glm::vec2(x, line.baseline) + box.offset * scale;
                placed.max = placed.min + glm::vec2(box.size) * scale;
                result.glyphs.push_back(placed);
            }

            x += advances[j];
            prev = glyph;
        }

        result.lines.push_back(line);
    }

    result.size = glm::vec2(widest, textHeight);
    return result;
}

} // namespace UI
} // namespace Shoonyakasha
