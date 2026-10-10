//
// LayoutGroupTest.cpp - Layout groups, layout sizes and size fitters
//
// Tier 2: EnTT registry and fonts, no GPU.
//

#include <gtest/gtest.h>

#include "ECS/Core.h"
#include "UI/CanvasLayout.h"
#include "UI/FontLibrary.h"
#include "UI/UIWidgets.h"

#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kRoboto = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/Roboto-Regular.ttf";

struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    entt::entity canvas = registry.create();

    Fixture() { registry.emplace<UICanvas>(canvas); }

    /// An element of `size` with its top-left at `position` in its parent.
    entt::entity add(entt::entity parent, glm::vec2 size, glm::vec2 position = glm::vec2(0.0f)) {
        const entt::entity e = createElement(registry, parent, size);
        auto& rect = registry.get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = position;
        return e;
    }

    template<typename Layout>
    Layout& group(entt::entity e) { return registry.emplace<Layout>(e); }

    void layout() { layoutCanvas(registry, canvas, {800.0f, 600.0f}, &fonts); }

    const Rect& rect(entt::entity e) { return registry.get<UIRect>(e).rect; }
};

void expectRect(const Rect& r, glm::vec2 min, glm::vec2 size) {
    EXPECT_NEAR(r.min.x, min.x, 1e-3f);
    EXPECT_NEAR(r.min.y, min.y, 1e-3f);
    EXPECT_NEAR(r.size().x, size.x, 1e-3f);
    EXPECT_NEAR(r.size().y, size.y, 1e-3f);
}

} // namespace

// ─── Distribution and grid arithmetic ───────────────────────────

TEST(LayoutSizes, DistributionMovesFromMinToPreferredThenSharesTheRest) {
    const std::vector<LayoutSizes> children = {{10, 50, 1}, {20, 40, 3}};
    std::vector<float> lengths;

    distributeSizes(children, 20, false, lengths);    // less than the mins
    EXPECT_EQ(lengths, (std::vector<float>{10, 20}));

    distributeSizes(children, 60, false, lengths);    // halfway from mins (30) to preferreds (90)
    EXPECT_EQ(lengths, (std::vector<float>{30, 30}));
    distributeSizes(children, 75, false, lengths);    // three quarters of the way
    EXPECT_EQ(lengths, (std::vector<float>{40, 35}));

    distributeSizes(children, 170, false, lengths);   // 80 spare, shared 1:3
    EXPECT_EQ(lengths, (std::vector<float>{70, 100}));
}

TEST(LayoutSizes, ForceExpandSharesSpareRoomWithInflexibleChildren) {
    const std::vector<LayoutSizes> children = {{0, 20, 0}, {0, 20, 0}};
    std::vector<float> lengths;
    distributeSizes(children, 100, false, lengths);
    EXPECT_EQ(lengths, (std::vector<float>{20, 20}));
    distributeSizes(children, 100, true, lengths);
    EXPECT_EQ(lengths, (std::vector<float>{50, 50}));
}

TEST(LayoutSizes, GridColumnsFitTheWidthOrStayFixed) {
    UIGridLayout grid;
    grid.cellSize = {50, 50};
    grid.spacing = {10, 10};
    EXPECT_EQ(gridColumns(grid, 169, 10), 2);   // three cells need 50 + 10 + 50 + 10 + 50 = 170
    EXPECT_EQ(gridColumns(grid, 170, 10), 3);
    EXPECT_EQ(gridColumns(grid, 1000, 4), 4);   // never more than the children
    EXPECT_EQ(gridColumns(grid, 10, 4), 1);     // at least one
    grid.columns = 5;
    EXPECT_EQ(gridColumns(grid, 10, 4), 5);
}

// ─── Linear layouts ─────────────────────────────────────────────

TEST(LayoutGroup, VerticalStacksChildrenWithPaddingAndSpacing) {
    Fixture f;
    const auto column = f.add(f.canvas, {300, 400}, {50, 20});
    auto& layout = f.group<UIVerticalLayout>(column);
    layout.padding = {10, 12, 10, 12};
    layout.spacing = 5;
    const auto a = f.add(column, {100, 30}, {999, 999});   // anchors are ignored
    const auto b = f.add(column, {120, 20});
    f.layout();

    expectRect(f.rect(a), {60, 32}, {100, 30});
    expectRect(f.rect(b), {60, 67}, {120, 20});
    EXPECT_TRUE(f.registry.get<UIRect>(a).drivenByLayout);
    EXPECT_FALSE(f.registry.get<UIRect>(column).drivenByLayout);
}

