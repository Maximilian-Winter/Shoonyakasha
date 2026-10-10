//
// CanvasLayoutTest.cpp - UIRect resolution, canvas scaling and the layout walk
//
// Tier 2: EnTT registry, no GPU.
//

#include <gtest/gtest.h>

#include "UI/CanvasLayout.h"
#include "UI/UISystems.h"
#include "ECS/Core.h"

#include <cmath>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

constexpr float kEps = 1e-4f;

void expectRectNear(const Rect& r, glm::vec2 min, glm::vec2 max) {
    EXPECT_NEAR(r.min.x, min.x, kEps);
    EXPECT_NEAR(r.min.y, min.y, kEps);
    EXPECT_NEAR(r.max.x, max.x, kEps);
    EXPECT_NEAR(r.max.y, max.y, kEps);
}

const Rect kParent{{100.0f, 50.0f}, {500.0f, 350.0f}};  // 400 x 300

UIRect fixed(glm::vec2 anchor, glm::vec2 pivot, glm::vec2 position, glm::vec2 size) {
    UIRect r;
    r.anchorMin = r.anchorMax = anchor;
    r.pivot = pivot;
    r.anchoredPosition = position;
    r.sizeDelta = size;
    return r;
}

void attach(entt::registry& registry, entt::entity parent, entt::entity child) {
    registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(child);
    registry.get_or_emplace<ECS::HierarchyComponent>(child).parent = parent;
}

entt::entity element(entt::registry& registry, entt::entity parent, const UIRect& rect) {
    const entt::entity e = registry.create();
    registry.emplace<UIRect>(e, rect);
    attach(registry, parent, e);
    return e;
}

entt::entity stretch(entt::registry& registry, entt::entity parent, float inset = 0.0f) {
    UIRect r;
    r.anchorMin = {0.0f, 0.0f};
    r.anchorMax = {1.0f, 1.0f};
    r.sizeDelta = {-2.0f * inset, -2.0f * inset};
    return element(registry, parent, r);
}

entt::entity canvas(entt::registry& registry, UICanvas c = {}) {
    const entt::entity e = registry.create();
    registry.emplace<UICanvas>(e, c);
    return e;
}

} // namespace

// ── Rect maths ──────────────────────────────────────────────────

TEST(CanvasLayout, IntersectOverlapAndDisjoint) {
    expectRectNear(intersect({{0, 0}, {10, 10}}, {{5, 2}, {20, 8}}), {5, 2}, {10, 8});

    const Rect none = intersect({{0, 0}, {10, 10}}, {{20, 20}, {30, 30}});
    EXPECT_TRUE(none.empty());
    EXPECT_FALSE(none.contains({20, 20}));
}

TEST(CanvasLayout, ContainsIsHalfOpen) {
    const Rect r{{0, 0}, {10, 10}};
    EXPECT_TRUE(r.contains({0, 0}));
    EXPECT_TRUE(r.contains({9.99f, 5}));
    EXPECT_FALSE(r.contains({10, 5}));
}

// ── resolveRect ─────────────────────────────────────────────────

TEST(CanvasLayout, FixedSizeCentred) {
    const Rect r = resolveRect(kParent, fixed({0.5f, 0.5f}, {0.5f, 0.5f}, {0, 0}, {100, 40}));
    expectRectNear(r, {250, 180}, {350, 220});
}

TEST(CanvasLayout, TopLeftAnchorWithOffset) {
    const Rect r = resolveRect(kParent, fixed({0, 0}, {0, 0}, {10, 20}, {50, 30}));
    expectRectNear(r, {110, 70}, {160, 100});
}

TEST(CanvasLayout, BottomRightAnchorWithPivotAtCorner) {
    const Rect r = resolveRect(kParent, fixed({1, 1}, {1, 1}, {-10, -10}, {50, 30}));
    expectRectNear(r, {440, 310}, {490, 340});
}

TEST(CanvasLayout, StretchWithInset) {
    UIRect e;
    e.anchorMin = {0, 0};
    e.anchorMax = {1, 1};
    e.sizeDelta = {-20, -20};
    expectRectNear(resolveRect(kParent, e), {110, 60}, {490, 340});
}

