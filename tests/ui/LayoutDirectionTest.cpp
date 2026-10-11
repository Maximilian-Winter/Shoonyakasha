//
// LayoutDirectionTest.cpp - Right-to-left layout: mirrored placement, groups,
// widgets, scrolling and text
//
// Tier 2: EnTT registry, no GPU. Each step runs input against the last
// layout, then layout and text.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/CanvasLayout.h"
#include "UI/FontLibrary.h"
#include "UI/UIBatchBuilder.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <GLFW/glfw3.h>

#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";
constexpr float kWidth = 800.0f;

struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    uint32_t font = fonts.load(kFonts + "Roboto-Regular.ttf");
    glm::vec2 screen{kWidth, 600.0f};
    UIPointerState pointer;
    UIFocusState focus;
    CanvasInputSystem input{&screen, nullptr, &pointer, &focus};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();

    explicit Fixture(bool rightToLeft = true) {
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas).layoutDirection =
            rightToLeft ? LayoutDirection::RightToLeft : LayoutDirection::LeftToRight;
    }

    UIRect& rect(entt::entity e) { return registry.get<UIRect>(e); }

    entt::entity element(entt::entity parent, glm::vec2 anchor, glm::vec2 pivot, glm::vec2 position, glm::vec2 size) {
        const entt::entity e = createElement(registry, parent, size);
        auto& r = rect(e);
        r.anchorMin = r.anchorMax = anchor;
        r.pivot = pivot;
        r.anchoredPosition = position;
        return e;
    }

    void layout() {
        CanvasLayoutSystem(&screen, &fonts).update(registry, 0.0f);
        CanvasTextSystem(&fonts).update(registry, 0.0f);
    }

    void step(glm::vec2 mouse, bool down, glm::vec2 wheel = glm::vec2(0.0f),
              std::vector<ECS::InputStateComponent::KeyPress> keys = {}) {
        auto& state = registry.get<ECS::InputStateComponent>(inputEntity);
        state.mousePosition = mouse;
        state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
        state.scrollDelta = wheel;
        state.keyPresses = std::move(keys);
        input.update(registry, 1.0f / 60.0f);
        layout();
        state.keyPresses.clear();
    }
};

void expectMirrored(const Rect& rtl, const Rect& ltr, float width) {
    EXPECT_NEAR(rtl.min.x, width - ltr.max.x, 1e-3f);
    EXPECT_NEAR(rtl.max.x, width - ltr.min.x, 1e-3f);
    EXPECT_NEAR(rtl.min.y, ltr.min.y, 1e-3f);
    EXPECT_NEAR(rtl.max.y, ltr.max.y, 1e-3f);
}

} // namespace

// ─── Placement ──────────────────────────────────────────────────

TEST(LayoutDirection, EveryAnchorPresetIsMirrored) {
    for (const float ax : {0.0f, 0.5f, 1.0f}) {
        for (const float ay : {0.0f, 0.5f, 1.0f}) {
            Fixture rtl(true), ltr(false);
            const glm::vec2 anchor(ax, ay), pivot(0.25f, 0.75f), position(30.0f, 12.0f), size(120.0f, 40.0f);
            const entt::entity a = rtl.element(rtl.canvas, anchor, pivot, position, size);
            const entt::entity b = ltr.element(ltr.canvas, anchor, pivot, position, size);
            rtl.layout();
            ltr.layout();
            SCOPED_TRACE(testing::Message() << "anchor " << ax << ", " << ay);
            expectMirrored(rtl.rect(a).rect, ltr.rect(b).rect, kWidth);
            EXPECT_TRUE(rtl.rect(a).rightToLeft);
            EXPECT_FALSE(ltr.rect(b).rightToLeft);
        }
    }

    // A stretched element with unequal margins.
    Fixture rtl(true), ltr(false);
    for (Fixture* f : {&rtl, &ltr}) {
        const entt::entity e = createElement(f->registry, f->canvas, {-60.0f, 0.0f});
        auto& r = f->rect(e);
        r.anchorMin = {0.0f, 0.0f};
        r.anchorMax = {1.0f, 1.0f};
        r.pivot = {0.0f, 0.5f};
        f->layout();
    }
    const auto rtlRects = rtl.registry.view<UIRect>();
    const auto ltrRects = ltr.registry.view<UIRect>();
    expectMirrored(rtl.rect(rtlRects.front()).rect, ltr.rect(ltrRects.front()).rect, kWidth);
}