TEST(LayoutGroup, HorizontalExpandsAlongAndFillsAcrossWhenAsked) {
    Fixture f;
    const auto row = f.add(f.canvas, {400, 60});
    auto& layout = f.group<UIHorizontalLayout>(row);
    layout.spacing = 20;
    layout.childForceExpand = {true, true};
    const auto a = f.add(row, {50, 10});
    const auto b = f.add(row, {50, 10});
    f.layout();

    // 400 - 20 spacing = 380; preferreds 100, so each gets 50 + 140.
    expectRect(f.rect(a), {0, 0}, {190, 60});
    expectRect(f.rect(b), {210, 0}, {190, 60});
}

TEST(LayoutGroup, SpareRoomFollowsChildAlignment) {
    Fixture f;
    const auto row = f.add(f.canvas, {400, 100});
    auto& layout = f.group<UIHorizontalLayout>(row);
    layout.childAlignment = {0.5f, 1.0f};
    const auto a = f.add(row, {100, 40});
    const auto b = f.add(row, {100, 20});
    f.layout();

    expectRect(f.rect(a), {100, 60}, {100, 40});
    expectRect(f.rect(b), {200, 80}, {100, 20});
}

TEST(LayoutGroup, FlexibleChildrenShareTheRestByWeight) {
    Fixture f;
    const auto row = f.add(f.canvas, {400, 50});
    f.group<UIHorizontalLayout>(row);
    const auto a = f.add(row, {50, 50});
    const auto b = f.add(row, {50, 50});
    f.registry.emplace<UILayoutElement>(a).flexibleSize = {1, -1};
    f.registry.emplace<UILayoutElement>(b).flexibleSize = {3, -1};
    f.layout();

    EXPECT_NEAR(f.rect(a).size().x, 125, 1e-3f);
    EXPECT_NEAR(f.rect(b).size().x, 275, 1e-3f);
}

TEST(LayoutGroup, ChildrenShrinkTowardsTheirMinsWhenRoomIsShort) {
    Fixture f;
    const auto row = f.add(f.canvas, {100, 50});
    f.group<UIHorizontalLayout>(row);
    const auto a = f.add(row, {80, 50});
    const auto b = f.add(row, {80, 50});
    for (const auto e : {a, b}) f.registry.emplace<UILayoutElement>(e).minSize = {40, -1};
    f.layout();

    // Mins 80, preferreds 160: 100 is a quarter of the way.
    EXPECT_NEAR(f.rect(a).size().x, 50, 1e-3f);
    EXPECT_NEAR(f.rect(b).min.x, 50, 1e-3f);
}

TEST(LayoutGroup, UncontrolledChildrenKeepTheirOwnSize) {
    Fixture f;
    const auto column = f.add(f.canvas, {300, 300});
    auto& layout = f.group<UIVerticalLayout>(column);
    layout.controlChildSize = {false, false};
    layout.childForceExpand = {true, true};
    const auto a = f.add(column, {70, 30});
    f.registry.emplace<UILayoutElement>(a).preferredSize = {200, 200};
    f.layout();
    expectRect(f.rect(a), {0, 0}, {70, 30});
}

TEST(LayoutGroup, HiddenAndIgnoredChildrenTakeNoSpace) {
    Fixture f;
    const auto column = f.add(f.canvas, {200, 300});
    f.group<UIVerticalLayout>(column);
    const auto hidden = f.add(column, {100, 50});
    f.registry.get<UIRect>(hidden).visible = false;
    const auto ignored = f.add(column, {40, 40}, {150, 250});
    f.registry.emplace<UILayoutElement>(ignored).ignoreLayout = true;
    const auto shown = f.add(column, {100, 50});
    f.layout();

    expectRect(f.rect(shown), {0, 0}, {100, 50});
    expectRect(f.rect(ignored), {150, 250}, {40, 40});   // by its anchors
    EXPECT_FALSE(f.registry.get<UIRect>(ignored).drivenByLayout);
}

TEST(LayoutGroup, NestedGroupsReportTheirChildrenUpwards) {
    Fixture f;
    const auto column = f.add(f.canvas, {400, 400});
    auto& outer = f.group<UIVerticalLayout>(column);
    outer.spacing = 10;
    const auto row = f.add(column, {0, 0});
    auto& inner = f.group<UIHorizontalLayout>(row);
    inner.padding = {4, 4, 4, 4};
    inner.spacing = 6;
    const auto a = f.add(row, {50, 30});
    const auto b = f.add(row, {60, 44});
    const auto below = f.add(column, {80, 20});
    f.layout();

    const auto& rowRect = f.registry.get<UIRect>(row);
    EXPECT_EQ(rowRect.preferredSize, glm::vec2(4 + 50 + 6 + 60 + 4, 4 + 44 + 4));
    expectRect(rowRect.rect, {0, 0}, {124, 52});
    expectRect(f.rect(a), {4, 4}, {50, 30});
    expectRect(f.rect(b), {60, 4}, {60, 44});
    expectRect(f.rect(below), {0, 62}, {80, 20});
}

