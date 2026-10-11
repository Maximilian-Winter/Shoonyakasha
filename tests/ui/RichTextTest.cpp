//
// RichTextTest.cpp - Markup in UIText: parsing, fonts, layout, drawing and links
//
// Tier 2: EnTT registry and fonts, no GPU.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/FontLibrary.h"
#include "UI/RichText.h"
#include "UI/UIBatchBuilder.h"
#include "UI/UIHitTest.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"
#include "UI/Utf8.h"

#include <GLFW/glfw3.h>

#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

} // namespace

// ─── Parsing ────────────────────────────────────────────────────

TEST(RichTextParse, NestedTagsSetEachStretch) {
    const RichText r = parseRichText(U"<b>a<i>b</i>c</b>d");
    EXPECT_EQ(r.text, U"abcd");
    ASSERT_EQ(r.spans.size(), 4u);
    EXPECT_TRUE(r.spans[0].style.bold);
    EXPECT_FALSE(r.spans[0].style.italic);
    EXPECT_TRUE(r.spans[1].style.bold);
    EXPECT_TRUE(r.spans[1].style.italic);
    EXPECT_TRUE(r.spans[2].style.bold);
    EXPECT_FALSE(r.spans[2].style.italic);
    EXPECT_EQ(r.spans[3].style, RichTextStyle{});
    EXPECT_EQ(r.spans[3].begin, 3u);
    EXPECT_EQ(r.spans[3].end, 4u);
}

TEST(RichTextParse, AClosingTagEndsTheMostRecentOfItsName) {
    const RichText r = parseRichText(U"<b>a<i>b</b>c</i>");
    EXPECT_EQ(r.text, U"abc");
    ASSERT_EQ(r.spans.size(), 3u);
    EXPECT_TRUE(r.spans[1].style.bold && r.spans[1].style.italic);
    EXPECT_FALSE(r.spans[2].style.bold);
    EXPECT_TRUE(r.spans[2].style.italic);
}

TEST(RichTextParse, UnclosedTagsRunToTheEnd) {
    const RichText r = parseRichText(U"<u><s>ab");
    EXPECT_EQ(r.text, U"ab");
    ASSERT_EQ(r.spans.size(), 1u);
    EXPECT_TRUE(r.spans[0].style.underline);
    EXPECT_TRUE(r.spans[0].style.strikethrough);
    EXPECT_EQ(r.spans[0].end, 2u);
}

TEST(RichTextParse, MalformedTagsAreText) {
    for (const std::u32string markup : {U"<color=red>x", U"<color=#12345>x", U"<foo>x", U"a < b", U"<b=1>x",
                                        U"<size=0>x", U"<size=big>x", U"</b>x", U"<i", U"<link>x", U"<u\n>x",
                                        U"</u=1>x"}) {
        const RichText r = parseRichText(markup);
        EXPECT_EQ(r.text, markup) << encodeUtf8(markup);
        ASSERT_EQ(r.spans.size(), 1u);
        EXPECT_EQ(r.spans[0].style, RichTextStyle{});
    }
}

TEST(RichTextParse, EscapesValuesAndLinks) {
    const RichText r = parseRichText(
        U"&lt;b&gt; &amp; <color=#FF000080>r</color><size=12.5>s</size><link=\"go home\">l</link><font=Serif>f</font>");
    EXPECT_EQ(r.text, U"<b> &amp; rslf");
    ASSERT_EQ(r.spans.size(), 5u);
    EXPECT_EQ(r.spans[0].style, RichTextStyle{});   // the escaped brackets are no tag
    EXPECT_TRUE(r.spans[1].style.hasColor);
    EXPECT_NEAR(r.spans[1].style.color.r, 1.0f, 1e-6f);
    EXPECT_NEAR(r.spans[1].style.color.g, 0.0f, 1e-6f);
    EXPECT_NEAR(r.spans[1].style.color.a, 128.0f / 255.0f, 1e-6f);
    EXPECT_FLOAT_EQ(r.spans[2].style.size, 12.5f);
    EXPECT_EQ(r.spans[3].style.link, 0);
    ASSERT_EQ(r.links.size(), 1u);
    EXPECT_EQ(r.links[0], "go home");
    EXPECT_EQ(r.spans[4].style.font, "Serif");
}

