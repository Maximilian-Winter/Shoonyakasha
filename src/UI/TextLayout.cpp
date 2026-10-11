//
// TextLayout.cpp - Breaking text into lines and placing glyph quads
//

#include "UI/TextLayout.h"
#include "UI/Bidi.h"
#include "UI/LineBreak.h"
#include "UI/TextShaper.h"

#include <algorithm>
#include <utility>

namespace Shoonyakasha {
namespace UI {

namespace {

bool isSpace(char32_t c) {
    return isBreakingSpace(c);
}

/// A cluster of a shaped paragraph: its code points and its glyphs.
struct Cluster {
    size_t first = 0;         // code points [first, end)
    size_t end = 0;
    size_t glyphBegin = 0;    // glyphs [glyphBegin, glyphEnd) of the paragraph
    size_t glyphEnd = 0;
    float width = 0.0f;       // scaled
    bool space = false;
    uint8_t level = 0;
};

/// The clusters of a shaped paragraph [begin, end), in text order.
/// `scale(font, codepoint)` converts a glyph's bake-size pixels to layout
/// units.
template <typename Scale>
std::vector<Cluster> clustersOf(const std::vector<ShapedGlyph>& glyphs, std::u32string_view text,
                                size_t begin, size_t end, const Scale& scale,
                                const std::vector<uint8_t>& levels) {
    std::vector<Cluster> clusters;
    for (size_t g = 0; g < glyphs.size();) {
        Cluster c;
        c.first = glyphs[g].cluster;
        c.glyphBegin = g;
        while (g < glyphs.size() && glyphs[g].cluster == c.first) {
            // Along the line: down it for glyphs shaped top to bottom.
            const float along = glyphs[g].upright ? glyphs[g].verticalAdvance : glyphs[g].advance;
            c.width += along * scale(glyphs[g].font, glyphs[g].cluster);
            ++g;
        }
        c.glyphEnd = g;
        c.space = isSpace(text[c.first]);
        c.level = c.first - begin < levels.size() ? levels[c.first - begin] : 0;
        clusters.push_back(c);
    }
    // Right-to-left runs come out of shaping in display order.
    std::sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) { return a.first < b.first; });
    // A cluster's code points run to the next cluster.
    for (size_t k = 0; k < clusters.size(); ++k) {
        clusters[k].end = k + 1 < clusters.size() ? clusters[k + 1].first : end;
    }
    return clusters;
}

struct Paragraph {
    BidiParagraph bidi;
    std::vector<ShapedGlyph> glyphs;
    std::vector<Cluster> clusters;      // text order
};

struct LineRange {
    size_t paragraph = 0;
    size_t first = 0;         // clusters [first, last) of the paragraph, in text order
    size_t last = 0;
    size_t codepointFirst = 0;
    size_t codepointEnd = 0;
};

bool isAsciiDigit(char32_t c) {
    return c >= U'0' && c <= U'9';
}

} // namespace

bool uprightInVerticalText(char32_t c) {
    // The blocks whose characters UAX #50 gives an upright orientation (U,
    // or Tu and Tr, whose vertical forms the fonts supply).
    static constexpr std::pair<char32_t, char32_t> kUpright[] = {
        {0x1100, 0x11FF},    // Hangul Jamo
        {0x2E80, 0x2FFF},    // CJK radicals, Kangxi radicals, ideographic description
        {0x3000, 0x303F},    // CJK symbols and punctuation
        {0x3040, 0x30FF},    // Hiragana, Katakana
        {0x3100, 0x31FF},    // Bopomofo, Hangul compatibility Jamo, Kanbun, strokes, Katakana extensions
        {0x3200, 0x9FFF},    // enclosed CJK, compatibility, extension A, Yijing, unified ideographs
        {0xA000, 0xA4CF},    // Yi
        {0xA960, 0xA97F},    // Hangul Jamo extended A
        {0xAC00, 0xD7FF},    // Hangul syllables, Jamo extended B
        {0xF900, 0xFAFF},    // CJK compatibility ideographs
        {0xFE10, 0xFE1F},    // vertical forms
        {0xFE30, 0xFE6F},    // CJK compatibility forms, small forms
        {0xFF00, 0xFFEF},    // halfwidth and fullwidth forms
        {0x1F000, 0x1FAFF},  // game symbols, enclosed alphanumerics and ideographs, emoji
        {0x20000, 0x3FFFF},  // ideographs of the supplementary planes
    };
    for (const auto& [low, high] : kUpright) {
        if (c >= low && c <= high) return true;
    }
    return false;
}

