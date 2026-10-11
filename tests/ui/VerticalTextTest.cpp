//
// VerticalTextTest.cpp - Columns of Japanese and Mongolian text
//
// Tier 1 and 2: fonts, and an EnTT registry for the systems; no GPU.
//

#include <gtest/gtest.h>

#include "UI/FontLibrary.h"
#include "UI/SdfFont.h"
#include "UI/TextLayout.h"
#include "UI/UIBatchBuilder.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

/// Roboto, with Japanese and Mongolian after it.
struct Fonts {
    SdfFont latin, japanese, mongolian;
    Fonts() {
        EXPECT_TRUE(latin.loadFromFile(kFonts + "Roboto-Regular.ttf"));
        EXPECT_TRUE(japanese.loadFromFile(kFonts + "NotoSansJP-Subset.ttf"));
        EXPECT_TRUE(mongolian.loadFromFile(kFonts + "NotoSansMongolian-Regular.ttf"));
    }
    std::vector<const SdfFont*> all() const { return {&latin, &japanese, &mongolian}; }

    TextLayoutResult layout(const std::u32string& text, TextOrientation orientation, glm::vec2 box = {400, 400},
                            bool uprightDigits = false, TextAlignH alignH = TextAlignH::Left,
                            TextAlignV alignV = TextAlignV::Top) const {
        TextStyle style;
        style.fontSize = 32.0f;
        style.orientation = orientation;
        style.uprightDigits = uprightDigits;
        style.alignH = alignH;
        style.alignV = alignV;
        return layoutText(all(), text, style, box);
    }
};

const PositionedGlyph* glyphAt(const TextLayoutResult& layout, size_t codepoint) {
    for (const PositionedGlyph& g : layout.glyphs) {
        if (g.codepoint == codepoint) return &g;
    }
    return nullptr;
}

glm::vec2 centre(const PositionedGlyph& g) {
    return (g.min + g.max) * 0.5f;
}

// The distance field's padding around each glyph quad, at size 32.
constexpr float kPad = SdfFont::kPadding * 32.0f / SdfFont::kBakeSize;

constexpr TextOrientation kRightToLeft = TextOrientation::VerticalRightToLeft;
constexpr TextOrientation kLeftToRight = TextOrientation::VerticalLeftToRight;

} // namespace

TEST(VerticalText, AJapaneseColumnStandsUprightWithVerticalPunctuation) {
    Fonts f;
    // a, ideographic comma, ideographic full stop, corner brackets around "nichi"
    const std::u32string text = {0x3042, 0x3001, 0x3002, 0x300C, 0x65E5, 0x300D};
    const TextLayoutResult column = f.layout(text, kRightToLeft);
    const TextLayoutResult row = f.layout(text, TextOrientation::Horizontal);
    ASSERT_TRUE(column.vertical);
    ASSERT_EQ(column.lines.size(), 1u);
    EXPECT_TRUE(column.clusters.empty());
    const TextLine& line = column.lines[0];

    // Upright, down the column from its top, the letters centred on it.
    float lastY = 0.0f;
    for (const PositionedGlyph& g : column.glyphs) {
        EXPECT_FALSE(g.sideways);
        EXPECT_GT(centre(g).y, lastY);
        lastY = centre(g).y;
    }
    for (const size_t index : {size_t{0}, size_t{4}}) {
        const PositionedGlyph* g = glyphAt(column, index);
        ASSERT_NE(g, nullptr);
        EXPECT_NEAR(centre(*g).x, line.baseline, 32.0f * 0.1f);
    }
    // The punctuation takes the font's vertical forms: other glyphs than in a row.
    for (const size_t index : {size_t{1}, size_t{3}}) {
        ASSERT_NE(glyphAt(column, index), nullptr);
        ASSERT_NE(glyphAt(row, index), nullptr);
        EXPECT_NE(glyphAt(column, index)->glyph, glyphAt(row, index)->glyph) << index;
    }
    // Each character takes about one em down the column.
    EXPECT_NEAR(glyphAt(column, 4)->min.y - glyphAt(column, 0)->min.y, 4.0f * 32.0f, 32.0f);
}

