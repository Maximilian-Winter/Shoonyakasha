//
// ShapingTest.cpp - HarfBuzz shaping, font fallback and clusters
//
// Tier 1: fonts from assets/fonts, no GPU. Strings are built from code points
// because this file is not compiled as UTF-8.
//

#include <gtest/gtest.h>

#include "ECS/Core.h"
#include "UI/CanvasLayout.h"
#include "UI/FontLibrary.h"
#include "UI/TextLayout.h"
#include "UI/TextShaper.h"
#include "UI/UIBatchBuilder.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <algorithm>
#include <initializer_list>
#include <string>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

const SdfFont& font(const char* file) {
    static std::vector<std::pair<std::string, std::unique_ptr<SdfFont>>> fonts;
    for (const auto& [name, f] : fonts) {
        if (name == file) return *f;
    }
    auto f = std::make_unique<SdfFont>();
    EXPECT_TRUE(f->loadFromFile(kFonts + file)) << file;
    fonts.emplace_back(file, std::move(f));
    return *fonts.back().second;
}

const SdfFont& roboto() { return font("Roboto-Regular.ttf"); }
const SdfFont& devanagari() { return font("NotoSansDevanagari-Regular.ttf"); }
const SdfFont& tibetan() { return font("NotoSerifTibetan.ttf"); }

std::u32string cp(std::initializer_list<char32_t> codepoints) { return std::u32string(codepoints); }

std::vector<ShapedGlyph> shape(const SdfFont& f, const std::u32string& text) {
    return shapeText({&f}, text, 0, text.size());
}

// Devanagari
constexpr char32_t KA = 0x0915, SSA = 0x0937, RA = 0x0930, VIRAMA = 0x094D, SIGN_I = 0x093F;
constexpr char32_t NA = 0x0928, MA = 0x092E, SA = 0x0938, TA = 0x0924, SIGN_E = 0x0947;
// Tibetan
constexpr char32_t TIB_SA = 0x0F66, TIB_SUB_GA = 0x0F92, TIB_SUB_RA = 0x0FB2;

} // namespace

// ─── Devanagari ─────────────────────────────────────────────────

TEST(Shaping, DevanagariConjunctIsOneGlyph) {
    const auto glyphs = shape(devanagari(), cp({KA, VIRAMA, SSA}));
    ASSERT_EQ(glyphs.size(), 1u);
    EXPECT_NE(glyphs[0].glyph, devanagari().findGlyph(KA));
    EXPECT_NE(glyphs[0].glyph, devanagari().findGlyph(SSA));
    EXPECT_EQ(glyphs[0].cluster, 0u);
}

TEST(Shaping, VowelSignIIsDrawnBeforeItsConsonant) {
    const auto glyphs = shape(devanagari(), cp({KA, SIGN_I}));
    ASSERT_EQ(glyphs.size(), 2u);
    EXPECT_NE(glyphs[0].glyph, devanagari().findGlyph(KA));   // the sign, first
    EXPECT_EQ(glyphs[1].glyph, devanagari().findGlyph(KA));
    EXPECT_EQ(glyphs[0].cluster, 0u);                          // one cluster
    EXPECT_EQ(glyphs[1].cluster, 0u);
}

TEST(Shaping, RephMovesAfterTheConsonantItSitsOn) {
    const auto glyphs = shape(devanagari(), cp({RA, VIRAMA, KA}));
    ASSERT_EQ(glyphs.size(), 2u);
    EXPECT_EQ(glyphs[0].glyph, devanagari().findGlyph(KA));
    EXPECT_NE(glyphs[1].glyph, devanagari().findGlyph(RA));    // the reph form, above
}

// ─── Tibetan ────────────────────────────────────────────────────

TEST(Shaping, TibetanStackIsTheFontsStackedGlyph) {
    // Noto Serif Tibetan draws sa with subjoined ga and ra as one glyph.
    const auto glyphs = shape(tibetan(), cp({TIB_SA, TIB_SUB_GA, TIB_SUB_RA}));
    ASSERT_EQ(glyphs.size(), 1u);
    for (const char32_t c : {TIB_SA, TIB_SUB_GA, TIB_SUB_RA}) EXPECT_NE(glyphs[0].glyph, tibetan().findGlyph(c));
}