TextLayoutResult layoutText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize, const std::vector<TextSpan>& spans) {
    TextLayoutResult result;
    if (fonts.empty() || !fonts[0] || !fonts[0]->valid() || text.empty()) return result;
    const SdfFont& primary = *fonts[0];
    const float scale = style.fontSize / SdfFont::kBakeSize;
    const bool vertical = style.orientation != TextOrientation::Horizontal;
    // Vertical lines run down the box.
    const float lineLength = vertical ? boxSize.y : boxSize.x;
    const bool wrap = style.wrap && lineLength > 0.0f;
    // A box sized to the text's own width can come back a rounding error
    // narrower; that must not push its last cluster onto a new line.
    const float wrapWidth = lineLength + 1e-3f;

    // Vertical text: how each code point is set.
    std::vector<VerticalForm> formAt;
    if (vertical) {
        formAt.resize(text.size(), VerticalForm::Sideways);
        for (size_t i = 0; i < text.size(); ++i) {
            if (uprightInVerticalText(text[i])) formAt[i] = VerticalForm::Upright;
        }
        if (style.uprightDigits) {
            for (size_t i = 0; i < text.size();) {
                size_t j = i;
                while (j < text.size() && isAsciiDigit(text[j])) ++j;
                if (j > i && j - i <= 2) {
                    for (size_t k = i; k < j; ++k) formAt[k] = VerticalForm::UprightHorizontal;
                }
                i = j > i ? j : i + 1;
            }
        }
    }

    // Each code point's fonts and size, from the span it is in.
    std::vector<const std::vector<uint32_t>*> chainAt(text.size(), nullptr);
    std::vector<float> sizeAt(text.size(), style.fontSize);
    for (const TextSpan& span : spans) {
        for (size_t i = span.begin; i < std::min(span.end, text.size()); ++i) {
            if (!span.fonts.empty()) chainAt[i] = &span.fonts;
            if (span.fontSize > 0.0f) sizeAt[i] = span.fontSize;
        }
    }
    const bool chained = std::any_of(chainAt.begin(), chainAt.end(), [](const auto* c) { return c != nullptr; });

    // A glyph is scaled to its size, and a glyph of a fallback font to the
    // em size of the first font of its code point's chain, so its letters
    // match whatever space that font reserves above and below.
    auto glyphScale = [&](uint32_t font, size_t codepoint) {
        const float s = (codepoint < sizeAt.size() ? sizeAt[codepoint] : style.fontSize) / SdfFont::kBakeSize;
        const std::vector<uint32_t>* chain = codepoint < chainAt.size() ? chainAt[codepoint] : nullptr;
        const uint32_t lead = chain && !chain->empty() && chain->front() < fonts.size() ? chain->front() : 0;
        const SdfFont* first = fonts[lead];
        if (font == lead || font >= fonts.size() || !fonts[font] || !first || first->heightInEms() <= 0.0f) return s;
        return s * fonts[font]->heightInEms() / first->heightInEms();
    };

    // Resolve, shape and break each paragraph into lines of clusters.
    std::vector<Paragraph> paragraphs;
    std::vector<LineRange> lines;
    for (size_t begin = 0; begin <= text.size();) {
        const size_t newline = text.find(U'\n', begin);
        const size_t end = newline == std::u32string_view::npos ? text.size() : newline;
        const size_t p = paragraphs.size();
        BidiParagraph bidi(text, begin, end, style.direction);
        std::vector<ShapedGlyph> glyphs = shapeText(fonts, text, begin, end, &bidi.levels(),
                                                    chained ? &chainAt : nullptr, vertical ? &formAt : nullptr);
        std::vector<Cluster> clusterList = clustersOf(glyphs, text, begin, end, glyphScale, bidi.levels());
        if (vertical) {
            // Digits set upright side by side take one character's height
            // together: their run's first cluster takes it all.
            for (size_t k = 0; k < clusterList.size(); ++k) {
                if (formAt[clusterList[k].first] != VerticalForm::UprightHorizontal) continue;
                const bool first = k == 0 || formAt[clusterList[k - 1].first] != VerticalForm::UprightHorizontal;
                clusterList[k].width = first ? (primary.ascent() - primary.descent()) * sizeAt[clusterList[k].first] /
                                                   SdfFont::kBakeSize
                                             : 0.0f;
            }
        }
        paragraphs.push_back({std::move(bidi), std::move(glyphs), std::move(clusterList)});
        const auto& clusters = paragraphs.back().clusters;
        const std::vector<LineBreakAfter> breaks = lineBreaks(text, begin, end);
        // The break after cluster k: that after its last code point.
        auto breakAfter = [&](size_t k) { return breaks[clusters[k].end - 1 - begin]; };

        auto codepointAt = [&](size_t k) { return k < clusters.size() ? clusters[k].first : end; };
        // The last place the line may break: at a space, which is dropped, or
        // after a cluster, which stays (UI/LineBreak.h).
        constexpr size_t kNone = static_cast<size_t>(-1);
        size_t start = 0;
        size_t lastBreak = kNone;
        bool breakAtSpace = false;
        float penX = 0.0f;
        for (size_t k = 0; k < clusters.size(); ++k) {
            const Cluster& c = clusters[k];
            if (wrap && !c.space && k > start && penX + c.width > wrapWidth) {
                if (lastBreak != kNone && breakAtSpace) {
                    lines.push_back({p, start, lastBreak, codepointAt(start), codepointAt(lastBreak)});
                    start = lastBreak + 1;
                } else if (lastBreak != kNone) {
                    lines.push_back({p, start, lastBreak + 1, codepointAt(start), codepointAt(lastBreak + 1)});
                    start = lastBreak + 1;
                } else {
                    lines.push_back({p, start, k, codepointAt(start), codepointAt(k)});
                    start = k;
                }
                lastBreak = kNone;
                penX = 0.0f;
                for (size_t j = start; j < k; ++j) penX += clusters[j].width;
            }
            if (c.space) {
                lastBreak = k;
                breakAtSpace = true;
            } else if (k + 1 < clusters.size() && !clusters[k + 1].space &&
                       breakAfter(k) != LineBreakAfter::None) {
                lastBreak = k;
                breakAtSpace = false;
            }
            penX += c.width;
            // A line separator, or a lone carriage return, ends the line.
            if (k + 1 < clusters.size() && breakAfter(k) == LineBreakAfter::Mandatory) {
                lines.push_back({p, start, k + 1, codepointAt(start), codepointAt(k + 1)});
                start = k + 1;
                lastBreak = kNone;
                penX = 0.0f;
            }
        }
        lines.push_back({p, start, clusters.size(), codepointAt(start), end});

        if (newline == std::u32string_view::npos) break;
        begin = newline + 1;
    }

    // Vertical placement. Each line is as tall as the first font at the
    // largest size on it; the step from one baseline to the next is the
    // first line's descent and gap and the second's ascent.
    std::vector<float> lineScale(lines.size(), scale);
    for (size_t l = 0; l < lines.size(); ++l) {
        const LineRange& range = lines[l];
        const auto& clusters = paragraphs[range.paragraph].clusters;
        float largest = 0.0f;
        for (size_t k = range.first; k < range.last; ++k) largest = std::max(largest, sizeAt[clusters[k].first]);
        // An empty line takes the size where it starts.
        if (range.first == range.last) largest = sizeAt[std::min(range.codepointFirst, text.size() - 1)];
        lineScale[l] = largest / SdfFont::kBakeSize;
    }
    std::vector<float> baselines(lines.size(), 0.0f);
    for (size_t l = 0; l < lines.size(); ++l) {
        baselines[l] = l == 0 ? primary.ascent() * lineScale[0]
                              : baselines[l - 1] + ((primary.lineGap() - primary.descent()) * lineScale[l - 1] +
                                                    primary.ascent() * lineScale[l]) * style.lineSpacing;
    }
    const float textHeight = baselines.back() - primary.descent() * lineScale.back();
    float top = 0.0f;
    if (style.alignV == TextAlignV::Middle) top = (boxSize.y - textHeight) * 0.5f;
    else if (style.alignV == TextAlignV::Bottom) top = boxSize.y - textHeight;

    // Vertical text: columns as wide as the lines are tall, side by side,
    // and placed as a block across the box.
    std::vector<float> columnX(lines.size(), 0.0f);
    std::vector<float> columnWidth(lines.size(), 0.0f);
    float columnsWidth = 0.0f;
    if (vertical) {
        std::vector<float> centre(lines.size(), 0.0f);
        for (size_t l = 0; l < lines.size(); ++l) {
            columnWidth[l] = (primary.ascent() - primary.descent()) * lineScale[l];
            centre[l] = l == 0 ? columnWidth[0] * 0.5f
                               : centre[l - 1] + (columnWidth[l - 1] * 0.5f + primary.lineGap() * lineScale[l - 1] +
                                                  columnWidth[l] * 0.5f) * style.lineSpacing;
        }
        columnsWidth = centre.back() + columnWidth.back() * 0.5f;
        const bool fromRight = style.orientation == TextOrientation::VerticalRightToLeft;
        TextAlignH align = style.alignH;
        if (align == TextAlignH::Start) align = fromRight ? TextAlignH::Right : TextAlignH::Left;
        else if (align == TextAlignH::End) align = fromRight ? TextAlignH::Left : TextAlignH::Right;
        float blockLeft = 0.0f;
        if (align == TextAlignH::Center) blockLeft = (boxSize.x - columnsWidth) * 0.5f;
        else if (align == TextAlignH::Right) blockLeft = boxSize.x - columnsWidth;
        for (size_t l = 0; l < lines.size(); ++l) {
            columnX[l] = fromRight ? blockLeft + columnsWidth - centre[l] : blockLeft + centre[l];
        }
    }

    // Places the clusters `order` of a paragraph down column `l`, centred on
    // x `cx` and `width` wide, from `y`.
    auto placeColumn = [&](TextLayoutResult& out, const Paragraph& paragraph, const std::vector<size_t>& order,
                           size_t l, float cx, float width, float y) {
        const auto& clusters = paragraph.clusters;
        const auto& glyphs = paragraph.glyphs;
        auto place = [&](const Cluster& c, const ShapedGlyph& shaped, glm::vec2 min, glm::vec2 max, bool sideways) {
            PositionedGlyph placed;
            placed.font = shaped.font;
            placed.glyph = shaped.glyph;
            placed.min = min;
            placed.max = max;
            placed.codepoint = c.first;
            placed.line = l;
            placed.sideways = sideways;
            out.glyphs.push_back(placed);
        };
        for (size_t i = 0; i < order.size();) {
            const Cluster& c = clusters[order[i]];
            const VerticalForm form = formAt[c.first];

            if (form == VerticalForm::UprightHorizontal) {
                // The run's glyphs side by side, centred across the column,
                // squeezed to fit it, in the height its first cluster takes.
                size_t j = i;
                float runWidth = 0.0f;
                for (; j < order.size() && formAt[clusters[order[j]].first] == VerticalForm::UprightHorizontal; ++j) {
                    const Cluster& d = clusters[order[j]];
                    for (size_t g = d.glyphBegin; g < d.glyphEnd; ++g) {
                        runWidth += glyphs[g].advance * glyphScale(glyphs[g].font, glyphs[g].cluster);
                    }
                }
                const float squeeze = runWidth > width && runWidth > 0.0f ? width / runWidth : 1.0f;
                const float baseline = y + primary.ascent() * sizeAt[c.first] / SdfFont::kBakeSize;
                float x = cx - runWidth * squeeze * 0.5f;
                for (size_t r = i; r < j; ++r) {
                    const Cluster& d = clusters[order[r]];
                    for (size_t g = d.glyphBegin; g < d.glyphEnd; ++g) {
                        const ShapedGlyph& shaped = glyphs[g];
                        const float s = glyphScale(shaped.font, shaped.cluster);
                        const SdfFont::GlyphBox box = fonts[shaped.font]->glyphBox(shaped.glyph);
                        if (!box.empty()) {
                            const glm::vec2 offset = (shaped.offset + box.offset) * s;
                            const glm::vec2 min(x + offset.x * squeeze, baseline + offset.y);
                            place(d, shaped, min, min + glm::vec2(box.size) * s * glm::vec2(squeeze, 1.0f), false);
                        }
                        x += shaped.advance * s * squeeze;
                    }
                }
                y += c.width;
                i = j;
                continue;
            }

            // Turned clockwise: the horizontal pen runs down the column, and
            // the baseline sits so the line's ascent and descent straddle the
            // column's centre.
            const float size = sizeAt[c.first] / SdfFont::kBakeSize;
            const float baselineX = cx - (primary.ascent() + primary.descent()) * size * 0.5f;
            for (size_t g = c.glyphBegin; g < c.glyphEnd; ++g) {
                const ShapedGlyph& shaped = glyphs[g];
                const float s = glyphScale(shaped.font, shaped.cluster);
                const SdfFont::GlyphBox box = fonts[shaped.font]->glyphBox(shaped.glyph);
                if (shaped.upright) {
                    // Shaped top to bottom: the pen is on the column's centre.
                    if (!box.empty()) {
                        const glm::vec2 min = glm::vec2(cx, y) + (shaped.offset + box.offset) * s;
                        place(c, shaped, min, min + glm::vec2(box.size) * s, false);
                    }
                    y += shaped.verticalAdvance * s;
                    continue;
                }
                if (!box.empty()) {
                    const glm::vec2 low = (shaped.offset + box.offset) * s;   // along, across (down)
                    const glm::vec2 high = low + glm::vec2(box.size) * s;
                    place(c, shaped, {baselineX - high.y, y + low.x}, {baselineX - low.y, y + high.x}, true);
                }
                y += shaped.advance * s;
            }
            ++i;
        }
    };

    result.lines.reserve(lines.size());
    result.vertical = vertical;
    float widest = 0.0f;
    std::vector<size_t> order;   // the line's clusters in display order
    for (size_t l = 0; l < lines.size(); ++l) {
        const LineRange& range = lines[l];
        const Paragraph& paragraph = paragraphs[range.paragraph];
        const auto& clusters = paragraph.clusters;
        const auto& glyphs = paragraph.glyphs;
        const bool rtl = paragraph.bidi.rightToLeft();

        // Spaces at the end of the line in text order: they go at its end in
        // the paragraph's direction and are left out of its width.
        size_t trimmed = range.last;
        while (trimmed > range.first && clusters[trimmed - 1].space) --trimmed;

        TextLine line;
        line.firstCodepoint = range.codepointFirst;
        line.codepointCount = range.codepointEnd - range.codepointFirst;
        line.rightToLeft = rtl;
        for (size_t k = range.first; k < trimmed; ++k) line.width += clusters[k].width;
        line.baseline = top + baselines[l];
        line.top = line.baseline - primary.ascent() * lineScale[l];
        line.bottom = line.baseline - primary.descent() * lineScale[l];
        widest = std::max(widest, line.width);

        // The line's runs in display order, and each run's clusters in its
        // direction.
        order.clear();
        if (trimmed > range.first) {
            const size_t contentEnd = trimmed < clusters.size() ? clusters[trimmed].first : range.codepointEnd;
            for (const BidiRun& run : paragraph.bidi.lineRuns(clusters[range.first].first, contentEnd)) {
                const size_t before = order.size();
                for (size_t k = range.first; k < trimmed; ++k) {
                    if (clusters[k].first >= run.begin && clusters[k].first < run.end) order.push_back(k);
                }
                if (run.level & 1u) std::reverse(order.begin() + static_cast<std::ptrdiff_t>(before), order.end());
            }
        }
        if (vertical) {
            for (size_t k = trimmed; k < range.last; ++k) order.push_back(k);
            line.baseline = columnX[l];
            line.top = columnX[l] - columnWidth[l] * 0.5f;
            line.bottom = columnX[l] + columnWidth[l] * 0.5f;
            float y = 0.0f;
            if (style.alignV == TextAlignV::Middle) y = (boxSize.y - line.width) * 0.5f;
            else if (style.alignV == TextAlignV::Bottom) y = boxSize.y - line.width;
            line.left = y;
            placeColumn(result, paragraph, order, l, columnX[l], columnWidth[l], y);
            result.lines.push_back(line);
            continue;
        }

        float trailing = 0.0f;
        for (size_t k = trimmed; k < range.last; ++k) trailing += clusters[k].width;
        if (rtl) {
            // The first of them next to the content, on its left.
            for (size_t k = trimmed; k < range.last; ++k) order.insert(order.begin(), k);
        } else {
            for (size_t k = trimmed; k < range.last; ++k) order.push_back(k);
        }

        TextAlignH align = style.alignH;
        if (align == TextAlignH::Start) align = rtl ? TextAlignH::Right : TextAlignH::Left;
        else if (align == TextAlignH::End) align = rtl ? TextAlignH::Left : TextAlignH::Right;
        float x = 0.0f;
        if (align == TextAlignH::Center) x = (boxSize.x - line.width) * 0.5f;
        else if (align == TextAlignH::Right) x = boxSize.x - line.width;
        line.left = x;
        if (rtl) x -= trailing;   // the trailing spaces are left of the content

        for (const size_t k : order) {
            const Cluster& c = clusters[k];
            const uint8_t level = k >= trimmed ? paragraph.bidi.baseLevel() : c.level;
            result.clusters.push_back({c.first, c.end - c.first, l, x, c.width, level});
            for (size_t g = c.glyphBegin; g < c.glyphEnd; ++g) {
                const ShapedGlyph& shaped = glyphs[g];
                const float s = glyphScale(shaped.font, shaped.cluster);
                const SdfFont::GlyphBox box = fonts[shaped.font]->glyphBox(shaped.glyph);
                if (!box.empty()) {
                    PositionedGlyph placed;
                    placed.font = shaped.font;
                    placed.glyph = shaped.glyph;
                    placed.min = glm::vec2(x, line.baseline) + (shaped.offset + box.offset) * s;
                    placed.max = placed.min + glm::vec2(box.size) * s;
                    placed.codepoint = c.first;
                    placed.line = l;
                    result.glyphs.push_back(placed);
                }
                x += shaped.advance * s;
            }
        }
        result.lines.push_back(line);
    }

    result.size = vertical ? glm::vec2(columnsWidth, widest) : glm::vec2(widest, textHeight);
    return result;
}

TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize) {
    return layoutText(std::vector<const SdfFont*>{&font}, text, style, boxSize);
}

} // namespace UI
} // namespace Shoonyakasha