TEST(LayoutDirection, NestedDirectionsAndInheritance) {
    Fixture f(true);
    // A left-to-right panel on the right of a right-to-left canvas...
    const entt::entity panel = f.element(f.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {20.0f, 20.0f}, {300.0f, 200.0f});
    f.rect(panel).layoutDirection = LayoutDirection::LeftToRight;
    // ...places its own child from its left; the child's child inherits.
    const entt::entity child = f.element(panel, {0.0f, 0.0f}, {0.0f, 0.0f}, {10.0f, 10.0f}, {50.0f, 50.0f});
    const entt::entity grandchild = f.element(child, {0.0f, 0.0f}, {0.0f, 0.0f}, {5.0f, 5.0f}, {10.0f, 10.0f});
    // A right-to-left element inside it mirrors its own children again.
    const entt::entity inner = f.element(panel, {0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f}, {100.0f, 40.0f});
    f.rect(inner).layoutDirection = LayoutDirection::RightToLeft;
    const entt::entity innerChild = f.element(inner, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {20.0f, 20.0f});
    f.layout();

    EXPECT_NEAR(f.rect(panel).rect.max.x, kWidth - 20.0f, 1e-3f);   // mirrored by the canvas
    EXPECT_FALSE(f.rect(panel).rightToLeft);
    EXPECT_NEAR(f.rect(child).rect.min.x, f.rect(panel).rect.min.x + 10.0f, 1e-3f);
    EXPECT_FALSE(f.rect(grandchild).rightToLeft);
    EXPECT_NEAR(f.rect(grandchild).rect.min.x, f.rect(child).rect.min.x + 5.0f, 1e-3f);
    EXPECT_NEAR(f.rect(inner).rect.min.x, f.rect(panel).rect.min.x, 1e-3f);
    EXPECT_TRUE(f.rect(innerChild).rightToLeft);
    EXPECT_NEAR(f.rect(innerChild).rect.max.x, f.rect(inner).rect.max.x, 1e-3f);
}

TEST(LayoutDirection, LayoutGroupsRunFromTheRight) {
    Fixture f(true);
    const entt::entity row = f.element(f.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {400.0f, 50.0f});
    auto& h = f.registry.emplace<UIHorizontalLayout>(row);
    h.padding = {10.0f, 0.0f, 0.0f, 0.0f};   // at the start: the right
    h.spacing = 5.0f;
    h.controlChildSize = {false, false};
    std::vector<entt::entity> children;
    for (int i = 0; i < 3; ++i) children.push_back(createElement(f.registry, row, {40.0f, 20.0f}));

    const entt::entity grid = f.element(f.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 100.0f}, {300.0f, 200.0f});
    auto& g = f.registry.emplace<UIGridLayout>(grid);
    g.cellSize = {50.0f, 50.0f};
    g.columns = 3;
    std::vector<entt::entity> cells;
    for (int i = 0; i < 4; ++i) cells.push_back(createElement(f.registry, grid, {0.0f, 0.0f}));
    f.layout();

    const Rect& rowRect = f.rect(row).rect;
    EXPECT_NEAR(rowRect.max.x, kWidth, 1e-3f);
    EXPECT_NEAR(f.rect(children[0]).rect.max.x, rowRect.max.x - 10.0f, 1e-3f);   // first child at the right, after the padding
    EXPECT_NEAR(f.rect(children[1]).rect.max.x, f.rect(children[0]).rect.min.x - 5.0f, 1e-3f);
    EXPECT_LT(f.rect(children[2]).rect.min.x, f.rect(children[1]).rect.min.x);

    const Rect& gridRect = f.rect(grid).rect;
    EXPECT_NEAR(f.rect(cells[0]).rect.max.x, gridRect.max.x, 1e-3f);   // the first column on the right
    EXPECT_NEAR(f.rect(cells[3]).rect.max.x, gridRect.max.x, 1e-3f);   // the second row starts there too
    EXPECT_GT(f.rect(cells[3]).rect.min.y, f.rect(cells[0]).rect.min.y);
    EXPECT_LT(f.rect(cells[2]).rect.min.x, f.rect(cells[1]).rect.min.x);
}

// ─── Widgets ────────────────────────────────────────────────────