// ─── Fonts ──────────────────────────────────────────────────────

TEST(RichTextPrepare, FontsByNameAndBoldAndItalicByStyle) {
    FontLibrary fonts;
    const uint32_t roboto = fonts.load(kFonts + "Roboto-Regular.ttf");
    const uint32_t playfair = fonts.load(kFonts + "PlayfairDisplay.ttf");
    const uint32_t hebrew = fonts.load(kFonts + "NotoSansHebrew-Regular.ttf");
    fonts.setName(playfair, "serif");

    // By a name given, by file name, and an unknown name keeping the text's font.
    const PreparedText p =
        prepareText("a<font=serif>b</font><font=NotoSansHebrew-Regular>c</font><font=nope>d</font>", true, roboto,
                    20.0f, fonts);
    EXPECT_EQ(p.text, U"abcd");
    ASSERT_EQ(p.spans.size(), 4u);
    auto leadOf = [&](const PreparedText& t, size_t span) { return t.fonts[t.spans[span].fonts.front()]; };
    EXPECT_EQ(p.fonts.front(), roboto);
    EXPECT_EQ(leadOf(p, 0), roboto);
    EXPECT_EQ(leadOf(p, 1), playfair);
    EXPECT_EQ(leadOf(p, 2), hebrew);
    EXPECT_EQ(leadOf(p, 3), roboto);

    // Bold and italic are faux without fonts for them.
    PreparedText styled = prepareText("<b><i>x</i></b>", true, roboto, 20.0f, fonts);
    ASSERT_EQ(styled.runs.size(), 1u);
    EXPECT_TRUE(styled.runs[0].fauxBold);
    EXPECT_TRUE(styled.runs[0].fauxItalic);
    // With a bold font, bold uses it and italic stays faux.
    fonts.setStyles(roboto, playfair, 0, 0);
    styled = prepareText("<b><i>x</i></b>", true, roboto, 20.0f, fonts);
    EXPECT_EQ(leadOf(styled, 0), playfair);
    EXPECT_FALSE(styled.runs[0].fauxBold);
    EXPECT_TRUE(styled.runs[0].fauxItalic);

    // Plain text keeps its brackets.
    const PreparedText plain = prepareText("<b>x</b>", false, roboto, 20.0f, fonts);
    EXPECT_EQ(plain.text, U"<b>x</b>");
    EXPECT_TRUE(plain.spans.empty());
    EXPECT_TRUE(plain.runs.empty());
}

// ─── Through the systems ────────────────────────────────────────

namespace {

struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    glm::vec2 screen{800.0f, 600.0f};
    UIPointerState pointer;
    UIFocusState focus;
    CanvasInputSystem input{&screen, nullptr, &pointer, &focus};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();
    uint32_t roboto = 0;