TEST(Shaping, TibetanStackHangsBelowItsBase) {
    TextStyle style;
    style.fontSize = 48.0f;
    style.wrap = false;
    auto lowest = [&](const std::u32string& text) {
        const auto result = layoutText(tibetan(), text, style, {1000.0f, 1000.0f});
        float bottom = 0.0f;
        for (const auto& g : result.glyphs) bottom = std::max(bottom, g.max.y);
        return std::pair{bottom, result.clusters.size()};
    };
    const auto [base, baseClusters] = lowest(cp({TIB_SA}));
    const auto [stack, stackClusters] = lowest(cp({TIB_SA, TIB_SUB_GA, TIB_SUB_RA}));
    EXPECT_GT(stack, base + 6.0f);   // about 9 at this size
    EXPECT_EQ(baseClusters, 1u);
    EXPECT_EQ(stackClusters, 1u);
}

// ─── Latin ──────────────────────────────────────────────────────

TEST(Shaping, KerningComesFromTheFont) {
    TextStyle style;
    style.wrap = false;
    auto width = [&](const char32_t* text) { return layoutText(roboto(), text, style, {1000, 100}).size.x; };
    EXPECT_LT(width(U"AV"), width(U"A") + width(U"V") - 0.5f);
}

// ─── Fallback ───────────────────────────────────────────────────

TEST(Shaping, CodePointsGoToTheFirstFontThatHasThem) {
    const std::u32string text = U"Hi " + cp({NA, MA, SA, VIRAMA, TA, SIGN_E});
    const auto glyphs = shapeText({&roboto(), &devanagari()}, text, 0, text.size());
    ASSERT_FALSE(glyphs.empty());
    for (const auto& g : glyphs) {
        EXPECT_EQ(g.font, g.cluster < 3 ? 0u : 1u) << "cluster " << g.cluster;
    }
}

TEST(Shaping, FallbackGlyphsMatchTheFirstFontsEmSize) {
    // Noto Serif Tibetan reserves a much taller line for its stacks than
    // Roboto; a fallback is scaled so its letters are not small beside Roboto's.
    ASSERT_GT(tibetan().heightInEms(), 1.5f * roboto().heightInEms());
    TextStyle style;
    style.fontSize = 40.0f;
    style.wrap = false;
    auto height = [&](const std::vector<const SdfFont*>& fonts) {
        const auto result = layoutText(fonts, cp({TIB_SA}), style, {1000.0f, 1000.0f});
        return result.glyphs.at(0).max.y - result.glyphs.at(0).min.y;
    };
    const float alone = height({&tibetan()});
    const float asFallback = height({&roboto(), &tibetan()});
    EXPECT_NEAR(asFallback / alone, tibetan().heightInEms() / roboto().heightInEms(), 1e-3f);
}

TEST(TextLayout, TextSizedToItsOwnWidthStaysOnOneLine) {
    TextStyle style;
    style.fontSize = 32.0f;
    const std::u32string text = U"Shunyakasha";
    const float natural = layoutText(roboto(), text, style, {1e9f, 1e9f}).size.x;
    // The width a rect of that size reports after min and max are placed.
    const float x = 22.37f;
    const float roundTrip = (x + natural) - x;
    EXPECT_EQ(layoutText(roboto(), text, style, {roundTrip, 100.0f}).lines.size(), 1u);
}

TEST(Shaping, ACodePointNoFontHasBecomesTheReplacement) {
    const std::u32string text = cp({0xE000});
    const auto glyphs = shapeText({&roboto(), &devanagari()}, text, 0, text.size());
    ASSERT_EQ(glyphs.size(), 1u);
    EXPECT_EQ(glyphs[0].glyph, roboto().glyphForCodepoint(0xE000));
}

