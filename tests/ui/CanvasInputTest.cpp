//
// CanvasInputTest.cpp - Hit testing, CanvasInputSystem and the widgets
//
// Tier 2: EnTT registry, no GPU. Each step lays the canvases out and then runs
// the input system, as a frame does with last frame's rects.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/UIBatchBuilder.h"
#include "UI/UIHitTest.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

struct Fixture {
    entt::registry registry;
    glm::vec2 screen{800.0f, 600.0f};
    glm::vec2 window{800.0f, 600.0f};
    UIPointerState pointer;
    CanvasInputSystem input{&screen, &window, &pointer};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = entt::null;

    Fixture() {
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        canvas = registry.create();
        registry.emplace<UICanvas>(canvas);
    }

    /// An element at `position` (top-left, canvas units) of `size`.
    entt::entity at(entt::entity parent, glm::vec2 position, glm::vec2 size) {
        const entt::entity e = createElement(registry, parent, size);
        auto& rect = registry.get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = {0.0f, 0.0f};
        rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = position;
        return e;
    }

    void layout() { CanvasLayoutSystem(&screen).update(registry, 0.0f); }

    /// One frame with the mouse at `mouse` (window coordinates).
    void step(glm::vec2 mouse, bool down) {
        auto& state = registry.get<ECS::InputStateComponent>(inputEntity);
        state.mousePosition = mouse;
        state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
        layout();
        input.update(registry, 0.0f);
    }

    UIInteractable& ui(entt::entity e) { return registry.get<UIInteractable>(e); }
};

} // namespace

// ─── Hit testing ────────────────────────────────────────────────

TEST(UIHitTest, TopmostRaycastTargetWins) {
    Fixture f;
    const auto back = f.at(f.canvas, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(back);
    const auto front = f.at(f.canvas, {50, 50}, {100, 100});
    f.registry.emplace<UIPanel>(front);
    f.layout();

    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {75, 75}), front);
    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {25, 25}), back);
    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {300, 300}), entt::entity{entt::null});
}

TEST(UIHitTest, TextAndNonTargetsLetThePointerThrough) {
    Fixture f;
    const auto back = f.at(f.canvas, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(back);
    const auto label = f.at(f.canvas, {0, 0}, {100, 100});
    f.registry.emplace<UIText>(label).text = "x";
    const auto glass = f.at(f.canvas, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(glass).raycastTarget = false;
    f.layout();

    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {50, 50}), back);
}

TEST(UIHitTest, ClippedAndHiddenElementsAreNotHit) {
    Fixture f;
    const auto clip = f.at(f.canvas, {0, 0}, {50, 50});
    f.registry.emplace<UIClip>(clip);
    const auto inside = f.at(clip, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(inside);
    const auto hidden = f.at(f.canvas, {200, 0}, {50, 50});
    f.registry.emplace<UIImage>(hidden);
    f.registry.get<UIRect>(hidden).visible = false;
    f.layout();

    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {25, 25}), inside);
    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {75, 75}), entt::entity{entt::null});
    EXPECT_EQ(hitTestCanvas(f.registry, f.canvas, {225, 25}), entt::entity{entt::null});
}

TEST(UIHitTest, ChildrenActForTheirInteractableAncestor) {
    Fixture f;
    const auto button = createButton(f.registry, f.canvas, "OK", 0);
    const auto icon = createElement(f.registry, button, {10, 10});
    f.registry.emplace<UIImage>(icon);
    EXPECT_EQ(interactableFor(f.registry, icon), button);
    EXPECT_EQ(interactableFor(f.registry, button), button);
    EXPECT_EQ(interactableFor(f.registry, f.canvas), entt::entity{entt::null});
}