    Fixture() {
        roboto = fonts.load(kFonts + "Roboto-Regular.ttf");
        fonts.setDefaultFallbacks({fonts.load(kFonts + "NotoSansArabic-Regular.ttf")});
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas);
    }

    /// Text of `markup` in a 400 x 200 box at (100, 100).
    entt::entity text(const std::string& markup, float size = 24.0f, bool rich = true) {
        const entt::entity e = createElement(registry, canvas, {400.0f, 200.0f});
        auto& rect = registry.get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = {100.0f, 100.0f};
        auto& t = registry.emplace<UIText>(e);
        t.text = markup;
        t.font = roboto;
        t.fontSize = size;
        t.richText = rich;
        step();
        return e;
    }

    void step() {
        input.update(registry, 0.016f);
        CanvasLayoutSystem(&screen, &fonts).update(registry, 0.016f);
        CanvasTextSystem(&fonts).update(registry, 0.016f);
    }

    ECS::InputStateComponent& state() { return registry.get<ECS::InputStateComponent>(inputEntity); }
    const UITextCache& cache(entt::entity e) { return registry.get<UITextCache>(e); }
    const UIText& textOf(entt::entity e) { return registry.get<UIText>(e); }

    /// The quads drawn, as their first vertex's index, of one mode.
    std::vector<UIVertex> vertices;
    std::vector<size_t> quads(UIDrawMode mode) {
        GlyphCache glyphs(512);
        UIDrawData data;
        buildDrawData(registry, fonts, glyphs, data);
        vertices = data.vertices;
        std::vector<size_t> out;
        for (size_t v = 0; v < vertices.size(); v += 4) {
            if ((vertices[v].mode & 0xFFu) == static_cast<uint32_t>(mode)) out.push_back(v);
        }
        return out;
    }

    /// The middle of the cluster that starts at code point `index`, in screen pixels.
    glm::vec2 clusterCentre(entt::entity e, size_t index) {
        const auto& layout = cache(e).layout;
        for (const TextCluster& c : layout.clusters) {
            if (c.firstCodepoint != index) continue;
            const TextLine& line = layout.lines[c.line];
            return registry.get<UIRect>(e).rect.min + glm::vec2(c.x + c.width * 0.5f, (line.top + line.bottom) * 0.5f);
        }
        ADD_FAILURE() << "no cluster at " << index;
        return {};
    }
};

} // namespace

TEST(RichText, ColoursGoOnTheirGlyphs) {
    Fixture f;
    f.text("<color=#FF0000>ab</color>cd");
    const auto text = f.quads(UIDrawMode::Text);
    ASSERT_EQ(text.size(), 4u);
    const uint32_t red = packColor({1.0f, 0.0f, 0.0f, 1.0f});
    const uint32_t white = packColor(glm::vec4(1.0f));
    EXPECT_EQ(f.vertices[text[0]].color, red);
    EXPECT_EQ(f.vertices[text[1]].color, red);
    EXPECT_EQ(f.vertices[text[2]].color, white);
    EXPECT_EQ(f.vertices[text[3]].color, white);
}

TEST(RichText, ALineIsAsTallAsItsLargestSize) {
    Fixture f;
    const entt::entity big = f.text("a<size=48>B</size>\nb");
    const auto& lines = f.cache(big).layout.lines;
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NEAR(lines[0].bottom - lines[0].top, 2.0f * (lines[1].bottom - lines[1].top), 1e-3f);
    EXPECT_GE(lines[1].top, lines[0].bottom - 1e-3f);   // the lines do not overlap

    // The large glyph is twice the height of the same glyph at the text's size.
    const entt::entity plain = f.text("B");
    auto heightOf = [&](entt::entity e, size_t codepoint) {
        for (const PositionedGlyph& g : f.cache(e).layout.glyphs) {
            if (g.codepoint == codepoint) return g.max.y - g.min.y;
        }
        return 0.0f;
    };
    EXPECT_NEAR(heightOf(big, 1), 2.0f * heightOf(plain, 0), 1e-3f);
}

TEST(RichText, PlainTextAndTheSameTextWithTagsLayOutAlike) {
    Fixture f;
    const entt::entity literal = f.text("a<b>c", 24.0f, false);
    EXPECT_EQ(f.cache(literal).layout.clusters.size(), 5u);   // the brackets are text

    const entt::entity rich = f.text("<u>ab</u>c");
    const entt::entity plain = f.text("abc", 24.0f, false);
    const auto& a = f.cache(rich).layout;
    const auto& b = f.cache(plain).layout;
    ASSERT_EQ(a.clusters.size(), b.clusters.size());
    for (size_t k = 0; k < a.clusters.size(); ++k) EXPECT_NEAR(a.clusters[k].x, b.clusters[k].x, 1e-4f);
    EXPECT_NEAR(a.lines[0].baseline, b.lines[0].baseline, 1e-4f);

    // Sized to its content, the markup takes no room.
    for (const entt::entity e : {rich, plain}) {
        f.registry.emplace<UIContentSizeFitter>(e).horizontal = UIContentSizeFitter::Fit::Preferred;
    }
    f.step();
    EXPECT_NEAR(f.registry.get<UIRect>(rich).rect.size().x, f.registry.get<UIRect>(plain).rect.size().x, 1e-3f);
}

