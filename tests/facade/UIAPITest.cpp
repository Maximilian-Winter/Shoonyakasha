//
// UIAPITest.cpp - The canvas UI facade over a registry, without a device
//

#include <entt/entt.hpp>
#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "Facade/UIAPI.h"
#include "UI/UIComponents.h"
#include "UI/UIContext.h"
#include "UI/UISystems.h"

#include <GLFW/glfw3.h>

#include <algorithm>

using namespace Shoonyakasha;
using namespace Shoonyakasha::Facade;

namespace {

struct UIAPIFixture : ::testing::Test {
    entt::registry registry;
    UI::UIContext context;
    UIAPI ui{registry, context};
    glm::vec2 screen{1280.0f, 720.0f};
    entt::entity input = registry.create();
    UI::CanvasInputSystem inputSystem{&screen, nullptr, &context.pointer()};   // keeps the press between frames

    UIAPIFixture() { registry.emplace<ECS::InputStateComponent>(input); }

    /// One frame: input against the last layout, then layout and text.
    void frame(glm::vec2 mouse = glm::vec2(-1.0f), bool down = false) {
        auto& state = registry.get<ECS::InputStateComponent>(input);
        state.mousePosition = mouse;
        state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
        inputSystem.update(registry, 0.0f);
        UI::CanvasLayoutSystem(&screen).update(registry, 0.0f);
        UI::CanvasTextSystem(&context.fonts()).update(registry, 0.0f);
    }

    entt::entity e(EntityHandle h) { return static_cast<entt::entity>(h); }
};

} // namespace

TEST_F(UIAPIFixture, CanvasesScaleToTheScreen) {
    const EntityHandle canvas = ui.createCanvas({640.0f, 360.0f}, CanvasScaleMode::ScaleWithScreen, 3);
    frame();
    EXPECT_EQ(ui.getCanvasSize(canvas), glm::vec2(640.0f, 360.0f));
    EXPECT_EQ(registry.get<UI::UICanvas>(e(canvas)).sortOrder, 3);

    ui.setCanvasScaling(canvas, CanvasScaleMode::ConstantPixel, {0.0f, 0.0f}, 2.0f);
    frame();
    EXPECT_EQ(ui.getCanvasSize(canvas), glm::vec2(640.0f, 360.0f));   // 1280 x 720 at 2 pixels per unit
}

TEST_F(UIAPIFixture, ElementsArePlacedByAnchorPositionAndSize) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle panel = ui.createPanel(canvas, {200.0f, 100.0f}, {0.1f, 0.1f, 0.1f, 1.0f});
    frame();
    EXPECT_EQ(ui.getRect(panel), glm::vec4(540.0f, 310.0f, 200.0f, 100.0f));   // centred

    ui.setAnchor(panel, {1.0f, 1.0f});
    ui.setPosition(panel, {-20.0f, -10.0f});
    ui.setSize(panel, {300.0f, 50.0f});
    frame();
    EXPECT_EQ(ui.getRect(panel), glm::vec4(960.0f, 660.0f, 300.0f, 50.0f));

    ui.setRect(panel, {0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 0.0f}, {0.0f, 8.0f}, {-16.0f, 40.0f});
    frame();
    EXPECT_EQ(ui.getRect(panel), glm::vec4(8.0f, 8.0f, 1264.0f, 40.0f));
}

TEST_F(UIAPIFixture, TextUsesTheDefaultFontAndButtonsExposeTheirLabel) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle label = ui.createText(canvas, "Hello", 30.0f);
    EXPECT_NE(ui.getDefaultFont(), 0u);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).font, ui.getDefaultFont());

    const EntityHandle button = ui.createButton(canvas, "Play");
    EXPECT_EQ(ui.getText(button), "Play");
    ui.setText(button, "Pause");
    EXPECT_EQ(ui.getText(button), "Pause");
    ui.setTextAlign(label, TextHAlign::Right, TextVAlign::Bottom);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).alignH, UI::TextAlignH::Right);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).alignV, UI::TextAlignV::Bottom);
    EXPECT_EQ(ui.loadFont("fonts/no-such-font.ttf"), 0u);
}