TEST(CanvasLayout, HorizontalStretchFixedHeightAtTop) {
    UIRect e;
    e.anchorMin = {0, 0};
    e.anchorMax = {1, 0};
    e.pivot = {0.5f, 0};
    e.sizeDelta = {0, 40};
    expectRectNear(resolveRect(kParent, e), {100, 50}, {500, 90});
}

// ── canvasScale ─────────────────────────────────────────────────

TEST(CanvasLayout, ConstantPixelScale) {
    UICanvas c;
    c.scaleFactor = 2.0f;
    EXPECT_FLOAT_EQ(canvasScale(c, {1920, 1080}), 2.0f);
}

TEST(CanvasLayout, ScaleWithScreenFollowsMatch) {
    UICanvas c;
    c.scaleMode = UICanvas::ScaleMode::ScaleWithScreen;
    c.referenceSize = {1920, 1080};

    c.match = 0.0f;
    EXPECT_NEAR(canvasScale(c, {3840, 1080}), 2.0f, kEps);
    c.match = 1.0f;
    EXPECT_NEAR(canvasScale(c, {3840, 1080}), 1.0f, kEps);
    c.match = 0.5f;
    EXPECT_NEAR(canvasScale(c, {3840, 1080}), std::sqrt(2.0f), kEps);
    EXPECT_NEAR(canvasScale(c, {960, 540}), 0.5f, kEps);
}

TEST(CanvasLayout, ZeroSizedTargetGivesScaleOne) {
    UICanvas c;
    c.scaleMode = UICanvas::ScaleMode::ScaleWithScreen;
    EXPECT_FLOAT_EQ(canvasScale(c, {0, 0}), 1.0f);
}

// ── layoutCanvas ────────────────────────────────────────────────

TEST(CanvasLayout, ScreenCanvasSizeIsScreenOverScale) {
    entt::registry registry;
    UICanvas c;
    c.scaleFactor = 2.0f;
    const entt::entity root = canvas(registry, c);

    layoutCanvas(registry, root, {1600, 900});

    const auto& laidOut = registry.get<UICanvas>(root);
    EXPECT_FLOAT_EQ(laidOut.scale, 2.0f);
    EXPECT_FLOAT_EQ(laidOut.size.x, 800.0f);
    EXPECT_FLOAT_EQ(laidOut.size.y, 450.0f);
    EXPECT_FLOAT_EQ(laidOut.targetSize.x, 1600.0f);
}

TEST(CanvasLayout, WorldCanvasUsesPixelSize) {
    entt::registry registry;
    UICanvas c;
    c.mode = UICanvas::Mode::WorldTexture;
    c.pixelSize = {512, 256};
    const entt::entity root = canvas(registry, c);
    const entt::entity full = stretch(registry, root);

    layoutCanvas(registry, root, {1600, 900});

    expectRectNear(registry.get<UIRect>(full).rect, {0, 0}, {512, 256});
}

TEST(CanvasLayout, NestedElementsAndClip) {
    entt::registry registry;
    const entt::entity root = canvas(registry);
    const entt::entity panel = stretch(registry, root, 100.0f);
    registry.emplace<UIClip>(panel);
    // Hangs 20 units off the panel's top-left corner.
    const entt::entity child = element(registry, panel, fixed({0, 0}, {0, 0}, {-20, -20}, {60, 60}));
    const entt::entity grandchild = stretch(registry, child, 5.0f);

    layoutCanvas(registry, root, {800, 600});

    const auto& p = registry.get<UIRect>(panel);
    expectRectNear(p.rect, {100, 100}, {700, 500});
    expectRectNear(p.clip, {0, 0}, {800, 600});  // a clip limits children, not the element

    const auto& c = registry.get<UIRect>(child);
    expectRectNear(c.rect, {80, 80}, {140, 140});
    expectRectNear(c.clip, {100, 100}, {700, 500});

    const auto& g = registry.get<UIRect>(grandchild);
    expectRectNear(g.rect, {85, 85}, {135, 135});
    expectRectNear(g.clip, {100, 100}, {700, 500});
}

