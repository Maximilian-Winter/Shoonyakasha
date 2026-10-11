//
// LineBreakTest.cpp - Script-aware line breaking
//
// Tier 1: fonts from assets/fonts, no GPU. Strings are built from code points
// because this file is not compiled as UTF-8.
//

#include <gtest/gtest.h>

#include "UI/LineBreak.h"
#include "UI/TextLayout.h"
#include "UI/WordDictionary.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <sstream>
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

/// Whether a line may break between `before` and `after`, alone.
bool canBreak(char32_t before, char32_t after) {
    return lineBreaks(cp({before, after}), 0, 2)[0] != LineBreakAfter::None;
}

/// The tests of a Unicode break test file in tests/data: each code point,
/// and whether a break follows it.
struct BreakCase {
    std::string line;
    std::u32string text;
    std::vector<bool> breakAfter;
};

std::vector<BreakCase> readBreakTests(const std::string& file) {
    std::vector<BreakCase> cases;
    std::ifstream in(std::string(SHOONYAKASHA_SOURCE_DIR) + "/tests/data/" + file);
    EXPECT_TRUE(in.good()) << file;
    const std::string kBreak = "\xC3\xB7", kNoBreak = "\xC3\x97";   // ÷ and ×
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        BreakCase c;
        c.line = line;
        std::istringstream tokens(line.substr(0, line.find('#')));
        for (std::string token; tokens >> token;) {
            if (token == kBreak || token == kNoBreak) {
                if (!c.text.empty()) c.breakAfter.push_back(token == kBreak);
            } else {
                c.text.push_back(static_cast<char32_t>(std::stoul(token, nullptr, 16)));
            }
        }
        if (!c.text.empty() && c.breakAfter.size() == c.text.size()) cases.push_back(std::move(c));
    }
    return cases;
}

} // namespace

// ─── The rules ──────────────────────────────────────────────────

TEST(LineBreak, OpportunitiesBetweenCharacters) {
    EXPECT_TRUE(canBreak(TSHEG, 0x0F64));      // after a tsheg
    EXPECT_TRUE(canBreak(SHAD, 0x0F56));       // after a shad
    EXPECT_FALSE(canBreak(0x0F66, SHAD));      // never before one
    EXPECT_FALSE(canBreak(NB_TSHEG, 0x0F64));  // nor after the non-breaking tsheg
    EXPECT_FALSE(canBreak(0x0F40, 0x0FB2));    // nor inside a syllable

    EXPECT_TRUE(canBreak(IDEO, IDEO2));        // between ideographs
    EXPECT_TRUE(canBreak(U'a', IDEO));         // on either side of one
    EXPECT_FALSE(canBreak(IDEO, FULL_STOP));   // never before 。
    EXPECT_FALSE(canBreak(OPEN_QUOTE, IDEO));  // nor after 「
    EXPECT_TRUE(canBreak(0x304B, 0x306A));     // between hiragana
    EXPECT_FALSE(canBreak(0x304B, 0x3063));    // but not before a small kana

    EXPECT_TRUE(canBreak(U'-', U'k'));         // after a hyphen
    EXPECT_FALSE(canBreak(U'l', U'-'));
    EXPECT_FALSE(canBreak(U'a', U'b'));        // Latin breaks only at spaces
    EXPECT_FALSE(canBreak(U'a', U' '));        // the break is after the space
    EXPECT_TRUE(canBreak(U' ', U'b'));
    EXPECT_FALSE(canBreak(U'1', U','));        // numbers keep their punctuation
    EXPECT_TRUE(isBreakingSpace(U' '));
    EXPECT_TRUE(isBreakingSpace(U'\t'));
}

TEST(LineBreak, MandatoryBreaks) {
    const auto breaks = lineBreaks(cp({U'a', 0x2028, U'b', U'\r', U'c', 0x000B, U'd'}), 0, 7);
    EXPECT_EQ(breaks[1], LineBreakAfter::Mandatory);   // after the line separator
    EXPECT_EQ(breaks[3], LineBreakAfter::Mandatory);   // after a lone carriage return
    EXPECT_EQ(breaks[5], LineBreakAfter::Mandatory);   // after a line tabulation
    EXPECT_EQ(breaks[0], LineBreakAfter::None);
    EXPECT_EQ(breaks.back(), LineBreakAfter::Mandatory);   // the end of the text
}

TEST(LineBreak, ConformsToASampleOfLineBreakTest) {
    const auto cases = readBreakTests("LineBreakTest-sample.txt");
    ASSERT_GT(cases.size(), 250u);
    for (const BreakCase& c : cases) {
        const auto breaks = lineBreaks(c.text, 0, c.text.size());
        for (size_t i = 0; i < c.text.size(); ++i) {
            EXPECT_EQ(breaks[i] != LineBreakAfter::None, c.breakAfter[i]) << "after code point " << i << " of " << c.line;
        }
    }
}

TEST(WordBreak, ConformsToASampleOfWordBreakTest) {
    const auto cases = readBreakTests("WordBreakTest-sample.txt");
    ASSERT_GT(cases.size(), 250u);
    for (const BreakCase& c : cases) {
        const auto boundaries = wordBoundaries(c.text);
        for (size_t i = 0; i < c.text.size(); ++i) {
            EXPECT_EQ(boundaries[i], c.breakAfter[i]) << "after code point " << i << " of " << c.line;
        }
    }
}