TEST(RichText, FauxBoldAndItalicAreInTheVertices) {
    Fixture f;
    f.text("<b>l</b><i>l</i>l");
    const auto text = f.quads(UIDrawMode::Text);
    ASSERT_EQ(text.size(), 3u);
    auto weightOf = [&](size_t v) { return (f.vertices[v].mode >> 16) & 0xFFu; };
    // Bold: the outline moved out, the quad upright.
    EXPECT_EQ(weightOf(text[0]), kFauxBoldWeight);
    EXPECT_FLOAT_EQ(f.vertices[text[0]].position.x, f.vertices[text[0] + 3].position.x);
    // Italic: the top moved right of the bottom.
    EXPECT_EQ(weightOf(text[1]), 0u);
    EXPECT_GT(f.vertices[text[1]].position.x, f.vertices[text[1] + 3].position.x);
    const float height = f.vertices[text[1] + 3].position.y - f.vertices[text[1]].position.y;
    EXPECT_NEAR(f.vertices[text[1]].position.x - f.vertices[text[1] + 3].position.x, height * kFauxItalicSlant, 1e-3f);
    // Plain.
    EXPECT_EQ(weightOf(text[2]), 0u);
    EXPECT_FLOAT_EQ(f.vertices[text[2]].position.x, f.vertices[text[2] + 3].position.x);
}

TEST(RichText, UnderlineAndStrikethroughAreQuadsFromTheFontsMetrics) {
    Fixture f;
    const entt::entity e = f.text("<u>ab</u>c<s>d</s>");
    const auto solid = f.quads(UIDrawMode::Solid);
    ASSERT_EQ(solid.size(), 2u);   // "ab" in one quad, then "d"
    const auto& layout = f.cache(e).layout;
    const glm::vec2 origin = f.registry.get<UIRect>(e).rect.min;
    const float baseline = origin.y + layout.lines[0].baseline;

    const UIVertex* under = &f.vertices[solid[0]];
    EXPECT_NEAR(under[0].position.x, origin.x + layout.clusters[0].x, 1e-3f);
    EXPECT_NEAR(under[1].position.x, origin.x + layout.clusters[1].x + layout.clusters[1].width, 1e-3f);
    EXPECT_GT(under[0].position.y, baseline);    // below the baseline
    const UIVertex* strike = &f.vertices[solid[1]];
    EXPECT_NEAR(strike[0].position.x, origin.x + layout.clusters[3].x, 1e-3f);
    EXPECT_LT(strike[2].position.y, baseline);   // above it
    EXPECT_GT(strike[2].position.y, strike[0].position.y);
}

TEST(RichText, LinksAreFoundByCluster) {
    Fixture f;
    const entt::entity e = f.text("go <link=a>here</link> now");
    const auto& c = f.cache(e);
    ASSERT_EQ(c.links, std::vector<std::string>{"a"});
    const glm::vec2 origin = f.registry.get<UIRect>(e).rect.min;
    EXPECT_EQ(linkAt(c.layout, c.runs, f.clusterCentre(e, 4) - origin), 0);    // the e of here
    EXPECT_EQ(linkAt(c.layout, c.runs, f.clusterCentre(e, 0) - origin), -1);   // g
    EXPECT_EQ(linkAt(c.layout, c.runs, f.clusterCentre(e, 4) - origin + glm::vec2(0.0f, 100.0f)), -1);   // below
    EXPECT_TRUE(isRaycastTarget(f.registry, e));
    EXPECT_FALSE(isRaycastTarget(f.registry, f.text("<u>no links</u>")));
}