TEST_F(UIAPIFixture, ClicksTogglesAndSlidersReachTheApplication) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle button = ui.createButton(canvas, "Go", {100.0f, 40.0f});   // 590..690, 340..380
    frame();

    frame({640.0f, 360.0f}, true);
    EXPECT_TRUE(ui.isPressed(button));
    EXPECT_TRUE(ui.isPointerOverUI());
    EXPECT_EQ(ui.getHoveredElement(), button);
    EXPECT_EQ(ui.getPointerCanvas(), canvas);
    EXPECT_EQ(ui.getPointerPosition(), glm::vec2(640.0f, 360.0f));
    frame({640.0f, 360.0f}, false);
    EXPECT_TRUE(ui.wasClicked(button));

    ui.setVisible(button, false);
    const EntityHandle toggle = ui.createToggle(canvas, "Fog", true);
    EXPECT_TRUE(ui.getToggle(toggle));
    frame();
    frame({640.0f, 360.0f}, true);
    frame({640.0f, 360.0f}, false);
    EXPECT_FALSE(ui.getToggle(toggle));
    EXPECT_TRUE(ui.valueChanged(toggle));
    ui.setToggle(toggle, true);
    EXPECT_TRUE(ui.getToggle(toggle));

    const EntityHandle slider = ui.createSlider(canvas, 0.0f, 1.0f, 0.25f);
    ui.setSliderRange(slider, 0.0f, 10.0f, true);
    ui.setSliderValue(slider, 7.0f);
    EXPECT_FLOAT_EQ(ui.getSliderValue(slider), 7.0f);

    ui.setInteractable(slider, false);
    EXPECT_FALSE(registry.get<UI::UIInteractable>(e(slider)).enabled);
}

TEST_F(UIAPIFixture, ClipColourParentAndRaycastSettings) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle a = ui.createElement(canvas, {10.0f, 10.0f});
    const EntityHandle b = ui.createImage(canvas, {10.0f, 10.0f});

    ui.setClip(a, true);
    EXPECT_TRUE(registry.all_of<UI::UIClip>(e(a)));
    ui.setClip(a, false);
    EXPECT_FALSE(registry.all_of<UI::UIClip>(e(a)));

    ui.setColor(b, {1.0f, 0.0f, 0.0f, 0.5f});
    EXPECT_EQ(registry.get<UI::UIImage>(e(b)).color, glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    ui.setRaycastTarget(b, false);
    EXPECT_FALSE(registry.get<UI::UIImage>(e(b)).raycastTarget);
    EXPECT_FALSE(ui.setTexture(b, "textures/panel.png"));   // no texture loader in this fixture

    ui.setParent(b, a);
    EXPECT_EQ(registry.get<ECS::HierarchyComponent>(e(b)).parent, e(a));
    const auto& canvasChildren = registry.get<ECS::HierarchyComponent>(e(canvas)).children;
    EXPECT_EQ(std::count(canvasChildren.begin(), canvasChildren.end(), e(b)), 0);
}

TEST_F(UIAPIFixture, WorldCanvasesLayOutInTheirOwnPixels) {
    const EntityHandle canvas = ui.createWorldCanvas({512.0f, 256.0f}, {2.0f, 1.0f});
    const auto& c = registry.get<UI::UICanvas>(e(canvas));
    EXPECT_EQ(c.mode, UI::UICanvas::Mode::WorldTexture);
    frame();
    EXPECT_EQ(ui.getCanvasSize(canvas), glm::vec2(512.0f, 256.0f));

    // A new resolution keeps the layout: the canvas still measures 512 x 256.
    ui.setCanvasPixelSize(canvas, {1024.0f, 512.0f});
    ui.setCanvasClearColor(canvas, {0.0f, 0.0f, 0.0f, 0.0f});
    frame();
    EXPECT_EQ(ui.getCanvasSize(canvas), glm::vec2(512.0f, 256.0f));
    EXPECT_EQ(c.clearColor, glm::vec4(0.0f));
}

TEST_F(UIAPIFixture, InvalidHandlesAreIgnored) {
    ui.setText(NullEntity, "x");
    ui.setPosition(12345, {1.0f, 1.0f});
    EXPECT_EQ(ui.getText(NullEntity), "");
    EXPECT_FALSE(ui.wasClicked(12345));
    EXPECT_EQ(ui.getRect(NullEntity), glm::vec4(0.0f));
    EXPECT_FALSE(ui.isPointerOverUI());
    EXPECT_EQ(ui.getPointerCanvas(), NullEntity);
}