// ─── Dictionary scripts ─────────────────────────────────────────

namespace {

// "chan rak maew", I love cats: three Thai words, no spaces.
std::u32string thaiILoveCats() {
    return cp({0x0E09, 0x0E31, 0x0E19, 0x0E23, 0x0E31, 0x0E01, 0x0E41, 0x0E21, 0x0E27});
}

} // namespace

TEST(WordDictionary, SplitsIntoTheFewestUnknownCharactersThenTheFewestWords) {
    WordDictionary d;
    for (const char32_t* w : {U"ab", U"abc", U"cd"}) d.add(w);
    EXPECT_EQ(d.split(U"abcd"), (std::vector<size_t>{2}));       // ab|cd beats abc|d
    EXPECT_EQ(d.split(U"xyab"), (std::vector<size_t>{2}));       // xy unknown, kept together
    EXPECT_EQ(d.split(U"abxyab"), (std::vector<size_t>{2, 4}));
    EXPECT_TRUE(d.split(U"zz").empty());

    // A known word may not end inside a unit: here "a" and "b" are one.
    WordDictionary single;
    single.add(U"a");
    const std::vector<bool> units{false, true, true};
    EXPECT_TRUE(single.split(U"abc", &units).empty());
}

TEST(WordDictionary, TheShippedListsLoad) {
    for (const auto script : {DictionaryScript::Thai, DictionaryScript::Lao, DictionaryScript::Khmer,
                              DictionaryScript::Myanmar}) {
        const WordDictionary* d = wordDictionary(script);
        ASSERT_NE(d, nullptr);
        EXPECT_GT(d->size(), 20000u);
    }
    EXPECT_EQ(dictionaryScriptOf(0x0E01), DictionaryScript::Thai);
    EXPECT_EQ(dictionaryScriptOf(0x0E81), DictionaryScript::Lao);
    EXPECT_EQ(dictionaryScriptOf(0x1780), DictionaryScript::Khmer);
    EXPECT_EQ(dictionaryScriptOf(0x1000), DictionaryScript::Myanmar);
    EXPECT_FALSE(dictionaryScriptOf(U'a').has_value());
}

TEST(LineBreak, ThaiBreaksBetweenWords) {
    const std::u32string text = thaiILoveCats();
    const auto breaks = lineBreaks(text, 0, text.size());
    std::vector<size_t> at;
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        if (breaks[i] != LineBreakAfter::None) at.push_back(i + 1);
    }
    EXPECT_EQ(at, (std::vector<size_t>{3, 6}));

    // Without a word list, the run does not break.
    setWordDictionary(DictionaryScript::Thai, std::make_shared<WordDictionary>());
    const auto plain = lineBreaks(text, 0, text.size());
    EXPECT_TRUE(std::all_of(plain.begin(), plain.end() - 1, [](LineBreakAfter b) { return b == LineBreakAfter::None; }));
    setWordDictionary(DictionaryScript::Thai, nullptr);
    EXPECT_NE(wordDictionary(DictionaryScript::Thai)->size(), 0u);   // the shipped list again
}

TEST(LineBreak, ThaiWrapsAtWords) {
    const SdfFont& thai = load("NotoSansThai-Regular.ttf");
    const std::u32string text = thaiILoveCats();
    const float twoWords = naturalWidth(thai, text.substr(0, 6));
    const auto result = wrapped(thai, text, twoWords + 0.5f);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[0].codepointCount, 6u);
    EXPECT_EQ(result.lines[1].firstCodepoint, 6u);
}

TEST(LineBreak, ALineSeparatorEndsTheLine) {
    const std::u32string text = cp({U'a', U'b', 0x2028, U'c', U'd'});
    TextStyle style;
    style.wrap = false;
    const auto result = layoutText(roboto(), text, style, {1000.0f, 1000.0f});
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1].firstCodepoint, 3u);
    EXPECT_GT(result.lines[1].baseline, result.lines[0].baseline);
}

TEST(LineBreak, KhmerBreaksBetweenWords) {
    // "khnhom srolanh neak", I love you: three words, the first and the last
    // with a subscript consonant (coeng, U+17D2).
    const std::u32string text = cp({0x1781, 0x17D2, 0x1789, 0x17BB, 0x17C6,
                                    0x179F, 0x17D2, 0x179A, 0x179B, 0x17B6, 0x1789, 0x17CB,
                                    0x17A2, 0x17D2, 0x1793, 0x1780});
    const auto breaks = lineBreaks(text, 0, text.size());
    std::vector<size_t> at;
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        if (breaks[i] != LineBreakAfter::None) at.push_back(i + 1);
    }
    EXPECT_EQ(at, (std::vector<size_t>{5, 12}));

    // Laid out with the shipped Khmer font, a box two words wide wraps there.
    const SdfFont& khmer = load("NotoSansKhmer-Regular.ttf");
    const auto result = wrapped(khmer, text, naturalWidth(khmer, text.substr(0, 12)) + 0.5f);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1].firstCodepoint, 12u);
}

TEST(WordBreak, ThaiWordsForEditing) {
    const std::vector<bool> ends = wordBoundaries(thaiILoveCats());
    std::vector<size_t> at;
    for (size_t i = 0; i < ends.size(); ++i) {
        if (ends[i]) at.push_back(i + 1);
    }
    EXPECT_EQ(at, (std::vector<size_t>{3, 6, 9}));
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
