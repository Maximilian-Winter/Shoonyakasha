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
            c.width += glyphs[g].advance * scale(glyphs[g].font, glyphs[g].cluster);
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

} // namespace

TextLayoutResult layoutText(const std::vector<const SdfFont*>& fonts, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize, const std::vector<TextSpan>& spans) {
    TextLayoutResult result;
    if (fonts.empty() || !fonts[0] || !fonts[0]->valid() || text.empty()) return result;
    const SdfFont& primary = *fonts[0];
    const float scale = style.fontSize / SdfFont::kBakeSize;
    const bool wrap = style.wrap && boxSize.x > 0.0f;
    // A box sized to the text's own width can come back a rounding error
    // narrower; that must not push its last cluster onto a new line.
    const float wrapWidth = boxSize.x + 1e-3f;

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
                                                    chained ? &chainAt : nullptr);
        std::vector<Cluster> clusterList = clustersOf(glyphs, text, begin, end, glyphScale, bidi.levels());
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

    result.lines.reserve(lines.size());
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

    result.size = glm::vec2(widest, textHeight);
    return result;
}

TextLayoutResult layoutText(const SdfFont& font, std::u32string_view text,
                            const TextStyle& style, const glm::vec2& boxSize) {
    return layoutText(std::vector<const SdfFont*>{&font}, text, style, boxSize);
}

} // namespace UI
} // namespace Shoonyakasha
