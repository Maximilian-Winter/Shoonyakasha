//
// BidiTest.cpp - Bidirectional text: levels, display order, caret and selection
//
// Tier 1 and 2: SheenBidi and HarfBuzz with the shipped fonts, then text
// fields through the systems. Hebrew and Arabic letters are written as code
// points so the order of the source is plain.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/Bidi.h"
#include "UI/FontLibrary.h"
#include "UI/TextEditing.h"
#include "UI/TextLayout.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"
#include "UI/Utf8.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

constexpr char32_t ALEF = 0x05D0, BET = 0x05D1, GIMEL = 0x05D2, DALET = 0x05D3, HE = 0x05D4, VAV = 0x05D5;
constexpr char32_t AIN = 0x0639, DAL = 0x062F;
constexpr char32_t RLO = 0x202E, PDF = 0x202C, RLI = 0x2067, PDI = 0x2069;

const SdfFont& font(const std::string& name) {
    static std::vector<std::unique_ptr<SdfFont>> fonts;
    static std::vector<std::string> names;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return *fonts[i];
    }
    auto f = std::make_unique<SdfFont>();
    f->loadFromFile(kFonts + name);
    fonts.push_back(std::move(f));
    names.push_back(name);
    return *fonts.back();
}

const SdfFont& roboto() { return font("Roboto-Regular.ttf"); }
const SdfFont& hebrew() { return font("NotoSansHebrew-Regular.ttf"); }
const SdfFont& arabic() { return font("NotoSansArabic-Regular.ttf"); }

std::vector<const SdfFont*> chain() { return {&roboto(), &hebrew(), &arabic()}; }

TextLayoutResult lay(std::u32string_view text, TextStyle style = {}, glm::vec2 box = {800.0f, 100.0f}) {
    style.wrap = false;
    return layoutText(chain(), text, style, box);
}

/// Each cluster's first code point, in display order.
std::vector<size_t> displayOrder(const TextLayoutResult& layout) {
    std::vector<size_t> order;
    for (const TextCluster& c : layout.clusters) order.push_back(c.firstCodepoint);
    return order;
}

} // namespace

// ─── Levels and runs ────────────────────────────────────────────

TEST(Bidi, ShippedFontsLoad) {
    EXPECT_TRUE(hebrew().valid());
    EXPECT_TRUE(arabic().valid());
    EXPECT_NE(hebrew().findGlyph(ALEF), 0);
    EXPECT_NE(arabic().findGlyph(AIN), 0);
}

TEST(Bidi, LevelsOfMixedText) {
    const std::u32string text{U'a', U'b', U'c', U' ', ALEF, BET, GIMEL};
    BidiParagraph latinFirst(text, 0, text.size(), TextDirection::Auto);
    EXPECT_EQ(latinFirst.baseLevel(), 0);
    EXPECT_EQ(latinFirst.levels(), (std::vector<uint8_t>{0, 0, 0, 0, 1, 1, 1}));

    const std::u32string hebrewFirst{ALEF, BET, U' ', U'a', U'b'};
    BidiParagraph rtl(hebrewFirst, 0, hebrewFirst.size(), TextDirection::Auto);
    EXPECT_TRUE(rtl.rightToLeft());
    EXPECT_EQ(rtl.levels(), (std::vector<uint8_t>{1, 1, 1, 2, 2}));

    // Forced, the same text reads left to right.
    BidiParagraph forced(hebrewFirst, 0, hebrewFirst.size(), TextDirection::LeftToRight);
    EXPECT_EQ(forced.baseLevel(), 0);
    EXPECT_EQ(forced.levels(), (std::vector<uint8_t>{1, 1, 0, 0, 0}));
}

TEST(Bidi, NumbersInArabicReadLeftToRight) {
    const std::u32string text{AIN, DAL, DAL, U' ', U'1', U'2', U'3'};
    BidiParagraph p(text, 0, text.size(), TextDirection::Auto);
    EXPECT_TRUE(p.rightToLeft());
    EXPECT_EQ(p.levels(), (std::vector<uint8_t>{1, 1, 1, 1, 2, 2, 2}));
}

TEST(Bidi, PlainLatinIsAllLevelZeroInOneRun) {
    const std::u32string text = U"plain text, 42";
    BidiParagraph p(text, 0, text.size(), TextDirection::Auto);
    EXPECT_EQ(p.baseLevel(), 0);
    EXPECT_TRUE(std::all_of(p.levels().begin(), p.levels().end(), [](uint8_t l) { return l == 0; }));
    const auto runs = p.lineRuns(0, text.size());
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].begin, 0u);
    EXPECT_EQ(runs[0].end, text.size());

    // Right to left forced: Latin is embedded at level 2.
    BidiParagraph forced(text, 0, text.size(), TextDirection::RightToLeft);
    EXPECT_EQ(forced.baseLevel(), 1);
    EXPECT_EQ(forced.levels()[0], 2);
}

