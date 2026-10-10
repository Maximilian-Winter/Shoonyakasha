//
// LineBreakTest.cpp - Script-aware line breaking
//
// Tier 1: fonts from assets/fonts, no GPU. Strings are built from code points
// because this file is not compiled as UTF-8.
//

#include <gtest/gtest.h>

#include "UI/LineBreak.h"
#include "UI/TextLayout.h"

#include <initializer_list>
#include <memory>
#include <string>

using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

const SdfFont& load(const char* file) {
    static std::vector<std::pair<std::string, std::unique_ptr<SdfFont>>> fonts;
    for (const auto& [name, f] : fonts) {
        if (name == file) return *f;
    }
    auto f = std::make_unique<SdfFont>();
    EXPECT_TRUE(f->loadFromFile(kFonts + file)) << file;
    fonts.emplace_back(file, std::move(f));
    return *fonts.back().second;
}

const SdfFont& roboto() { return load("Roboto-Regular.ttf"); }
const SdfFont& tibetan() { return load("NotoSerifTibetan.ttf"); }

std::u32string cp(std::initializer_list<char32_t> codepoints) { return std::u32string(codepoints); }

constexpr char32_t TSHEG = 0x0F0B, SHAD = 0x0F0D, NB_TSHEG = 0x0F0C;
constexpr char32_t IDEO = 0x6F22, IDEO2 = 0x5B57;         // 漢 字
constexpr char32_t FULL_STOP = 0x3002, OPEN_QUOTE = 0x300C; // 。 「

/// "bkra shis bde legs" with tsheg between the syllables and a shad at the end.
std::u32string tashiDelek() {
    return cp({0x0F56, 0x0F40, 0x0FB2, TSHEG, 0x0F64, 0x0F72, 0x0F66, TSHEG,
               0x0F56, 0x0F51, 0x0F7A, TSHEG, 0x0F63, 0x0F7A, 0x0F42, 0x0F66, SHAD});
}

float naturalWidth(const SdfFont& font, const std::u32string& text, float size = 24.0f) {
    TextStyle style;
    style.fontSize = size;
    style.wrap = false;
    return layoutText(font, text, style, {1e9f, 1e9f}).size.x;
}

TextLayoutResult wrapped(const SdfFont& font, const std::u32string& text, float width, float size = 24.0f) {
    TextStyle style;
    style.fontSize = size;
    return layoutText(font, text, style, {width, 1e9f});
}

char32_t lastOf(const std::u32string& text, const TextLine& line) {
    return text[line.firstCodepoint + line.codepointCount - 1];
}

} // namespace

// ─── The rules ──────────────────────────────────────────────────

TEST(LineBreak, OpportunitiesBetweenCharacters) {
    EXPECT_TRUE(canBreakBetween(TSHEG, 0x0F64));      // after a tsheg
    EXPECT_TRUE(canBreakBetween(SHAD, 0x0F56));       // after a shad
    EXPECT_FALSE(canBreakBetween(0x0F66, SHAD));      // never before one
    EXPECT_FALSE(canBreakBetween(NB_TSHEG, 0x0F64));  // nor after the non-breaking tsheg
    EXPECT_FALSE(canBreakBetween(0x0F40, 0x0FB2));    // nor inside a syllable

    EXPECT_TRUE(canBreakBetween(IDEO, IDEO2));        // between ideographs
    EXPECT_TRUE(canBreakBetween(U'a', IDEO));         // on either side of one
    EXPECT_FALSE(canBreakBetween(IDEO, FULL_STOP));   // never before 。
    EXPECT_FALSE(canBreakBetween(OPEN_QUOTE, IDEO));  // nor after 「
    EXPECT_TRUE(canBreakBetween(0x304B, 0x306A));     // between hiragana
    EXPECT_FALSE(canBreakBetween(0x304B, 0x3063));    // but not before a small kana

    EXPECT_TRUE(canBreakBetween(U'-', U'k'));         // after a hyphen
    EXPECT_FALSE(canBreakBetween(U'l', U'-'));
    EXPECT_FALSE(canBreakBetween(U'a', U'b'));        // Latin breaks only at spaces
    EXPECT_FALSE(canBreakBetween(U'a', U' '));        // the break is at the space itself
    EXPECT_TRUE(isBreakingSpace(U' '));
    EXPECT_TRUE(isBreakingSpace(U'\t'));
}

// ─── In layout ──────────────────────────────────────────────────

TEST(LineBreak, TibetanWrapsAfterTshegAndShad) {
    std::u32string text;
    for (int i = 0; i < 4; ++i) text += tashiDelek();
    const float syllables = naturalWidth(tibetan(), tashiDelek());   // four syllables
    const auto result = wrapped(tibetan(), text, syllables * 0.6f);

    ASSERT_GT(result.lines.size(), 4u);
    for (size_t l = 0; l + 1 < result.lines.size(); ++l) {
        const char32_t last = lastOf(text, result.lines[l]);
        EXPECT_TRUE(last == TSHEG || last == SHAD) << "line " << l << " ends with U+" << std::hex
                                                   << static_cast<uint32_t>(last);
    }
    for (const auto& line : result.lines) EXPECT_NE(text[line.firstCodepoint], SHAD);
}

TEST(LineBreak, IdeographsWrapAnywhereButNotBeforeTheFullStop) {
    // Roboto has no CJK: the ideographs are drawn as its replacement glyph,
    // all one width, and break by their code points all the same.
    const std::u32string text = cp({IDEO, IDEO2, IDEO, FULL_STOP, IDEO, IDEO2, IDEO, IDEO2});
    const float three = naturalWidth(roboto(), cp({IDEO, IDEO2, IDEO}));
    const auto result = wrapped(roboto(), text, three + 0.5f);

    ASSERT_GE(result.lines.size(), 3u);
    size_t covered = 0;
    for (const auto& line : result.lines) {
        EXPECT_NE(text[line.firstCodepoint], FULL_STOP);
        covered += line.codepointCount;
    }
    EXPECT_EQ(covered, text.size());   // nothing dropped: no spaces to drop
    // The first line gives up its last ideograph, which takes 。 along to the
    // next line rather than leave it to start one.
    EXPECT_EQ(result.lines[0].codepointCount, 2u);
    EXPECT_EQ(result.lines[1].firstCodepoint, 2u);
}

TEST(LineBreak, HyphenatedWordsBreakAfterTheHyphen) {
    const std::u32string text = U"well-known";
    const float first = naturalWidth(roboto(), U"well-");
    const float whole = naturalWidth(roboto(), text);
    const auto result = wrapped(roboto(), text, (first + whole) * 0.5f);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].codepointCount, 5u);   // "well-"
    EXPECT_EQ(result.lines[1].firstCodepoint, 5u);   // "known"
}

TEST(LineBreak, ASyllableWiderThanTheBoxStillBreaksBetweenClusters) {
    const std::u32string text = cp({0x0F56, 0x0F40, 0x0FB2, 0x0F64, 0x0F72, 0x0F66, 0x0F56, 0x0F51, 0x0F7A});
    const auto result = wrapped(tibetan(), text, 10.0f);
    ASSERT_GT(result.lines.size(), 1u);
    size_t covered = 0;
    for (const auto& line : result.lines) covered += line.codepointCount;
    EXPECT_EQ(covered, text.size());
}
