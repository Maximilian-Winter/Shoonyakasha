//
// TextTest.cpp - UTF-8 decoding, atlas packing, SDF fonts and text layout
//
// Tier 1: CPU only. The font tests read the fonts shipped in assets/fonts.
//

#include <gtest/gtest.h>

#include "UI/Utf8.h"
#include "UI/GlyphAtlasPacker.h"
#include "UI/SdfFont.h"
#include "UI/TextLayout.h"

#include <random>
#include <string>

using namespace Shoonyakasha::UI;

namespace {

const std::string kFontDir = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

const SdfFont& roboto() {
    static SdfFont font = [] {
        SdfFont f;
        f.loadFromFile(kFontDir + "Roboto-Regular.ttf");
        return f;
    }();
    return font;
}

} // namespace

// ── UTF-8 ───────────────────────────────────────────────────────

TEST(Utf8, DecodesOneToFourByteSequences) {
    // A, é, €, क (Devanagari KA), 😀
    EXPECT_EQ(decodeUtf8("A\xC3\xA9\xE2\x82\xAC\xE0\xA4\x95\xF0\x9F\x98\x80"),
              std::u32string({U'A', 0xE9, 0x20AC, 0x0915, 0x1F600}));
}

TEST(Utf8, StrayContinuationAndInvalidLeadBytes) {
    EXPECT_EQ(decodeUtf8("a\x80" "b\xFF" "c"), std::u32string({U'a', 0xFFFD, U'b', 0xFFFD, U'c'}));
}

TEST(Utf8, TruncatedSequenceIsOneReplacement) {
    EXPECT_EQ(decodeUtf8("\xE2\x82"), std::u32string({0xFFFD}));
    EXPECT_EQ(decodeUtf8("\xE2\x82" "A"), std::u32string({0xFFFD, U'A'}));
}

TEST(Utf8, OverlongSurrogateAndOutOfRangeAreRejected) {
    EXPECT_EQ(decodeUtf8("\xC0\xAF"), std::u32string({0xFFFD}));          // overlong '/'
    EXPECT_EQ(decodeUtf8("\xED\xA0\x80"), std::u32string({0xFFFD}));      // U+D800
    EXPECT_EQ(decodeUtf8("\xF4\x90\x80\x80"), std::u32string({0xFFFD}));  // U+110000
}

// ── GlyphAtlasPacker ────────────────────────────────────────────

TEST(GlyphAtlasPacker, PlacementsStayInBoundsAndDoNotOverlap) {
    GlyphAtlasPacker packer(256, 256, 1);
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> side(4, 40);

    struct Placed { glm::ivec2 pos, size; };
    std::vector<Placed> placed;
    for (int i = 0; i < 1000; ++i) {
        const glm::ivec2 size(side(rng), side(rng));
        const auto pos = packer.insert(size.x, size.y);
        if (!pos) break;
        placed.push_back({*pos, size});
    }
    ASSERT_GT(placed.size(), 20u);

    for (size_t a = 0; a < placed.size(); ++a) {
        const auto& p = placed[a];
        EXPECT_GE(p.pos.x, 0);
        EXPECT_GE(p.pos.y, 0);
        EXPECT_LE(p.pos.x + p.size.x, 256);
        EXPECT_LE(p.pos.y + p.size.y, 256);
        for (size_t b = a + 1; b < placed.size(); ++b) {
            const auto& q = placed[b];
            // With spacing 1, rects must also not touch.
            const bool apart = p.pos.x + p.size.x + 1 <= q.pos.x || q.pos.x + q.size.x + 1 <= p.pos.x ||
                               p.pos.y + p.size.y + 1 <= q.pos.y || q.pos.y + q.size.y + 1 <= p.pos.y;
            EXPECT_TRUE(apart) << "rects " << a << " and " << b;
        }
    }
}

TEST(GlyphAtlasPacker, FullAtlasRefusesAndResetFrees) {
    GlyphAtlasPacker packer(64, 64, 0);
    EXPECT_TRUE(packer.insert(64, 32));
    EXPECT_TRUE(packer.insert(64, 32));
    EXPECT_FALSE(packer.insert(1, 1));
    EXPECT_FALSE(packer.insert(65, 1));

    packer.reset();
    const auto pos = packer.insert(64, 64);
    ASSERT_TRUE(pos);
    EXPECT_EQ(*pos, glm::ivec2(0, 0));
}