TEST(UIHitTest, OverlayDrawnLastIsTestedFirst) {
    Fixture f;
    const auto low = f.at(f.canvas, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(low);
    const auto top = f.registry.create();
    f.registry.emplace<UICanvas>(top).sortOrder = 5;
    const auto high = f.at(top, {0, 0}, {100, 100});
    f.registry.emplace<UIImage>(high);
    f.layout();

    const auto hit = findPointerHit(f.registry, {50, 50}, std::nullopt);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->canvas, top);
    EXPECT_EQ(hit->element, high);
}

// ─── Buttons ────────────────────────────────────────────────────

TEST(CanvasInput, ClickIsAPressAndReleaseOverTheSameElement) {
    Fixture f;
    const auto button = f.at(f.canvas, {100, 100}, {100, 40});
    f.registry.emplace<UIInteractable>(button);
    f.registry.emplace<UIButton>(button);

    f.step({150, 120}, false);
    EXPECT_TRUE(f.ui(button).hovered);
    EXPECT_FALSE(f.ui(button).pressed);
    EXPECT_TRUE(f.pointer.overUI);
    EXPECT_EQ(f.pointer.hovered, button);

    f.step({150, 120}, true);
    EXPECT_TRUE(f.ui(button).pressed);
    EXPECT_FALSE(f.ui(button).clicked);
    EXPECT_EQ(f.pointer.pressed, button);

    f.step({150, 120}, false);
    EXPECT_TRUE(f.ui(button).clicked);
    EXPECT_FALSE(f.ui(button).pressed);

    f.step({150, 120}, false);
    EXPECT_FALSE(f.ui(button).clicked);   // one frame only
}

TEST(CanvasInput, ReleaseElsewhereIsNotAClick) {
    Fixture f;
    const auto button = f.at(f.canvas, {100, 100}, {100, 40});
    f.registry.emplace<UIInteractable>(button);

    f.step({150, 120}, true);
    f.step({500, 500}, true);
    EXPECT_TRUE(f.ui(button).pressed);    // still captured
    EXPECT_FALSE(f.ui(button).hovered);
    EXPECT_TRUE(f.pointer.overUI);
    f.step({500, 500}, false);
    EXPECT_FALSE(f.ui(button).clicked);
    EXPECT_FALSE(f.pointer.overUI);
}

TEST(CanvasInput, PressStartedOutsideDoesNotPressAnElement) {
    Fixture f;
    const auto button = f.at(f.canvas, {100, 100}, {100, 40});
    f.registry.emplace<UIInteractable>(button);

    f.step({500, 500}, true);
    f.step({150, 120}, true);
    EXPECT_FALSE(f.ui(button).pressed);
    f.step({150, 120}, false);
    EXPECT_FALSE(f.ui(button).clicked);
}

TEST(CanvasInput, DisabledElementsIgnoreThePointer) {
    Fixture f;
    const auto button = f.at(f.canvas, {100, 100}, {100, 40});
    f.registry.emplace<UIInteractable>(button).enabled = false;

    f.step({150, 120}, true);
    f.step({150, 120}, false);
    EXPECT_FALSE(f.ui(button).hovered);
    EXPECT_FALSE(f.ui(button).clicked);
}

TEST(CanvasInput, CursorIsScaledFromWindowToFramebufferPixels) {
    Fixture f;
    f.window = {400.0f, 300.0f};   // a 2x display
    const auto button = f.at(f.canvas, {300, 220}, {40, 40});
    f.registry.emplace<UIInteractable>(button);

    f.step({160, 120}, false);     // (320, 240) in pixels
    EXPECT_TRUE(f.ui(button).hovered);
}

TEST(CanvasInput, CapturedMouseUsesTheScreenCentre) {
    Fixture f;
    const auto button = f.at(f.canvas, {380, 280}, {40, 40});
    f.registry.emplace<UIInteractable>(button);
    f.registry.get<ECS::InputStateComponent>(f.inputEntity).mouseCaptured = true;

    f.step({0, 0}, false);
    EXPECT_TRUE(f.ui(button).hovered);
}

// ─── Toggles and sliders ────────────────────────────────────────