TEST(FontLibrary, ChainsFallbacksOnceEachAndSkipsUnloadedFonts) {
    FontLibrary fonts;
    const uint32_t a = fonts.load(kFonts + "Roboto-Regular.ttf");
    const uint32_t b = fonts.load(kFonts + "NotoSansDevanagari-Regular.ttf");
    const uint32_t c = fonts.load(kFonts + "NotoSerifTibetan.ttf");
    ASSERT_TRUE(a && b && c);

    EXPECT_EQ(fonts.chain(a), (std::vector<uint32_t>{a}));
    fonts.setDefaultFallbacks({b, c});
    EXPECT_EQ(fonts.chain(a), (std::vector<uint32_t>{a, b, c}));
    fonts.setFallbacks(a, {c, a, 99, c});
    EXPECT_EQ(fonts.chain(a), (std::vector<uint32_t>{a, c}));
    EXPECT_EQ(fonts.chain(b), (std::vector<uint32_t>{b, c}));
    EXPECT_TRUE(fonts.chain(99).empty());
}

// ─── Layout over clusters ───────────────────────────────────────

TEST(TextLayout, ClustersCoverTheTextAndLinesNeverSplitThem) {
    // Four conjuncts and a vowel sign, wrapped one conjunct at a time.
    std::u32string text;
    for (int i = 0; i < 4; ++i) text += cp({KA, VIRAMA, SSA});
    text += cp({KA, SIGN_I});
    TextStyle style;
    style.fontSize = 32.0f;
    const auto result = layoutText(devanagari(), text, style, {20.0f, 1000.0f});

    EXPECT_EQ(result.clusters.size(), 5u);
    size_t covered = 0;
    for (const auto& c : result.clusters) covered += c.codepointCount;
    EXPECT_EQ(covered, text.size());
    ASSERT_EQ(result.lines.size(), 5u);
    for (size_t l = 0; l < result.lines.size(); ++l) {
        EXPECT_EQ(result.lines[l].firstCodepoint, result.clusters[l].firstCodepoint);
        EXPECT_EQ(result.clusters[l].line, l);
    }
}

TEST(TextLayout, ClusterPositionsFollowAlignment) {
    TextStyle style;
    style.fontSize = 24.0f;
    style.alignH = TextAlignH::Right;
    const auto result = layoutText(roboto(), U"ab", style, {300.0f, 50.0f});
    ASSERT_EQ(result.clusters.size(), 2u);
    EXPECT_NEAR(result.clusters[1].x + result.clusters[1].width, 300.0f, 1e-3f);
    EXPECT_NEAR(result.clusters[0].x + result.clusters[0].width, result.clusters[1].x, 1e-3f);
}

// ─── Through the systems ────────────────────────────────────────

TEST(Shaping, CanvasTextDrawsFallbackGlyphsFromTheirOwnFont) {
    entt::registry registry;
    FontLibrary fonts;
    GlyphCache glyphs(512);
    UIDrawData data;
    const uint32_t latin = fonts.load(kFonts + "Roboto-Regular.ttf");
    const uint32_t deva = fonts.load(kFonts + "NotoSansDevanagari-Regular.ttf");
    fonts.setFallbacks(latin, {deva});

    const entt::entity canvas = registry.create();
    registry.emplace<UICanvas>(canvas);
    const entt::entity label = createElement(registry, canvas, {400.0f, 60.0f});
    auto& text = registry.emplace<UIText>(label);
    text.text = "Om \xE0\xA4\x93\xE0\xA4\x82";   // "Om " then Devanagari O and anusvara, UTF-8
    text.font = latin;

    const glm::vec2 screen(800.0f, 600.0f);
    CanvasLayoutSystem(&screen, &fonts).update(registry, 0.0f);
    CanvasTextSystem(&fonts).update(registry, 0.0f);
    const auto& cache = registry.get<UITextCache>(label);
    EXPECT_EQ(cache.fonts, (std::vector<uint32_t>{latin, deva}));
    const bool usesFallback = std::any_of(cache.layout.glyphs.begin(), cache.layout.glyphs.end(),
                                          [](const PositionedGlyph& g) { return g.font == 1; });
    EXPECT_TRUE(usesFallback);

    buildDrawData(registry, fonts, glyphs, data);
    EXPECT_EQ(data.indices.size(), cache.layout.glyphs.size() * 6);
    EXPECT_EQ(glyphs.glyphCount(), cache.layout.glyphs.size());   // each glyph once, from its own font
}