TEST(VerticalText, ALatinWordIsTurnedClockwise) {
    Fonts f;
    const std::u32string text = {0x65E5, 0x672C, U'A', U'B'};   // nihon AB
    const TextLayoutResult column = f.layout(text, kRightToLeft);
    const TextLayoutResult row = f.layout(U"AB", TextOrientation::Horizontal);
    const TextLine& line = column.lines[0];
    for (const size_t index : {size_t{2}, size_t{3}}) {
        const PositionedGlyph* turned = glyphAt(column, index);
        const PositionedGlyph* upright = glyphAt(row, index - 2);
        ASSERT_NE(turned, nullptr);
        ASSERT_NE(upright, nullptr);
        EXPECT_TRUE(turned->sideways);
        // Its box turned: as wide as the letter is tall, and as tall as it is wide.
        EXPECT_NEAR(turned->max.x - turned->min.x, upright->max.y - upright->min.y, 1e-3f);
        EXPECT_NEAR(turned->max.y - turned->min.y, upright->max.x - upright->min.x, 1e-3f);
        // In the column, below the Japanese.
        EXPECT_GT(turned->min.x + kPad, line.top - 1.0f);
        EXPECT_LT(turned->max.x - kPad, line.bottom + 1.0f);
        EXPECT_GT(turned->min.y + kPad, glyphAt(column, 1)->max.y - kPad - 1.0f);
    }
    // B follows A down the column.
    EXPECT_GT(glyphAt(column, 3)->min.y, glyphAt(column, 2)->min.y);
}

TEST(VerticalText, OneOrTwoDigitsStandUprightSideBySide) {
    Fonts f;
    // "nen 12 tsuki", and "2026 nen"
    const std::u32string date = {0x5E74, U'1', U'2', 0x6708};
    const TextLayoutResult upright = f.layout(date, kRightToLeft, {400, 400}, true);
    const PositionedGlyph* one = glyphAt(upright, 1);
    const PositionedGlyph* two = glyphAt(upright, 2);
    ASSERT_NE(one, nullptr);
    ASSERT_NE(two, nullptr);
    EXPECT_FALSE(one->sideways);
    EXPECT_FALSE(two->sideways);
    EXPECT_LT(one->max.x - kPad, two->min.x + kPad);       // side by side
    EXPECT_NEAR(one->max.y, two->max.y, 1.0f);            // on one baseline
    const TextLine& line = upright.lines[0];
    EXPECT_GE(one->min.x + kPad, line.top - 1e-3f);       // squeezed into the column
    EXPECT_LE(two->max.x - kPad, line.bottom + 1e-3f);
    // The month character comes one character's height after the year.
    const float cell = glyphAt(upright, 3)->min.y - glyphAt(upright, 0)->min.y;
    EXPECT_NEAR(cell, 2.0f * 32.0f, 32.0f * 0.25f);

    // Without the option, or for more than two digits, digits are turned.
    EXPECT_TRUE(glyphAt(f.layout(date, kRightToLeft), 1)->sideways);
    const TextLayoutResult year = f.layout({U'2', U'0', U'2', U'6', 0x5E74}, kRightToLeft, {400, 400}, true);
    for (size_t i = 0; i < 4; ++i) EXPECT_TRUE(glyphAt(year, i)->sideways) << i;
}

TEST(VerticalText, MongolianColumnsRunFromTheLeft) {
    Fonts f;
    // Two paragraphs: "mongol" and "bichig".
    const std::u32string text = {0x182E, 0x1823, 0x1829, 0x182D, 0x1823, 0x182F, U'\n',
                                 0x182A, 0x1822, 0x1834, 0x1822, 0x182D};
    const TextLayoutResult fromLeft = f.layout(text, kLeftToRight);
    ASSERT_EQ(fromLeft.lines.size(), 2u);
    EXPECT_LT(fromLeft.lines[0].baseline, fromLeft.lines[1].baseline);
    for (const PositionedGlyph& g : fromLeft.glyphs) {
        EXPECT_TRUE(g.sideways);
        EXPECT_EQ(g.font, 2u);   // from the Mongolian font
    }
    // The columns stand side by side, starting at the left of the box.
    EXPECT_NEAR(fromLeft.lines[0].top, 0.0f, 1e-3f);
    EXPECT_LE(fromLeft.lines[0].bottom, fromLeft.lines[1].top + 1e-3f);

    const TextLayoutResult fromRight = f.layout(text, kRightToLeft);
    EXPECT_GT(fromRight.lines[0].baseline, fromRight.lines[1].baseline);
}