TEST(CanvasInput, ToggleFlipsOnClickAndShowsItsCheckmark) {
    Fixture f;
    const auto toggle = createToggle(f.registry, f.canvas, "Fog", 0);
    const auto checkmark = f.registry.get<UIToggle>(toggle).checkmark;
    EXPECT_FALSE(f.registry.get<UIRect>(checkmark).visible);

    // The toggle row is centred: 200 x 28 around (400, 300).
    f.step({400, 300}, true);
    f.step({400, 300}, false);
    EXPECT_TRUE(f.registry.get<UIToggle>(toggle).isOn);
    EXPECT_TRUE(f.ui(toggle).valueChanged);
    EXPECT_TRUE(f.registry.get<UIRect>(checkmark).visible);

    f.step({400, 300}, false);
    EXPECT_FALSE(f.ui(toggle).valueChanged);
}

TEST(CanvasInput, SliderFollowsADragBeyondItsEnds) {
    Fixture f;
    const auto slider = createSlider(f.registry, f.canvas, 0.0f, 10.0f, 0.0f);   // 200 wide, x 300..500
    const auto& state = f.registry.get<UISlider>(slider);

    f.step({350, 300}, true);
    EXPECT_FLOAT_EQ(state.value, 2.5f);
    EXPECT_TRUE(f.ui(slider).valueChanged);

    f.step({350, 300}, true);
    EXPECT_FALSE(f.ui(slider).valueChanged);   // no movement, no change

    f.step({900, 0}, true);                    // far outside: clamped
    EXPECT_FLOAT_EQ(state.value, 10.0f);
    EXPECT_FLOAT_EQ(f.registry.get<UIRect>(state.fill).anchorMax.x, 1.0f);
    EXPECT_FLOAT_EQ(f.registry.get<UIRect>(state.handle).anchorMin.x, 1.0f);
    f.step({900, 0}, false);
}

TEST(CanvasInput, SliderRoundsToWholeNumbersAndPlacesItsHandleFromItsValue) {
    Fixture f;
    const auto slider = createSlider(f.registry, f.canvas, 0.0f, 4.0f, 0.0f);
    auto& state = f.registry.get<UISlider>(slider);
    state.wholeNumbers = true;

    f.step({361, 300}, true);   // 0.305 of the way: 1.22
    EXPECT_FLOAT_EQ(state.value, 1.0f);
    f.step({361, 300}, false);

    state.value = 3.0f;         // set by the application
    f.step({0, 0}, false);
    EXPECT_FLOAT_EQ(f.registry.get<UIRect>(state.handle).anchorMax.x, 0.75f);
}

TEST(CanvasInput, StateColourTintsTheInteractablesOwnPanel) {
    Fixture f;
    const auto button = f.at(f.canvas, {0, 0}, {100, 40});
    f.registry.emplace<UIPanel>(button).color = {1.0f, 0.5f, 0.0f, 1.0f};
    auto& interactable = f.registry.emplace<UIInteractable>(button);
    interactable.pressedColor = {0.5f, 0.5f, 0.5f, 1.0f};

    f.step({50, 20}, true);
    FontLibrary fonts;
    GlyphCache glyphs(64);
    UIDrawData data;
    buildDrawData(f.registry, fonts, glyphs, data);
    ASSERT_FALSE(data.vertices.empty());
    EXPECT_EQ(data.vertices[0].color, packColor({0.5f, 0.25f, 0.0f, 1.0f}));
}

// ─── World canvases ─────────────────────────────────────────────