TEST(GlyphAtlasPacker, UsesTheShelfThatWastesLeastHeight) {
    GlyphAtlasPacker packer(256, 256, 0);
    EXPECT_EQ(*packer.insert(10, 20), glm::ivec2(0, 0));   // shelf at y 0, height 20
    EXPECT_EQ(*packer.insert(10, 40), glm::ivec2(0, 20));  // shelf at y 20, height 40
    EXPECT_EQ(*packer.insert(10, 18), glm::ivec2(10, 0));  // fits both; the 20 shelf wastes less
    EXPECT_EQ(*packer.insert(10, 30), glm::ivec2(10, 20));
}

// ── SdfFont ─────────────────────────────────────────────────────

TEST(SdfFont, LoadFailures) {
    SdfFont font;
    EXPECT_FALSE(font.loadFromFile(kFontDir + "does-not-exist.ttf"));
    EXPECT_FALSE(font.loadFromMemory({1, 2, 3, 4, 5, 6, 7, 8}));
    EXPECT_FALSE(font.valid());
    EXPECT_EQ(font.glyphForCodepoint(U'A'), 0);
}

TEST(SdfFont, MetricsAtBakeSize) {
    const SdfFont& font = roboto();
    ASSERT_TRUE(font.valid());
    EXPECT_GT(font.ascent(), 0.0f);
    EXPECT_LT(font.descent(), 0.0f);
    EXPECT_NEAR(font.ascent() - font.descent(), SdfFont::kBakeSize, 0.5f);
}

TEST(SdfFont, MissingCodepointFallsBack) {
    const SdfFont& font = roboto();
    EXPECT_NE(font.findGlyph(U'A'), 0);
    EXPECT_EQ(font.findGlyph(0xE000), 0);  // private use area

    const int expected = font.findGlyph(0xFFFD) ? font.findGlyph(0xFFFD) : font.findGlyph(U'?');
    EXPECT_NE(expected, 0);
    EXPECT_EQ(font.glyphForCodepoint(0xE000), expected);
}

TEST(SdfFont, RasterizedFieldMatchesBoxAndHasInsideAndOutside) {
    const SdfFont& font = roboto();
    const int glyph = font.findGlyph(U'I');
    const auto box = font.glyphBox(glyph);
    const auto bitmap = font.rasterize(glyph);

    ASSERT_FALSE(box.empty());
    EXPECT_EQ(bitmap.size, box.size);
    ASSERT_EQ(bitmap.pixels.size(), static_cast<size_t>(box.size.x * box.size.y));

    const auto at = [&](int x, int y) { return bitmap.pixels[static_cast<size_t>(y * box.size.x + x)]; };
    EXPECT_GT(at(box.size.x / 2, box.size.y / 2), SdfFont::kOnEdge);  // inside the stem
    EXPECT_LT(at(0, 0), SdfFont::kOnEdge);                            // in the padding
    // The glyph sits on the baseline: its box starts above it and ends near it.
    EXPECT_LT(box.offset.y, 0.0f);
    EXPECT_NEAR(box.offset.y + static_cast<float>(box.size.y), static_cast<float>(SdfFont::kPadding), 2.0f);
}

TEST(SdfFont, SpaceHasAdvanceButNoField) {
    const SdfFont& font = roboto();
    const int space = font.findGlyph(U' ');
    EXPECT_GT(font.advance(space), 0.0f);
    EXPECT_TRUE(font.glyphBox(space).empty());
    EXPECT_TRUE(font.rasterize(space).pixels.empty());
}

TEST(SdfFont, NonLatinFontCoversItsScript) {
    SdfFont font;
    ASSERT_TRUE(font.loadFromFile(kFontDir + "NotoSansDevanagari-Regular.ttf"));
    const int ka = font.findGlyph(0x0915);
    EXPECT_NE(ka, 0);
    EXPECT_FALSE(font.rasterize(ka).pixels.empty());
}

// ── TextLayout ──────────────────────────────────────────────────

namespace {

TextLayoutResult layout(std::u32string_view text, TextStyle style = {}, glm::vec2 box = {1000, 500}) {
    return layoutText(roboto(), text, style, box);
}

float lineWidth(std::u32string_view text, float fontSize = 24.0f) {
    TextStyle style;
    style.fontSize = fontSize;
    style.wrap = false;
    return layout(text, style).lines.at(0).width;
}

} // namespace

TEST(TextLayout, EmptyTextHasNoLines) {
    const auto result = layout(U"");
    EXPECT_TRUE(result.lines.empty());
    EXPECT_TRUE(result.glyphs.empty());
}

TEST(TextLayout, SingleLineSitsOnTheFirstBaseline) {
    const auto result = layout(U"Hello");
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.glyphs.size(), 5u);

    const float scale = 24.0f / SdfFont::kBakeSize;
    EXPECT_NEAR(result.lines[0].baseline, roboto().ascent() * scale, 1e-3f);
    EXPECT_NEAR(result.size.y, 24.0f, 0.5f);
    // Glyphs advance left to right.
    for (size_t i = 1; i < result.glyphs.size(); ++i) {
        EXPECT_GT(result.glyphs[i].min.x, result.glyphs[i - 1].min.x);
    }
}