// ─── Grid ───────────────────────────────────────────────────────

TEST(LayoutGroup, GridWrapsCellsRowByRow) {
    Fixture f;
    const auto grid = f.add(f.canvas, {230, 400});
    auto& layout = f.group<UIGridLayout>(grid);
    layout.cellSize = {60, 40};
    layout.spacing = {10, 5};
    layout.padding = {5, 5, 5, 5};
    std::vector<entt::entity> cells;
    for (int i = 0; i < 5; ++i) cells.push_back(f.add(grid, {1, 1}));
    f.layout();

    // Inner width 220 fits three cells of 60 with 10 between.
    expectRect(f.rect(cells[0]), {5, 5}, {60, 40});
    expectRect(f.rect(cells[2]), {145, 5}, {60, 40});
    expectRect(f.rect(cells[3]), {5, 50}, {60, 40});
    EXPECT_EQ(f.registry.get<UIRect>(grid).preferredSize.y, 5 + 40 + 5 + 40 + 5);
}

TEST(LayoutGroup, GridWithFixedColumnsCentresItsBlock) {
    Fixture f;
    const auto grid = f.add(f.canvas, {300, 300});
    auto& layout = f.group<UIGridLayout>(grid);
    layout.cellSize = {50, 50};
    layout.columns = 2;
    layout.childAlignment = {0.5f, 0.5f};
    std::vector<entt::entity> cells;
    for (int i = 0; i < 4; ++i) cells.push_back(f.add(grid, {1, 1}));
    f.layout();
    expectRect(f.rect(cells[0]), {100, 100}, {50, 50});
    expectRect(f.rect(cells[3]), {150, 150}, {50, 50});
}

// ─── Size fitters and text ──────────────────────────────────────

TEST(LayoutGroup, SizeFitterGrowsAGroupToItsContent) {
    Fixture f;
    const auto column = f.add(f.canvas, {200, 10}, {30, 40});
    auto& layout = f.group<UIVerticalLayout>(column);
    layout.padding = {0, 8, 0, 8};
    layout.spacing = 4;
    f.registry.emplace<UIContentSizeFitter>(column).vertical = UIContentSizeFitter::Fit::Preferred;
    for (int i = 0; i < 3; ++i) f.add(column, {100, 20});
    f.layout();
    expectRect(f.rect(column), {30, 40}, {200, 8 + 20 * 3 + 4 * 2 + 8});
}

TEST(LayoutGroup, WrappedTextGetsTallerInANarrowerColumn) {
    Fixture f;
    const uint32_t font = f.fonts.load(kRoboto);
    ASSERT_NE(font, 0u);
    auto column = [&](float width) {
        const auto c = f.add(f.canvas, {width, 500});
        auto& layout = f.group<UIVerticalLayout>(c);
        layout.childForceExpand = {true, false};
        const auto label = f.add(c, {0, 0});
        auto& text = f.registry.emplace<UIText>(label);
        text.text = "A line of text long enough to wrap in a narrow column of the layout.";
        text.font = font;
        text.fontSize = 20.0f;
        return label;
    };
    const auto wide = column(800);
    const auto narrow = column(150);
    f.layout();

    const float wideHeight = f.rect(wide).size().y;
    const float narrowHeight = f.rect(narrow).size().y;
    EXPECT_GT(wideHeight, 0.0f);
    EXPECT_GT(narrowHeight, 2.5f * wideHeight);
    EXPECT_NEAR(f.rect(narrow).size().x, 150, 1e-3f);
    EXPECT_GT(f.registry.get<UIRect>(wide).preferredSize.x, 200.0f);   // its unwrapped width
}

TEST(LayoutGroup, TextOutsideGroupsAndFittersIsNotMeasured) {
    Fixture f;
    const uint32_t font = f.fonts.load(kRoboto);
    const auto label = f.add(f.canvas, {100, 30});
    auto& text = f.registry.emplace<UIText>(label);
    text.text = "Hello";
    text.font = font;
    f.layout();
    EXPECT_FALSE(f.registry.all_of<UITextMeasure>(label));

    f.registry.emplace<UIContentSizeFitter>(label).horizontal = UIContentSizeFitter::Fit::Preferred;
    f.layout();
    EXPECT_TRUE(f.registry.all_of<UITextMeasure>(label));
    EXPECT_GT(f.rect(label).size().x, 20.0f);
    EXPECT_LT(f.rect(label).size().x, 100.0f);
}