TEST(RichText, ALinkInAnArabicLineKeepsItsClustersInDisplayOrder) {
    Fixture f;
    // "marhaban <link=x>world</link> bika", right to left with the link left to right inside.
    std::u32string markup = {0x0645, 0x0631, 0x062D, 0x0628, 0x0627, U' '};
    markup += U"<link=x>world</link> ";
    markup += std::u32string{0x0628, 0x0643};
    const entt::entity e = f.text(encodeUtf8(markup));
    const auto& c = f.cache(e);
    ASSERT_EQ(c.layout.lines.size(), 1u);
    EXPECT_TRUE(c.layout.lines[0].rightToLeft);

    // The link's five clusters: side by side, left to right in typing order,
    // right of "bika" and left of "marhaban".
    std::vector<const TextCluster*> link;
    float arabicFirst = 0.0f, arabicLast = 0.0f;
    for (const TextCluster& cluster : c.layout.clusters) {
        if (cluster.firstCodepoint >= 6 && cluster.firstCodepoint < 11) link.push_back(&cluster);
        if (cluster.firstCodepoint == 0) arabicFirst = cluster.x;
        if (cluster.firstCodepoint == 12) arabicLast = cluster.x;
    }
    ASSERT_EQ(link.size(), 5u);
    for (size_t k = 1; k < link.size(); ++k) {
        EXPECT_EQ(link[k]->firstCodepoint, link[k - 1]->firstCodepoint + 1);
        EXPECT_NEAR(link[k]->x, link[k - 1]->x + link[k - 1]->width, 1e-3f);
    }
    EXPECT_GT(link.front()->x, arabicLast);
    EXPECT_LT(link.back()->x, arabicFirst);
    const glm::vec2 origin = f.registry.get<UIRect>(e).rect.min;
    for (size_t index = 6; index < 11; ++index) {
        EXPECT_EQ(linkAt(c.layout, c.runs, f.clusterCentre(e, index) - origin), 0);
    }
    EXPECT_EQ(linkAt(c.layout, c.runs, f.clusterCentre(e, 0) - origin), -1);
}

TEST(RichText, ATextFieldShowsItsTextAsTyped) {
    Fixture f;
    const entt::entity field = createTextField(f.registry, f.canvas, "", f.roboto);
    auto& state = f.registry.get<UITextField>(field);
    state.text = "<b>x</b>";
    f.registry.get<UIText>(state.textElement).richText = true;
    f.step();
    EXPECT_FALSE(f.textOf(state.textElement).richText);
    EXPECT_EQ(f.cache(state.textElement).layout.clusters.size(), 8u);
}

TEST(RichText, PointingAtALinkHoversItAndPressingAndReleasingClicksIt) {
    Fixture f;
    const entt::entity e = f.text("go <link=home>here</link>");
    const glm::vec2 overLink = f.clusterCentre(e, 3);
    const glm::vec2 overText = f.clusterCentre(e, 0);

    f.state().mousePosition = overLink;
    f.step();
    EXPECT_EQ(f.textOf(e).hoveredLink, "home");
    // The hovered link takes the link colour.
    const auto text = f.quads(UIDrawMode::Text);
    ASSERT_EQ(text.size(), 6u);
    EXPECT_EQ(f.vertices[text[0]].color, packColor(glm::vec4(1.0f)));
    EXPECT_EQ(f.vertices[text[2]].color, packColor(f.textOf(e).linkColor));

    f.state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = true;
    f.step();
    EXPECT_EQ(f.textOf(e).clickedLink, "");
    f.state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = false;
    f.step();
    EXPECT_EQ(f.textOf(e).clickedLink, "home");
    f.step();
    EXPECT_EQ(f.textOf(e).clickedLink, "");          // for one frame

    // Released off the link, it is no click.
    f.state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = true;
    f.step();
    f.state().mousePosition = overText;
    f.state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = false;
    f.step();
    EXPECT_EQ(f.textOf(e).clickedLink, "");
    EXPECT_EQ(f.textOf(e).hoveredLink, "");
}