TEST(CanvasLayout, NestedClipsIntersect) {
    entt::registry registry;
    const entt::entity root = canvas(registry);
    const entt::entity outer = element(registry, root, fixed({0, 0}, {0, 0}, {0, 0}, {300, 300}));
    registry.emplace<UIClip>(outer);
    const entt::entity inner = element(registry, outer, fixed({0, 0}, {0, 0}, {200, 200}, {300, 300}));
    registry.emplace<UIClip>(inner);
    const entt::entity leaf = stretch(registry, inner);

    layoutCanvas(registry, root, {800, 600});

    expectRectNear(registry.get<UIRect>(leaf).clip, {200, 200}, {300, 300});
}

TEST(CanvasLayout, DrawOrderIsDepthFirst) {
    entt::registry registry;
    const entt::entity root = canvas(registry);
    const entt::entity a = stretch(registry, root);
    const entt::entity a1 = stretch(registry, a);
    const entt::entity b = stretch(registry, root);

    layoutCanvas(registry, root, {800, 600});

    EXPECT_EQ(registry.get<UIRect>(a).drawOrder, 0u);
    EXPECT_EQ(registry.get<UIRect>(a1).drawOrder, 1u);
    EXPECT_EQ(registry.get<UIRect>(b).drawOrder, 2u);
    EXPECT_EQ(registry.get<UIRect>(b).canvas, root);
}

TEST(CanvasLayout, HiddenParentHidesSubtree) {
    entt::registry registry;
    const entt::entity root = canvas(registry);
    const entt::entity parent = stretch(registry, root);
    const entt::entity child = stretch(registry, parent);
    registry.get<UIRect>(parent).visible = false;

    layoutCanvas(registry, root, {800, 600});

    EXPECT_FALSE(registry.get<UIRect>(parent).visibleInHierarchy);
    EXPECT_FALSE(registry.get<UIRect>(child).visibleInHierarchy);
    // Rects are still resolved, so showing the parent needs no extra frame.
    expectRectNear(registry.get<UIRect>(child).rect, {0, 0}, {800, 600});
}

TEST(CanvasLayout, WalkSkipsNonUiEntitiesAndNestedCanvases) {
    entt::registry registry;
    const entt::entity root = canvas(registry);

    const entt::entity plain = registry.create();
    attach(registry, root, plain);
    const entt::entity underPlain = stretch(registry, plain);

    const entt::entity nested = canvas(registry);
    registry.emplace<UIRect>(nested);
    attach(registry, root, nested);
    const entt::entity underNested = stretch(registry, nested);

    layoutCanvas(registry, root, {800, 600});

    EXPECT_EQ(registry.get<UIRect>(underPlain).canvas, entt::entity{entt::null});
    EXPECT_EQ(registry.get<UIRect>(underNested).canvas, entt::entity{entt::null});
}

// ── CanvasLayoutSystem ──────────────────────────────────────────

TEST(CanvasLayoutSystem, PrioritySetAtConstruction) {
    const glm::vec2 screen(800, 600);
    CanvasLayoutSystem system(&screen);
    EXPECT_EQ(system.priority, CanvasLayoutSystem::kPriority);
    EXPECT_GT(system.priority, 0);
}

TEST(CanvasLayoutSystem, FollowsScreenSizeAndClearsDetachedElements) {
    entt::registry registry;
    glm::vec2 screen(800, 600);
    CanvasLayoutSystem system(&screen);

    const entt::entity root = canvas(registry);
    const entt::entity full = stretch(registry, root);

    system.update(registry, 0.016f);
    expectRectNear(registry.get<UIRect>(full).rect, {0, 0}, {800, 600});
    EXPECT_TRUE(registry.get<UIRect>(full).visibleInHierarchy);

    screen = {1024, 768};
    system.update(registry, 0.016f);
    expectRectNear(registry.get<UIRect>(full).rect, {0, 0}, {1024, 768});

    registry.get<ECS::HierarchyComponent>(root).children.clear();
    registry.get<ECS::HierarchyComponent>(full).parent = entt::null;
    system.update(registry, 0.016f);
    EXPECT_FALSE(registry.get<UIRect>(full).visibleInHierarchy);
    EXPECT_EQ(registry.get<UIRect>(full).canvas, entt::entity{entt::null});
}
