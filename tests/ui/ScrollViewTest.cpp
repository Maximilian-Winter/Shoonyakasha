//
// ScrollViewTest.cpp - Scroll views, scrollbars and their input
//
// Tier 2: EnTT registry, no GPU. Each step runs a frame: input against the
// last layout, then layout.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/CanvasLayout.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <GLFW/glfw3.h>

#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Fixture {
    entt::registry registry;
    glm::vec2 screen{800.0f, 600.0f};
    UIPointerState pointer;
    CanvasInputSystem input{&screen, nullptr, &pointer};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();
    entt::entity view = entt::null;

    /// A 200 x 100 view at (100, 100) with `rows` rows of 50 in its column.
    explicit Fixture(int rows = 8) {
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas);
        view = createScrollView(registry, canvas, {200.0f, 100.0f});
        place(view, {100.0f, 100.0f});
        for (int i = 0; i < rows; ++i) row(50.0f);
        layout();
    }

    void place(entt::entity e, glm::vec2 position) {
        auto& rect = registry.get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = position;
    }

    entt::entity content() { return scrollView().content; }
    UIScrollView& scrollView() { return registry.get<UIScrollView>(view); }

    entt::entity row(float height) {
        const entt::entity e = createElement(registry, content(), {0.0f, height});
        registry.emplace<UIPanel>(e).raycastTarget = false;
        return e;
    }

    void layout() { CanvasLayoutSystem(&screen).update(registry, 0.0f); }

    void step(glm::vec2 mouse, bool down, glm::vec2 wheel = glm::vec2(0.0f)) {
        auto& state = registry.get<ECS::InputStateComponent>(inputEntity);
        state.mousePosition = mouse;
        state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
        state.scrollDelta = wheel;
        input.update(registry, kDt);
        layout();
    }

    float offset() { return scrollView().scrollPosition.y * scrollView().range().y; }
    const Rect& rect(entt::entity e) { return registry.get<UIRect>(e).rect; }
};

} // namespace

TEST(ScrollView, LayoutMovesTheContentByTheScrollOffset) {
    Fixture f;   // 8 rows of 50: 400 tall in a 100 tall view
    EXPECT_EQ(f.scrollView().contentSize, glm::vec2(188.0f, 400.0f));   // the view less its scrollbar
    EXPECT_EQ(f.scrollView().viewportSize, glm::vec2(200.0f, 100.0f));
    EXPECT_FLOAT_EQ(f.rect(f.content()).min.y, 100.0f);

    f.scrollView().scrollPosition.y = 0.5f;
    f.layout();
    EXPECT_FLOAT_EQ(f.rect(f.content()).min.y, 100.0f - 150.0f);
    EXPECT_FLOAT_EQ(f.rect(f.content()).min.x, 100.0f);   // not scrolling sideways

    f.scrollView().scrollPosition.y = 3.0f;   // clamped
    f.layout();
    EXPECT_FLOAT_EQ(f.scrollView().scrollPosition.y, 1.0f);
}

TEST(ScrollView, WheelScrollsTheViewUnderThePointer) {
    Fixture f;
    f.step({200, 150}, false, {0.0f, -1.0f});    // towards the user: on down the content
    EXPECT_FLOAT_EQ(f.offset(), 48.0f);
    f.step({200, 150}, false, {0.0f, 2.0f});     // back up, stopping at the start
    EXPECT_FLOAT_EQ(f.offset(), 0.0f);
    f.step({600, 500}, false, {0.0f, -1.0f});    // not over the view
    EXPECT_FLOAT_EQ(f.offset(), 0.0f);
    for (int i = 0; i < 20; ++i) f.step({200, 150}, false, {0.0f, -1.0f});
    EXPECT_FLOAT_EQ(f.offset(), 300.0f);         // stops at the end
}

TEST(ScrollView, WheelPassesToTheOuterViewWhenTheInnerCannotMove) {
    Fixture f(0);
    const entt::entity inner = createScrollView(f.registry, f.content(), {150.0f, 60.0f});
    for (int i = 0; i < 2; ++i) {
        const entt::entity e = createElement(f.registry, f.registry.get<UIScrollView>(inner).content, {0.0f, 50.0f});
        f.registry.emplace<UIPanel>(e).raycastTarget = false;
    }
    for (int i = 0; i < 6; ++i) f.row(50.0f);
    f.layout();
    f.step({150, 120}, false);   // the inner view is at the top of the outer one's column

    f.step({150, 120}, false, {0.0f, -1.0f});
    auto& innerView = f.registry.get<UIScrollView>(inner);
    EXPECT_FLOAT_EQ(innerView.scrollPosition.y, 1.0f);   // 100 in 60: 40 to scroll, all used
    EXPECT_FLOAT_EQ(f.offset(), 0.0f);
    f.step({150, 120}, false, {0.0f, -1.0f});
    EXPECT_FLOAT_EQ(f.offset(), 48.0f);                  // the inner one is at its end
}