namespace {

/// A main camera at (0, 0, 5) looking down -Z.
void addCamera(entt::registry& registry, glm::vec2 screen) {
    const auto camera = registry.create();
    auto& c = registry.emplace<ECS::CameraComponent>(camera);
    c.isMainCamera = true;
    c.viewMatrix = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    c.projectionMatrix = glm::perspective(glm::radians(60.0f), screen.x / screen.y, 0.1f, 100.0f);
}

/// A 400 x 200 world canvas on a 2 x 1 quad at `z`, facing +Z (or -Z).
entt::entity addWorldCanvas(entt::registry& registry, float z, bool facingCamera = true) {
    const auto canvas = registry.create();
    auto& c = registry.emplace<UICanvas>(canvas);
    c.mode = UICanvas::Mode::WorldTexture;
    c.pixelSize = {400.0f, 200.0f};
    glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, z));
    if (!facingCamera) world = glm::rotate(world, glm::pi<float>(), glm::vec3(0, 1, 0));
    registry.emplace<ECS::TransformComponent>(canvas).worldMatrix = glm::scale(world, glm::vec3(2, 1, 1));
    return canvas;
}

} // namespace

TEST(CanvasInput, WorldCanvasIsHitThroughTheCameraRay) {
    Fixture f;
    addCamera(f.registry, f.screen);
    const auto world = addWorldCanvas(f.registry, 0.0f);
    const auto button = f.at(world, {150, 75}, {100, 50});   // the middle of the canvas
    f.registry.emplace<UIInteractable>(button);

    f.step({400, 300}, false);
    EXPECT_TRUE(f.ui(button).hovered);
    EXPECT_EQ(f.pointer.canvas, world);
    EXPECT_NEAR(f.pointer.position.x, 200.0f, 0.01f);
    EXPECT_NEAR(f.pointer.position.y, 100.0f, 0.01f);
}

TEST(CanvasInput, NearestWorldCanvasHidesTheOnesBehindIt) {
    Fixture f;
    addCamera(f.registry, f.screen);
    const auto farCanvas = addWorldCanvas(f.registry, -2.0f);
    const auto farButton = f.at(farCanvas, {0, 0}, {400, 200});
    f.registry.emplace<UIInteractable>(farButton);
    const auto nearCanvas = addWorldCanvas(f.registry, 0.0f);   // nothing on it

    f.step({400, 300}, false);
    EXPECT_FALSE(f.ui(farButton).hovered);
    EXPECT_EQ(f.pointer.canvas, nearCanvas);
    EXPECT_FALSE(f.pointer.overUI);
}

TEST(CanvasInput, WorldCanvasSeenFromBehindIsNotHit) {
    Fixture f;
    addCamera(f.registry, f.screen);
    const auto world = addWorldCanvas(f.registry, 0.0f, false);
    const auto button = f.at(world, {0, 0}, {400, 200});
    f.registry.emplace<UIInteractable>(button);

    f.step({400, 300}, false);
    EXPECT_FALSE(f.ui(button).hovered);
}

TEST(CanvasInput, WorldSliderFollowsADragOffItsQuad) {
    Fixture f;
    addCamera(f.registry, f.screen);
    const auto world = addWorldCanvas(f.registry, 0.0f);
    const auto slider = createSlider(f.registry, world, 0.0f, 1.0f, 0.0f, {400.0f, 200.0f});   // fills the canvas

    f.step({400, 300}, true);
    EXPECT_NEAR(f.registry.get<UISlider>(slider).value, 0.5f, 1e-3f);
    f.step({799, 300}, true);   // right of the quad, which spans about x 296..504
    EXPECT_FLOAT_EQ(f.registry.get<UISlider>(slider).value, 1.0f);
}

TEST(CameraRay, QuadPlaneHitRunsPastTheQuad) {
    ECS::Ray ray;
    ray.origin = {1.5f, 0.0f, 5.0f};
    ray.direction = {0.0f, 0.0f, -1.0f};
    EXPECT_FALSE(ECS::intersectUnitQuad(ray, glm::mat4(1.0f)));
    const auto hit = ECS::intersectQuadPlane(ray, glm::mat4(1.0f));
    ASSERT_TRUE(hit);
    EXPECT_FLOAT_EQ(hit->uv.x, 2.0f);
    EXPECT_FLOAT_EQ(hit->uv.y, 0.5f);
}