TEST(VerticalText, ColumnsWrapAtTheBoxHeightAndAlign) {
    Fonts f;
    std::u32string text;
    for (int i = 0; i < 12; ++i) text += std::u32string{0x3042, 0x3044, 0x3046};   // a i u
    const glm::vec2 box(400.0f, 200.0f);
    const TextLayoutResult start = f.layout(text, kRightToLeft, box, false, TextAlignH::Start, TextAlignV::Middle);
    ASSERT_GT(start.lines.size(), 2u);
    float longest = 0.0f;
    for (const TextLine& line : start.lines) {
        EXPECT_LE(line.width, box.y + 1e-3f);
        EXPECT_NEAR(line.left, (box.y - line.width) * 0.5f, 1e-3f);   // centred down the column
        longest = std::max(longest, line.width);
    }
    // From the right: Start puts the first column at the box's right edge.
    EXPECT_GT(start.lines[0].baseline, start.lines[1].baseline);
    EXPECT_NEAR(start.lines[0].bottom, box.x, 1e-3f);
    EXPECT_NEAR(start.size.x, start.lines[0].bottom - start.lines.back().top, 1e-3f);
    EXPECT_NEAR(start.size.y, longest, 1e-3f);

    // Left puts the last column at the box's left edge.
    const TextLayoutResult left = f.layout(text, kRightToLeft, box, false, TextAlignH::Left);
    EXPECT_NEAR(left.lines.back().top, 0.0f, 1e-3f);
    EXPECT_NEAR(left.lines[0].left, 0.0f, 1e-3f);
}

TEST(VerticalText, TurnedGlyphsAreDrawnWithTheirTextureTurnedAndMeasuredUpright) {
    entt::registry registry;
    FontLibrary fonts;
    const uint32_t latin = fonts.load(kFonts + "Roboto-Regular.ttf");
    fonts.setFallbacks(latin, {fonts.load(kFonts + "NotoSansJP-Subset.ttf")});
    glm::vec2 screen{800.0f, 600.0f};
    const entt::entity canvas = registry.create();
    registry.emplace<UICanvas>(canvas);
    const entt::entity e = createElement(registry, canvas, {300.0f, 300.0f});
    auto& text = registry.emplace<UIText>(e);
    text.text = encodeUtf8(std::u32string{0x65E5, U'A'});
    text.font = latin;
    text.fontSize = 32.0f;
    text.orientation = TextOrientation::VerticalRightToLeft;
    registry.emplace<UIContentSizeFitter>(e) = {UIContentSizeFitter::Fit::Preferred, UIContentSizeFitter::Fit::Preferred};
    for (int i = 0; i < 2; ++i) {
        CanvasLayoutSystem(&screen, &fonts).update(registry, 0.0f);
        CanvasTextSystem(&fonts).update(registry, 0.0f);
    }
    const auto& layout = registry.get<UITextCache>(e).layout;
    ASSERT_EQ(layout.glyphs.size(), 2u);
    // Sized to the text: as wide as its column, as tall as the column is long.
    const glm::vec2 size = registry.get<UIRect>(e).rect.size();
    EXPECT_NEAR(size.x, layout.size.x, 1e-3f);
    EXPECT_NEAR(size.y, layout.size.y, 1e-3f);
    EXPECT_GT(size.y, size.x);

    GlyphCache glyphs(512);
    UIDrawData data;
    buildDrawData(registry, fonts, glyphs, data);
    ASSERT_EQ(data.vertices.size(), 8u);
    const UIVertex* upright = &data.vertices[0];
    const UIVertex* turned = &data.vertices[4];
    // Upright: the texture's top-left at the quad's top-left.
    EXPECT_LT(upright[0].uv.x, upright[1].uv.x);
    EXPECT_FLOAT_EQ(upright[0].uv.y, upright[1].uv.y);
    // Turned clockwise: the texture's bottom-left at the quad's top-left,
    // its top-left at the quad's top-right.
    EXPECT_FLOAT_EQ(turned[0].uv.x, turned[1].uv.x);
    EXPECT_GT(turned[0].uv.y, turned[1].uv.y);
    EXPECT_FLOAT_EQ(turned[1].uv.y, turned[2].uv.y);
    EXPECT_LT(turned[1].uv.x, turned[2].uv.x);
}