TEST(Bidi, LineRunsComeInDisplayOrder) {
    // A right-to-left paragraph: Hebrew, a Latin word, Hebrew.
    const std::u32string text{ALEF, BET, GIMEL, U' ', U'a', U'b', U'c', U' ', DALET, HE, VAV};
    BidiParagraph p(text, 0, text.size(), TextDirection::Auto);
    const auto runs = p.lineRuns(0, text.size());
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].begin, 7u);   // the last Hebrew word, with the space before it, is leftmost
    EXPECT_EQ(runs[1].begin, 4u);
    EXPECT_EQ(runs[1].level, 2);
    EXPECT_EQ(runs[2].begin, 0u);

    // Runs of a line within the paragraph.
    const auto tail = p.lineRuns(4, 7);
    ASSERT_EQ(tail.size(), 1u);
    EXPECT_EQ(tail[0].begin, 4u);
    EXPECT_EQ(tail[0].end, 7u);
}

// ─── Layout ─────────────────────────────────────────────────────

TEST(BidiLayout, HebrewInALatinLineReadsRightToLeft) {
    const std::u32string text{U'a', U'b', U'c', U' ', ALEF, BET, GIMEL};
    const TextLayoutResult layout = lay(text);
    EXPECT_EQ(displayOrder(layout), (std::vector<size_t>{0, 1, 2, 3, 6, 5, 4}));
    EXPECT_FALSE(layout.lines[0].rightToLeft);
    for (size_t k = 1; k < layout.clusters.size(); ++k) {
        EXPECT_NEAR(layout.clusters[k].x, layout.clusters[k - 1].x + layout.clusters[k - 1].width, 1e-3f);
    }
    EXPECT_TRUE(layout.clusters.back().rightToLeft());
    EXPECT_FALSE(layout.clusters.front().rightToLeft());
}

TEST(BidiLayout, ArabicWithNumbersAndASpace) {
    const std::u32string text{AIN, DAL, DAL, U' ', U'1', U'2', U'3'};
    const TextLayoutResult layout = lay(text);
    EXPECT_TRUE(layout.lines[0].rightToLeft);
    // The number, left to right, then the space and the word from its end.
    EXPECT_EQ(displayOrder(layout), (std::vector<size_t>{4, 5, 6, 3, 2, 1, 0}));
}

TEST(BidiLayout, BracketsAreMirroredInRightToLeftText) {
    const std::u32string text{U'(', ALEF, U')'};
    const TextLayoutResult layout = lay(text, [] { TextStyle s; s.direction = TextDirection::RightToLeft; return s; }());
    ASSERT_EQ(layout.glyphs.size(), 3u);
    // Shown left to right: the closing bracket, alef, the opening bracket;
    // each bracket drawn as its mirror, so the pair still encloses alef.
    EXPECT_EQ(displayOrder(layout), (std::vector<size_t>{2, 1, 0}));
    const auto fonts = chain();
    const PositionedGlyph& left = layout.glyphs.front();
    const PositionedGlyph& right = layout.glyphs.back();
    EXPECT_EQ(left.glyph, fonts[left.font]->findGlyph(U'('));
    EXPECT_EQ(right.glyph, fonts[right.font]->findGlyph(U')'));
}

TEST(BidiLayout, OverridesAndIsolates) {
    // RLO ... PDF shows Latin backwards.
    const std::u32string overridden{RLO, U'a', U'b', U'c', PDF};
    std::vector<size_t> letters;
    for (const size_t i : displayOrder(lay(overridden))) {
        if (i >= 1 && i <= 3) letters.push_back(i);
    }
    EXPECT_EQ(letters, (std::vector<size_t>{3, 2, 1}));

    // An isolate keeps a Hebrew phrase in its own place between Latin words.
    const std::u32string isolated{U'x', U' ', RLI, ALEF, BET, PDI, U' ', U'y'};
    std::vector<size_t> shown;
    for (const size_t i : displayOrder(lay(isolated))) {
        if (isolated[i] != RLI && isolated[i] != PDI) shown.push_back(i);
    }
    EXPECT_EQ(shown, (std::vector<size_t>{0, 1, 4, 3, 6, 7}));
}