TEST(LayoutDirection, ASliderRunsFromTheRight) {
    Fixture f(true);
    const entt::entity slider = createSlider(f.registry, f.canvas, 0.0f, 10.0f, 2.0f, {200.0f, 24.0f});
    f.layout();
    const Rect r = f.rect(slider).rect;
    const auto& s = f.registry.get<UISlider>(slider);
    // At 2 of 10, the handle is a fifth of the way from the right.
    const Rect handle = f.rect(s.handle).rect;
    EXPECT_NEAR((handle.min.x + handle.max.x) * 0.5f, r.max.x - 0.2f * r.size().x, 1e-3f);
    EXPECT_NEAR(f.rect(s.fill).rect.max.x, r.max.x, 1e-3f);

    // Dragged to the left end, it is at its maximum; to the right end, its minimum.
    const float y = (r.min.y + r.max.y) * 0.5f;
    f.step({r.min.x + 50.0f, y}, true);
    f.step({r.min.x - 20.0f, y}, true);
    EXPECT_FLOAT_EQ(s.value, 10.0f);
    f.step({r.max.x + 20.0f, y}, true);
    EXPECT_FLOAT_EQ(s.value, 0.0f);
    f.step({r.max.x + 20.0f, y}, false);

    // Focused, Left moves the handle left: up.
    f.focus.focused = slider;
    f.step({0.0f, 0.0f}, false, {}, {{GLFW_KEY_LEFT, 0, false, 0}});
    EXPECT_FLOAT_EQ(s.value, 1.0f);
    f.step({0.0f, 0.0f}, false, {}, {{GLFW_KEY_RIGHT, 0, false, 0}});
    EXPECT_FLOAT_EQ(s.value, 0.0f);
}

TEST(LayoutDirection, AToggleHasItsBoxOnTheRightAndItsLabelBesideIt) {
    Fixture f(true);
    const entt::entity toggle = createToggle(f.registry, f.canvas, "Fog", f.font, true, {200.0f, 28.0f});
    f.layout();
    const auto& targets = f.registry.get<UIInteractable>(toggle).tintTargets;   // box, checkmark, label
    const Rect box = f.rect(targets[0]).rect;
    EXPECT_NEAR(box.max.x, f.rect(toggle).rect.max.x, 1e-3f);
    const auto& label = f.registry.get<UITextCache>(targets[2]);
    ASSERT_FALSE(label.layout.clusters.empty());
    EXPECT_TRUE(label.layout.lines[0].rightToLeft);   // its direction follows the layout
    const Rect labelRect = f.rect(targets[2]).rect;
    const float textRight = labelRect.min.x + label.layout.clusters.back().x + label.layout.clusters.back().width;
    EXPECT_NEAR(textRight, labelRect.max.x, 1e-3f);   // against the box
    EXPECT_LT(labelRect.max.x, box.min.x);
}

// ─── Scrolling ──────────────────────────────────────────────────

TEST(LayoutDirection, ScrollViewsStartAtTheRightWithTheirBarOnTheLeft) {
    Fixture f(true);
    const entt::entity view = createScrollView(f.registry, f.canvas, {200.0f, 100.0f}, true, true);
    auto& r = f.rect(view);
    r.anchorMin = r.anchorMax = r.pivot = {0.0f, 0.0f};   // mirrored: at the canvas's right
    auto& sv = f.registry.get<UIScrollView>(view);
    auto& content = f.rect(sv.content);
    f.registry.remove<UIContentSizeFitter>(sv.content);
    content.sizeDelta = {600.0f, 400.0f};
    f.layout();

    const Rect viewRect = f.rect(view).rect;
    EXPECT_NEAR(f.rect(sv.content).rect.max.x, viewRect.max.x, 1e-3f);   // its start at the right
    EXPECT_NEAR(f.rect(sv.verticalScrollbar).rect.min.x, viewRect.min.x, 1e-3f);

    // Scrolling on moves the content right, showing what is to its left.
    sv.scrollPosition.x = 0.5f;
    f.layout();
    EXPECT_NEAR(f.rect(sv.content).rect.max.x, viewRect.max.x + 0.5f * sv.range().x, 1e-3f);

    // Dragging the content right scrolls on, as it moves with the pointer.
    sv.scrollPosition.x = 0.0f;
    sv.inertia = false;
    f.layout();
    const glm::vec2 at = (viewRect.min + viewRect.max) * 0.5f;
    f.step(at, true);
    f.step(at + glm::vec2(40.0f, 0.0f), true);
    EXPECT_NEAR(sv.scrollPosition.x * sv.range().x, 40.0f, 1e-3f);
    f.step(at + glm::vec2(40.0f, 0.0f), false);

    // Tilting the wheel left moves the view left: on through the content.
    sv.scrollPosition.x = 0.0f;
    f.layout();
    f.step(at, false, {-1.0f, 0.0f});
    EXPECT_NEAR(sv.scrollPosition.x * sv.range().x, sv.wheelSpeed, 1e-3f);
}