TEST(ScrollView, DraggingPastTheThresholdScrollsAndCancelsAButtonPress) {
    Fixture f(0);
    const entt::entity button = createButton(f.registry, f.content(), "B", 0, {100.0f, 40.0f});
    for (int i = 0; i < 6; ++i) f.row(50.0f);
    f.layout();

    f.step({150, 120}, true);                    // press on the button
    EXPECT_TRUE(f.registry.get<UIInteractable>(button).pressed);
    f.step({150, 115}, true);                    // 5 units: not yet a drag
    EXPECT_FLOAT_EQ(f.offset(), 0.0f);
    EXPECT_TRUE(f.registry.get<UIInteractable>(button).pressed);
    f.step({150, 90}, true);                     // 30 up: the content follows
    EXPECT_FLOAT_EQ(f.offset(), 30.0f);
    EXPECT_FALSE(f.registry.get<UIInteractable>(button).pressed);
    EXPECT_TRUE(f.pointer.overUI);
    f.scrollView().inertia = false;
    f.step({150, 90}, false);
    EXPECT_FALSE(f.registry.get<UIInteractable>(button).clicked);
    EXPECT_FLOAT_EQ(f.offset(), 30.0f);
}

TEST(ScrollView, AClickWithoutMovingStillClicks) {
    Fixture f(0);
    const entt::entity button = createButton(f.registry, f.content(), "B", 0, {100.0f, 40.0f});
    for (int i = 0; i < 6; ++i) f.row(50.0f);
    f.layout();
    f.step({150, 120}, true);
    f.step({152, 118}, false);
    EXPECT_TRUE(f.registry.get<UIInteractable>(button).clicked);
}

TEST(ScrollView, ReleasedDragsCoastAndSlowDown) {
    Fixture f;
    f.step({200, 190}, true);
    for (int y = 180; y >= 140; y -= 10) f.step({200, static_cast<float>(y)}, true);   // 600 units/s upwards
    const float released = f.offset();
    f.step({200, 140}, false);
    const float velocity = f.scrollView().velocity.y;
    EXPECT_GT(velocity, 100.0f);

    f.step({200, 140}, false);
    EXPECT_GT(f.offset(), released);
    EXPECT_LT(f.scrollView().velocity.y, velocity);
    for (int i = 0; i < 600 && f.scrollView().velocity.y != 0.0f; ++i) f.step({200, 140}, false);
    EXPECT_EQ(f.scrollView().velocity.y, 0.0f);   // stopped, by slowing down or at the end
}

TEST(ScrollView, ASliderInsideKeepsItsDrag) {
    Fixture f(0);
    const entt::entity slider = createSlider(f.registry, f.content(), 0.0f, 1.0f, 0.0f, {180.0f, 24.0f});
    for (int i = 0; i < 6; ++i) f.row(50.0f);
    f.layout();
    f.step({110, 112}, true);
    f.step({400, 60}, true);                     // far up, and right past the slider's end
    EXPECT_FLOAT_EQ(f.offset(), 0.0f);
    EXPECT_FLOAT_EQ(f.registry.get<UISlider>(slider).value, 1.0f);
}

TEST(ScrollView, ScrollbarShowsTheVisibleFractionAndScrolls) {
    Fixture f;
    const entt::entity bar = f.scrollView().verticalScrollbar;
    f.step({0, 0}, false);
    const auto& scrollbar = f.registry.get<UIScrollbar>(bar);
    EXPECT_FLOAT_EQ(scrollbar.size, 0.25f);
    EXPECT_TRUE(f.registry.get<UIRect>(bar).visible);
    // The bar runs down the view's right edge: x 290..300, y 100..200; its
    // handle is the top quarter.
    EXPECT_FLOAT_EQ(f.rect(scrollbar.handle).max.y, 125.0f);

    f.step({295, 110}, true);                    // take the handle 10 below its top
    f.step({295, 147.5f}, true);                 // move it halfway down its travel of 75
    EXPECT_NEAR(f.scrollView().scrollPosition.y, 0.5f, 1e-4f);
    f.step({295, 147.5f}, false);
    f.layout();
    EXPECT_NEAR(f.rect(scrollbar.handle).min.y, 137.5f, 1e-3f);

    f.step({295, 195}, true);                    // the track below the handle: a page down
    EXPECT_NEAR(f.scrollView().scrollPosition.y, 0.75f, 1e-4f);
    f.step({295, 195}, false);
}

TEST(ScrollView, ScrollbarHidesWhileTheContentFits) {
    Fixture f(1);
    f.step({0, 0}, false);
    f.layout();
    const entt::entity bar = f.scrollView().verticalScrollbar;
    EXPECT_FLOAT_EQ(f.registry.get<UIScrollbar>(bar).size, 1.0f);
    EXPECT_FALSE(f.registry.get<UIRect>(bar).visible);
}

TEST(ScrollView, HorizontalViewsScrollSidewaysWithEitherWheel) {
    Fixture f(0);
    const entt::entity strip = createScrollView(f.registry, f.canvas, {200.0f, 60.0f}, true, false);
    f.place(strip, {300.0f, 300.0f});
    auto& content = f.registry.get<UIScrollView>(strip).content;
    f.registry.remove<UIVerticalLayout>(content);
    f.registry.emplace<UIHorizontalLayout>(content);
    for (int i = 0; i < 5; ++i) {
        const entt::entity e = createElement(f.registry, content, {100.0f, 50.0f});
        f.registry.emplace<UIPanel>(e).raycastTarget = false;
    }
    f.layout();
    const auto& view = f.registry.get<UIScrollView>(strip);
    EXPECT_FLOAT_EQ(view.range().x, 300.0f);

    f.step({350, 320}, false, {0.0f, -1.0f});
    EXPECT_FLOAT_EQ(view.scrollPosition.x * view.range().x, 48.0f);
    f.step({350, 320}, false, {1.0f, 0.0f});
    EXPECT_FLOAT_EQ(view.scrollPosition.x * view.range().x, 96.0f);
}