TEST(BidiLayout, StartAndEndFollowTheParagraph) {
    TextStyle style;
    style.alignH = TextAlignH::Start;
    const TextLayoutResult latin = lay(U"abc", style);
    EXPECT_NEAR(latin.clusters.front().x, 0.0f, 1e-3f);

    const std::u32string hebrewText{ALEF, BET, GIMEL};
    const TextLayoutResult start = lay(hebrewText, style);
    EXPECT_NEAR(start.clusters.back().x + start.clusters.back().width, 800.0f, 1e-3f);   // at the right

    style.alignH = TextAlignH::End;
    EXPECT_NEAR(lay(hebrewText, style).clusters.front().x, 0.0f, 1e-3f);
    EXPECT_NEAR(lay(U"abc", style).clusters.back().x + lay(U"abc", style).clusters.back().width, 800.0f, 1e-3f);

    // Left stays left in a right-to-left paragraph.
    style.alignH = TextAlignH::Left;
    EXPECT_NEAR(lay(hebrewText, style).clusters.front().x, 0.0f, 1e-3f);
}

TEST(BidiLayout, TrailingSpacesGoAtTheParagraphsEnd) {
    TextStyle style;
    style.alignH = TextAlignH::Start;
    const std::u32string text{ALEF, BET, U' ', U' '};
    const TextLayoutResult layout = lay(text, style);
    // Shown left to right: the two spaces, then bet and alef against the right edge.
    EXPECT_EQ(displayOrder(layout), (std::vector<size_t>{3, 2, 1, 0}));
    EXPECT_NEAR(layout.clusters.back().x + layout.clusters.back().width, 800.0f, 1e-3f);
    EXPECT_NEAR(layout.lines[0].width, layout.clusters[2].width + layout.clusters[3].width, 1e-3f);
}

TEST(BidiLayout, WrappedRightToLeftLinesBreakInTypingOrder) {
    // Three Hebrew words; the box fits two.
    const std::u32string text{ALEF, BET, U' ', GIMEL, DALET, U' ', HE, VAV};
    TextStyle style;
    style.alignH = TextAlignH::Start;
    const TextLayoutResult one = layoutText(chain(), text, style, {1000.0f, 100.0f});
    float twoWords = 0.0f;
    for (const TextCluster& c : one.clusters) {
        if (c.firstCodepoint < 5) twoWords += c.width;
    }
    const TextLayoutResult wrapped = layoutText(chain(), text, style, {twoWords + 1.0f, 100.0f});
    ASSERT_EQ(wrapped.lines.size(), 2u);
    EXPECT_EQ(wrapped.lines[0].firstCodepoint, 0u);   // the first words typed are on the first line
    EXPECT_EQ(wrapped.lines[1].firstCodepoint, 6u);
}

// ─── Caret ──────────────────────────────────────────────────────

TEST(BidiCaret, StandsOnTheMoreDeeplyEmbeddedSide) {
    // a b c ' ' | gimel bet alef, shown left to right.
    const std::u32string text{U'a', U'b', U'c', U' ', ALEF, BET, GIMEL};
    const TextLayoutResult layout = lay(text);
    const TextCluster& space = layout.clusters[3];
    const TextCluster& gimel = layout.clusters[4];
    const TextCluster& alef = layout.clusters[6];

    EXPECT_NEAR(caretX(layout, 0), 0.0f, 1e-3f);
    // Before alef, after the space: at alef's leading edge, its right.
    EXPECT_NEAR(caretX(layout, 4), alef.x + alef.width, 1e-3f);
    // At the end, after gimel: its trailing edge, its left, just after the space.
    EXPECT_NEAR(caretX(layout, 7), gimel.x, 1e-3f);
    EXPECT_NEAR(gimel.x, space.x + space.width, 1e-3f);

    // In display order: 0 1 2 3, then the Hebrew from its end, then 4 at the right.
    std::vector<size_t> visual;
    for (const auto& [index, x] : caretPositions(layout, text.size())) visual.push_back(index);
    EXPECT_EQ(visual, (std::vector<size_t>{0, 1, 2, 3, 7, 6, 5, 4}));

    EXPECT_EQ(caretAt(layout, text.size(), alef.x + alef.width + 50.0f), 4u);
    EXPECT_EQ(caretAt(layout, text.size(), gimel.x + 0.1f), 7u);
}

TEST(BidiCaret, LeftAndRightMoveInDisplayOrder) {
    TextEditBuffer b;
    b.text = U"abc ";
    b.text += std::u32string{ALEF, BET, GIMEL};
    b.visual = {0, 1, 2, 3, 7, 6, 5, 4};
    EXPECT_EQ(b.rightStop(3), 7u);
    EXPECT_EQ(b.rightStop(7), 6u);
    EXPECT_EQ(b.rightStop(4), 4u);   // the right end
    EXPECT_EQ(b.leftStop(4), 5u);
    EXPECT_EQ(b.leftStop(0), 0u);

    // An edit makes the order stale: moves fall back to typing order.
    b.caret = b.anchor = 7;
    b.insert(U"x");
    EXPECT_TRUE(b.visual.empty());
    EXPECT_EQ(b.leftStop(8), 7u);
}