TEST(TextLayout, WidthScalesWithFontSize) {
    EXPECT_NEAR(lineWidth(U"Shoonyakasha", 48.0f), 2.0f * lineWidth(U"Shoonyakasha", 24.0f), 1e-2f);
}

TEST(TextLayout, NewlineStartsALine) {
    TextStyle style;
    style.lineSpacing = 1.5f;
    const auto result = layout(U"ab\ncd", style);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1].firstCodepoint, 3u);

    const float scale = 24.0f / SdfFont::kBakeSize;
    const float lineHeight = (roboto().ascent() - roboto().descent() + roboto().lineGap()) * scale * 1.5f;
    EXPECT_NEAR(result.lines[1].baseline - result.lines[0].baseline, lineHeight, 1e-3f);
}

TEST(TextLayout, WrapsAtTheLastSpace) {
    const float hello = lineWidth(U"hello");
    const float whole = lineWidth(U"hello world");

    const auto result = layout(U"hello world", {}, {(hello + whole) * 0.5f, 500});
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].codepointCount, 5u);  // "hello", the space dropped
    EXPECT_EQ(result.lines[1].firstCodepoint, 6u);
    EXPECT_NEAR(result.lines[0].width, hello, 1e-3f);
}

TEST(TextLayout, NoWrapKeepsOneLine) {
    TextStyle style;
    style.wrap = false;
    EXPECT_EQ(layout(U"hello world", style, {10, 500}).lines.size(), 1u);
}

TEST(TextLayout, LongWordBreaksBetweenCharacters) {
    const float boxWidth = lineWidth(U"mmm") + 0.5f;
    const auto result = layout(U"mmmmmmmmmm", {}, {boxWidth, 500});
    EXPECT_GE(result.lines.size(), 3u);
    size_t total = 0;
    for (const auto& line : result.lines) {
        EXPECT_LE(line.width, boxWidth + 1e-3f);
        EXPECT_GT(line.codepointCount, 0u);
        total += line.codepointCount;
    }
    EXPECT_EQ(total, 10u);
}

TEST(TextLayout, HorizontalAlignment) {
    const glm::vec2 box(400, 100);
    const auto left = layout(U"Tara", {}, box);
    const float width = left.lines[0].width;

    TextStyle center;
    center.alignH = TextAlignH::Center;
    TextStyle right;
    right.alignH = TextAlignH::Right;

    EXPECT_NEAR(layout(U"Tara", center, box).glyphs[0].min.x,
                left.glyphs[0].min.x + (box.x - width) * 0.5f, 1e-3f);
    EXPECT_NEAR(layout(U"Tara", right, box).glyphs[0].min.x,
                left.glyphs[0].min.x + (box.x - width), 1e-3f);
}

TEST(TextLayout, VerticalAlignment) {
    const glm::vec2 box(400, 100);
    const auto top = layout(U"Tara", {}, box);

    TextStyle middle;
    middle.alignV = TextAlignV::Middle;
    TextStyle bottom;
    bottom.alignV = TextAlignV::Bottom;

    EXPECT_NEAR(layout(U"Tara", middle, box).lines[0].baseline,
                top.lines[0].baseline + (box.y - top.size.y) * 0.5f, 1e-3f);
    EXPECT_NEAR(layout(U"Tara", bottom, box).lines[0].baseline,
                top.lines[0].baseline + (box.y - top.size.y), 1e-3f);
}

TEST(TextLayout, SpacesAndControlCharactersMakeNoQuads) {
    EXPECT_EQ(layout(U"a b").glyphs.size(), 2u);
    EXPECT_EQ(layout(U"a\x01" U"b").glyphs.size(), 2u);
    EXPECT_NEAR(lineWidth(U"a\x01" U"b"), lineWidth(U"ab"), 1e-4f);
}

TEST(TextLayout, TrailingSpacesDoNotCountTowardWidth) {
    EXPECT_NEAR(lineWidth(U"ab   "), lineWidth(U"ab"), 1e-4f);
}

TEST(TextLayout, MissingCodepointUsesFallbackGlyph) {
    const std::u32string privateUse(1, char32_t{0xE000});
    const auto result = layout(privateUse);
    ASSERT_EQ(result.glyphs.size(), 1u);
    EXPECT_EQ(result.glyphs[0].glyph, roboto().glyphForCodepoint(0xE000));
}