TEST(LayoutDirection, AHorizontalScrollbarReadsFromTheRight) {
    Fixture f(true);
    const entt::entity view = createScrollView(f.registry, f.canvas, {200.0f, 100.0f}, true, false);
    auto& r = f.rect(view);
    r.anchorMin = r.anchorMax = r.pivot = {0.0f, 0.0f};
    auto& sv = f.registry.get<UIScrollView>(view);
    f.registry.remove<UIContentSizeFitter>(sv.content);
    f.rect(sv.content).sizeDelta = {800.0f, 80.0f};
    f.layout();                     // the content's size is known from here
    f.step({0.0f, 0.0f}, false);    // the bar shows, its handle a quarter long

    const entt::entity bar = sv.horizontalScrollbar;
    const Rect barRect = f.rect(bar).rect;
    const Rect handle = f.rect(f.registry.get<UIScrollbar>(bar).handle).rect;
    ASSERT_TRUE(f.rect(bar).visibleInHierarchy);
    EXPECT_NEAR(handle.size().x, barRect.size().x * 0.25f, 1e-3f);
    EXPECT_NEAR(handle.max.x, barRect.max.x, 1e-3f);   // at the start: the right

    // Pressing the track at its left end pages on.
    const float y = (barRect.min.y + barRect.max.y) * 0.5f;
    f.step({barRect.min.x + 2.0f, y}, true);
    EXPECT_NEAR(sv.scrollPosition.x, 0.25f, 1e-4f);   // a page: the visible quarter
    f.step({barRect.min.x + 2.0f, y}, false);
}

// ─── Text and images ────────────────────────────────────────────

TEST(LayoutDirection, TextFollowsTheLayoutWhereItHasNoDirectionOfItsOwn) {
    Fixture f(true);
    auto text = [&](const std::string& value, TextDirection direction) {
        const entt::entity e = f.element(f.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {300.0f, 40.0f});
        auto& t = f.registry.emplace<UIText>(e);
        t.text = value;
        t.font = f.font;
        t.alignH = TextAlignH::Start;
        t.direction = direction;
        return e;
    };
    const entt::entity number = text("12:30", TextDirection::Auto);          // no letters: the layout's direction
    const entt::entity latin = text("Play", TextDirection::Auto);            // Latin letters: left to right
    const entt::entity fromLayout = text("Play", TextDirection::FromLayout); // the layout's, whatever the letters
    f.layout();

    auto rightEdge = [&](entt::entity e) {
        const auto& c = f.registry.get<UITextCache>(e).layout.clusters;
        return c.back().x + c.back().width;
    };
    auto leftEdge = [&](entt::entity e) { return f.registry.get<UITextCache>(e).layout.clusters.front().x; };
    EXPECT_NEAR(rightEdge(number), 300.0f, 1e-3f);
    EXPECT_NEAR(leftEdge(latin), 0.0f, 1e-3f);
    EXPECT_NEAR(rightEdge(fromLayout), 300.0f, 1e-3f);

    // Left to right, Auto text without letters stays left.
    Fixture ltr(false);
    const entt::entity e = ltr.element(ltr.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {300.0f, 40.0f});
    auto& t = ltr.registry.emplace<UIText>(e);
    t.text = "12:30";
    t.font = ltr.font;
    t.alignH = TextAlignH::Start;
    ltr.layout();
    EXPECT_NEAR(ltr.registry.get<UITextCache>(e).layout.clusters.front().x, 0.0f, 1e-3f);
}

TEST(LayoutDirection, AnImageMarkedToMirrorIsFlipped) {
    Fixture f(true);
    GPUTexture texture;
    texture.view = reinterpret_cast<VkImageView>(uintptr_t{7});
    texture.width = texture.height = 16;
    auto image = [&](bool mirror) {
        const entt::entity e = f.element(f.canvas, {0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}, {16.0f, 16.0f});
        auto& img = f.registry.emplace<UIImage>(e);
        img.texture = texture;
        img.mirrorInRightToLeft = mirror;
        return e;
    };
    image(false);
    image(true);
    f.layout();
    GlyphCache glyphs(64);
    UIDrawData data;
    buildDrawData(f.registry, f.fonts, glyphs, data);
    ASSERT_EQ(data.vertices.size(), 8u);
    EXPECT_EQ(data.vertices[0].uv.x, 0.0f);   // the plain one, left edge first
    EXPECT_EQ(data.vertices[4].uv.x, 1.0f);   // the mirrored one
}