// ─── Text fields ────────────────────────────────────────────────

namespace {

struct FieldFixture {
    entt::registry registry;
    FontLibrary fonts;
    glm::vec2 screen{800.0f, 600.0f};
    UIPointerState pointer;
    UIFocusState focus;
    UIClipboard clipboard;
    CanvasInputSystem input{&screen, nullptr, &pointer, &focus, &clipboard};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();
    entt::entity field = entt::null;

    FieldFixture() {
        const uint32_t latin = fonts.load(kFonts + "Roboto-Regular.ttf");
        fonts.setFallbacks(latin, {fonts.load(kFonts + "NotoSansHebrew-Regular.ttf")});
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas);
        field = createTextField(registry, canvas, "", latin, {400.0f, 36.0f});
        auto& rect = registry.get<UIRect>(field);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = {100.0f, 100.0f};
        step();
        focus.focused = field;   // once it is laid out, so it can take focus
    }

    ECS::InputStateComponent& state() { return registry.get<ECS::InputStateComponent>(inputEntity); }
    UITextField& text() { return registry.get<UITextField>(field); }

    void step(std::vector<ECS::InputStateComponent::KeyPress> keys = {}) {
        state().keyPresses = std::move(keys);
        input.update(registry, 0.1f);
        CanvasLayoutSystem(&screen, &fonts).update(registry, 0.1f);
        CanvasTextSystem(&fonts).update(registry, 0.1f);
        state().keyPresses.clear();
    }
    void key(int key, int mods = 0) { step({{key, mods, false, 0}}); }
};

} // namespace

TEST(BidiTextField, ArrowsFollowTheDisplay) {
    FieldFixture f;
    f.text().text = encodeUtf8(std::u32string{U'a', U'b', U' ', ALEF, BET});
    f.text().caret = f.text().selectionAnchor = 2;
    f.step();
    f.key(GLFW_KEY_RIGHT);
    EXPECT_EQ(f.text().caret, 5u);   // past the space: the end, shown left of bet
    f.key(GLFW_KEY_RIGHT);
    EXPECT_EQ(f.text().caret, 4u);
    f.key(GLFW_KEY_RIGHT);
    EXPECT_EQ(f.text().caret, 3u);   // the right end
    f.key(GLFW_KEY_LEFT);
    EXPECT_EQ(f.text().caret, 4u);
}

TEST(BidiTextField, ARightToLeftFieldAlignsRightAndMovesByWordWithRight) {
    FieldFixture f;
    f.text().text = encodeUtf8(std::u32string{ALEF, BET, U' ', GIMEL, DALET});
    f.text().caret = f.text().selectionAnchor = 5;
    f.step();
    const auto& cache = f.registry.get<UITextCache>(f.text().textElement);
    ASSERT_TRUE(cache.layout.lines[0].rightToLeft);
    const float right = cache.layout.clusters.back().x + cache.layout.clusters.back().width;
    EXPECT_NEAR(right, f.registry.get<UIRect>(f.text().textElement).rect.size().x, 1e-3f);

    f.key(GLFW_KEY_RIGHT, GLFW_MOD_CONTROL);   // towards the start of the text
    EXPECT_EQ(f.text().caret, 3u);
    f.key(GLFW_KEY_LEFT, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().caret, 5u);
}

TEST(BidiTextField, ASelectionAcrossADirectionChangeIsTwoRects) {
    FieldFixture f;
    f.text().text = encodeUtf8(std::u32string{U'a', U'b', U' ', ALEF, BET, GIMEL});
    f.text().selectionAnchor = 1;   // b, the space and alef
    f.text().caret = 4;
    f.step();
    const auto& rects = f.registry.get<UIImageRects>(f.text().selectionElement).rects;
    ASSERT_EQ(rects.size(), 2u);
    // b and the space side by side on the left; alef alone at the right end.
    const auto& cache = f.registry.get<UITextCache>(f.text().textElement);
    const float left = f.registry.get<UIRect>(f.text().textElement).rect.min.x;
    const TextCluster& alef = cache.layout.clusters.back();
    EXPECT_NEAR(rects[1].min.x, left + alef.x, 1e-3f);
    EXPECT_NEAR(rects[1].max.x, left + alef.x + alef.width, 1e-3f);
    EXPECT_LT(rects[0].max.x, rects[1].min.x);

    // Left without Shift collapses the selection to its end shown on the left.
    f.key(GLFW_KEY_LEFT);
    EXPECT_EQ(f.text().caret, 1u);
    EXPECT_EQ(f.text().selectionAnchor, 1u);
}
