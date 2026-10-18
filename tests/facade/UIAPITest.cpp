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
#include <vector>
#include <limits>

TEST(ToolkitGeometry, ValidationIsAtomicAndPartsHaveStableMeaning) {
    using namespace Shoonyakasha;
    using namespace Shoonyakasha::Facade;
    entt::registry registry;
    UI::UIContext context;
    UIAPI ui{registry, context};
    auto canvas = ui.createCanvas();
    auto e = ui.createElement(canvas, {100, 100});
    std::vector<glm::vec2> points{{0, 0}, {10, 0}, {0, 10}};
    std::vector<uint32_t> indices{0, 1, 2};
    std::vector<glm::vec4> colors(3, glm::vec4(1));
    ui.setGeometry(e, points, indices, colors);
    EXPECT_THROW(ui.setGeometry(e, points, {0, 1, 9}, colors), std::invalid_argument);
    EXPECT_THROW(ui.setGeometry(e, points, {0, 1}, colors), std::invalid_argument);
    points[0].x = std::numeric_limits<float>::infinity();
    EXPECT_THROW(ui.setGeometry(e, points, indices, colors), std::invalid_argument);
    EXPECT_EQ(registry.get<UI::UIGeometry>(static_cast<entt::entity>(e)).positions[0].x, 0);
    ui.setGeometry(e, {}, {}, {});
    EXPECT_TRUE(registry.get<UI::UIGeometry>(static_cast<entt::entity>(e)).indices.empty());

    auto slider = ui.createSlider(canvas, 0, 1, 0.5f);
    ui.setWidgetPartColor(slider, "track", {1, 0, 0, 1});
    ui.setWidgetPartColor(slider, "fill", {0, 1, 0, 1});
    const auto& parts = registry.get<UI::UIWidgetParts>(static_cast<entt::entity>(slider));
    EXPECT_EQ(registry.get<UI::UIPanel>(parts.track).color, glm::vec4(1, 0, 0, 1));
    EXPECT_EQ(registry.get<UI::UIImage>(parts.fill).color, glm::vec4(0, 1, 0, 1));
    EXPECT_THROW(ui.setWidgetPartColor(slider, "unknown", glm::vec4(1)), std::invalid_argument);
    ui.setInteractionColors(slider, glm::vec4(1), glm::vec4(2), glm::vec4(3), glm::vec4(4), glm::vec4(5));
    EXPECT_EQ(registry.get<UI::UIInteractable>(static_cast<entt::entity>(slider)).focusedColor, glm::vec4(4));
}

using namespace Shoonyakasha;
using namespace Shoonyakasha::Facade;

namespace {

struct UIAPIFixture : ::testing::Test {
    entt::registry registry;
    UI::UIContext context;
    UIAPI ui{registry, context};
    glm::vec2 screen{1280.0f, 720.0f};
    entt::entity input = registry.create();
    UI::CanvasInputSystem inputSystem{&screen, nullptr, &context.pointer(), &context.focus(),
                                      &context.clipboard()};   // keeps the press between frames

    UIAPIFixture() { registry.emplace<ECS::InputStateComponent>(input); }

    /// One frame: input against the last layout, then layout and text.
    void frame(glm::vec2 mouse = glm::vec2(-1.0f), bool down = false) {
        auto& state = registry.get<ECS::InputStateComponent>(input);
        state.mousePosition = mouse;
        state.mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = down;
        inputSystem.update(registry, 0.0f);
        state.keyPresses.clear();   // presses and typing last one frame
        state.textInput.clear();
        UI::CanvasLayoutSystem(&screen, &context.fonts()).update(registry, 0.0f);
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
    ui.setTextAlign(label, TextHAlign::Start);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).alignH, UI::TextAlignH::Start);
    ui.setTextAlign(label, TextHAlign::End);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).alignH, UI::TextAlignH::End);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).direction, UI::TextDirection::Auto);
    ui.setTextDirection(label, TextDirection::RightToLeft);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).direction, UI::TextDirection::RightToLeft);
    EXPECT_EQ(ui.loadFont("fonts/no-such-font.ttf"), 0u);
}

TEST_F(UIAPIFixture, FontFallbacksReachTheFontLibrary) {
    const uint32_t latin = ui.getDefaultFont();
    const uint32_t deva = ui.loadFont("fonts/NotoSansDevanagari-Regular.ttf");
    const uint32_t tibetan = ui.loadFont("fonts/NotoSerifTibetan.ttf");
    ASSERT_TRUE(latin && deva && tibetan);
    ui.setDefaultFontFallbacks({deva, tibetan});
    EXPECT_EQ(context.fonts().chain(latin), (std::vector<uint32_t>{latin, deva, tibetan}));
    ui.setFontFallbacks(latin, {tibetan});
    EXPECT_EQ(context.fonts().chain(latin), (std::vector<uint32_t>{latin, tibetan}));
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

TEST_F(UIAPIFixture, MultiLineTextFields) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle field = ui.createTextField(canvas, "Notes", {240.0f, 120.0f});
    const auto& f = registry.get<UI::UITextField>(e(field));
    EXPECT_FALSE(f.multiLine);
    ui.setTextFieldMultiLine(field);
    EXPECT_TRUE(f.multiLine);
    EXPECT_FALSE(f.submitOnEnter);
    ui.setTextFieldMultiLine(field, true, true);
    EXPECT_TRUE(f.submitOnEnter);
    ui.setText(field, "one\ntwo");
    frame();
    EXPECT_EQ(ui.getText(field), "one\ntwo");
    EXPECT_TRUE(registry.get<UI::UIText>(f.textElement).wrap);
    ui.setTextFieldMultiLine(field, false);
    EXPECT_FALSE(f.multiLine);
    EXPECT_TRUE(f.submitOnEnter);
}

TEST_F(UIAPIFixture, RichText) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle label = ui.createText(canvas, "<b>bold</b> <link=more>more</link>");
    ui.setRichText(label);
    ui.setLinkColor(label, {1.0f, 0.5f, 0.0f, 1.0f});
    const auto& t = registry.get<UI::UIText>(e(label));
    EXPECT_TRUE(t.richText);
    EXPECT_EQ(t.linkColor, glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
    frame();
    EXPECT_EQ(registry.get<UI::UITextCache>(e(label)).links, std::vector<std::string>{"more"});
    EXPECT_EQ(ui.clickedLink(label), "");
    registry.get<UI::UIText>(e(label)).clickedLink = "more";
    EXPECT_EQ(ui.clickedLink(label), "more");
    EXPECT_EQ(ui.hoveredLink(label), "");

    const uint32_t font = ui.getDefaultFont();
    ui.setFontName(font, "body");
    EXPECT_EQ(context.fonts().find("body"), font);
    EXPECT_EQ(context.fonts().find("Roboto-Regular"), font);
    ui.setFontStyles(font, font);
    EXPECT_FALSE(context.fonts().styled(font, true, false).fauxBold);
}

TEST_F(UIAPIFixture, TextOrientation) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle label = ui.createText(canvas, "12");
    const auto& t = registry.get<UI::UIText>(e(label));
    EXPECT_EQ(t.orientation, UI::TextOrientation::Horizontal);
    ui.setTextOrientation(label, TextOrientation::VerticalRightToLeft, true);
    EXPECT_EQ(t.orientation, UI::TextOrientation::VerticalRightToLeft);
    EXPECT_TRUE(t.uprightDigits);
    ui.setTextOrientation(label, TextOrientation::VerticalLeftToRight);
    EXPECT_EQ(t.orientation, UI::TextOrientation::VerticalLeftToRight);
    EXPECT_FALSE(t.uprightDigits);
    frame();
    EXPECT_TRUE(registry.get<UI::UITextCache>(e(label)).layout.vertical);
    ui.setTextOrientation(label, TextOrientation::Horizontal);
    frame();
    EXPECT_FALSE(registry.get<UI::UITextCache>(e(label)).layout.vertical);
}

TEST_F(UIAPIFixture, Accessible) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle slider = ui.createSlider(canvas, 0.0f, 1.0f, 0.5f);
    ui.setAccessible(slider, "Volume", "How loud the music is");
    const auto& a = registry.get<UI::UIAccessible>(e(slider));
    EXPECT_EQ(a.name, "Volume");
    EXPECT_EQ(a.description, "How loud the music is");
    EXPECT_FALSE(a.hidden);
    EXPECT_FALSE(a.role.has_value());
    ui.setAccessible(slider, "", "", true, AccessibleRole::Button);
    EXPECT_TRUE(a.hidden);
    EXPECT_EQ(a.role, UI::AccessibleRole::Button);
    ui.setAccessible(slider, "Volume");
    EXPECT_FALSE(a.role.has_value());
}

TEST_F(UIAPIFixture, TextFieldComposition) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle field = ui.createTextField(canvas, "Name");
    EXPECT_EQ(ui.getTextFieldComposition(field), "");
    registry.get<UI::UITextField>(e(field)).composition.text = std::u32string{0x304B, 0x3093};   // ka n
    EXPECT_EQ(ui.getTextFieldComposition(field), "\xE3\x81\x8B\xE3\x82\x93");
    EXPECT_EQ(ui.getText(field), "");                // not part of the text
    EXPECT_EQ(ui.getTextFieldComposition(canvas), "");
}

TEST_F(UIAPIFixture, LayoutDirection) {
    const EntityHandle canvas = ui.createCanvas();
    EXPECT_FALSE(ui.isRightToLeft(canvas));
    ui.setLayoutDirection(canvas, LayoutDirection::RightToLeft);
    EXPECT_EQ(registry.get<UI::UICanvas>(e(canvas)).layoutDirection, UI::LayoutDirection::RightToLeft);
    EXPECT_TRUE(ui.isRightToLeft(canvas));

    const EntityHandle panel = ui.createElement(canvas, {100.0f, 100.0f});
    const EntityHandle icon = ui.createImage(panel, {16.0f, 16.0f});
    ui.setLayoutDirection(panel, LayoutDirection::LeftToRight);
    EXPECT_EQ(registry.get<UI::UIRect>(e(panel)).layoutDirection, UI::LayoutDirection::LeftToRight);
    frame();
    EXPECT_FALSE(ui.isRightToLeft(panel));
    EXPECT_FALSE(ui.isRightToLeft(icon));   // inherited from the panel
    ui.setLayoutDirection(panel, LayoutDirection::Inherit);
    frame();
    EXPECT_TRUE(ui.isRightToLeft(icon));    // now from the canvas

    ui.setMirrorInRightToLeft(icon, true);
    EXPECT_TRUE(registry.get<UI::UIImage>(e(icon)).mirrorInRightToLeft);
    const EntityHandle label = ui.createText(canvas, "Play");
    ui.setTextDirection(label, TextDirection::FromLayout);
    EXPECT_EQ(registry.get<UI::UIText>(e(label)).direction, UI::TextDirection::FromLayout);
}

TEST_F(UIAPIFixture, TintTargets) {
    const EntityHandle canvas = ui.createCanvas();
    const EntityHandle button = ui.createButton(canvas, "Go");
    const std::vector<EntityHandle> label = ui.getTintTargets(button);
    ASSERT_EQ(label.size(), 1u);
    EXPECT_TRUE(registry.all_of<UI::UIText>(e(label[0])));

    const EntityHandle icon = ui.createImage(canvas, {16.0f, 16.0f});
    ui.addTintTarget(button, icon);
    ui.addTintTarget(button, icon);   // listed once
    EXPECT_EQ(ui.getTintTargets(button), (std::vector<EntityHandle>{label[0], icon}));
    ui.removeTintTarget(button, label[0]);
    EXPECT_EQ(ui.getTintTargets(button), std::vector<EntityHandle>{icon});

    // A plain element becomes interactable.
    const EntityHandle row = ui.createElement(canvas, {100.0f, 20.0f});
    ui.addTintTarget(row, icon);
    EXPECT_TRUE(registry.all_of<UI::UIInteractable>(e(row)));
    EXPECT_TRUE(ui.getTintTargets(canvas).empty());
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

    const EntityHandle lit = ui.createWorldCanvas({256.0f, 256.0f}, {1.0f, 1.0f}, 1.0f, true);
    EXPECT_EQ(registry.get<UI::UICanvas>(e(lit)).textureSlot, "albedoMap");
}

TEST_F(UIAPIFixture, LayoutGroupsPlaceAndSizeTheirChildren) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle column = ui.createVerticalLayout(canvas, {300.0f, 0.0f}, 10.0f, {8.0f, 8.0f, 8.0f, 8.0f});
    ui.setAnchor(column, {0.0f, 0.0f});
    ui.setPosition(column, {20.0f, 30.0f});
    ui.setSizeToContent(column, SizeFit::Unconstrained, SizeFit::Preferred);
    std::vector<EntityHandle> buttons;
    for (int i = 0; i < 3; ++i) buttons.push_back(ui.createButton(canvas, "B", {160.0f, 40.0f}));
    for (EntityHandle b : buttons) ui.setParent(b, column);
    ui.setLayoutElement(buttons[1], {-1.0f, -1.0f}, {-1.0f, 60.0f}, {-1.0f, -1.0f});
    frame();

    EXPECT_EQ(ui.getRect(column), glm::vec4(20.0f, 30.0f, 300.0f, 8 + 40 + 10 + 60 + 10 + 40 + 8));
    EXPECT_EQ(ui.getRect(buttons[0]), glm::vec4(28.0f, 38.0f, 160.0f, 40.0f));
    EXPECT_EQ(ui.getRect(buttons[1]), glm::vec4(28.0f, 88.0f, 160.0f, 60.0f));
    EXPECT_EQ(ui.getPreferredSize(column), glm::vec2(8 + 160 + 8, 176.0f));

    ui.setLayoutChildSizing(column, true, true, true, false);   // children fill the width
    ui.setLayoutAlignment(column, {0.0f, 0.0f});
    frame();
    EXPECT_EQ(ui.getRect(buttons[2]).z, 300.0f - 16.0f);

    const EntityHandle grid = ui.createGridLayout(canvas, {300.0f, 300.0f}, {100.0f, 50.0f}, 2, {10.0f, 10.0f});
    ui.setAnchor(grid, {1.0f, 1.0f});
    const EntityHandle cell = ui.createImage(grid, {1.0f, 1.0f});
    ui.createImage(grid, {1.0f, 1.0f});
    const EntityHandle third = ui.createImage(grid, {1.0f, 1.0f});
    frame();
    EXPECT_EQ(ui.getRect(cell), glm::vec4(980.0f, 420.0f, 100.0f, 50.0f));
    EXPECT_EQ(ui.getRect(third), glm::vec4(980.0f, 480.0f, 100.0f, 50.0f));

    ui.setSizeToContent(column, SizeFit::Unconstrained, SizeFit::Unconstrained);
    EXPECT_FALSE(registry.all_of<UI::UIContentSizeFitter>(e(column)));
}

TEST_F(UIAPIFixture, ScrollViewsScrollTheirContent) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle view = ui.createScrollView(canvas, {300.0f, 200.0f});
    ui.setAnchor(view, {0.0f, 0.0f});
    const EntityHandle content = ui.getScrollContent(view);
    ASSERT_NE(content, NullEntity);
    std::vector<EntityHandle> rows;
    for (int i = 0; i < 10; ++i) rows.push_back(ui.createPanel(content, {0.0f, 50.0f}));
    frame();
    frame();
    EXPECT_EQ(ui.getRect(rows[0]).z, 300.0f - 12.0f);   // the view's width less its scrollbar

    // Rows 0..9 span 0..500; the view shows 200, so the range is 300.
    ui.scrollTo(view, rows[6]);                          // 300..350: scroll down by 150
    frame();
    EXPECT_FLOAT_EQ(ui.getScrollPosition(view).y, 0.5f);
    EXPECT_FLOAT_EQ(ui.getRect(rows[6]).y, 150.0f);
    ui.scrollTo(view, rows[1]);                          // 50..100: back up to 50
    EXPECT_NEAR(ui.getScrollPosition(view).y, 50.0f / 300.0f, 1e-5f);

    ui.setScrollPosition(view, {0.0f, 2.0f});
    EXPECT_FLOAT_EQ(ui.getScrollPosition(view).y, 1.0f);
    ui.setScrollSettings(view, 100.0f, false);
    EXPECT_EQ(registry.get<UI::UIScrollView>(e(view)).wheelSpeed, 100.0f);
    EXPECT_EQ(registry.get<UI::UIScrollView>(e(view)).movement, UI::UIScrollView::Movement::Clamped);
    ui.setScrollElastic(view, true, 0.2f);
    EXPECT_EQ(registry.get<UI::UIScrollView>(e(view)).movement, UI::UIScrollView::Movement::Elastic);
    EXPECT_FLOAT_EQ(registry.get<UI::UIScrollView>(e(view)).elasticity, 0.2f);
    ui.setScrollElastic(view, false);
    EXPECT_EQ(registry.get<UI::UIScrollView>(e(view)).movement, UI::UIScrollView::Movement::Clamped);

    // The wheel over it scrolls by its speed.
    ui.setScrollPosition(view, {0.0f, 0.0f});
    frame();
    registry.get<ECS::InputStateComponent>(input).scrollDelta = {0.0f, -1.0f};
    frame({100.0f, 100.0f});
    EXPECT_NEAR(ui.getScrollPosition(view).y, 100.0f / 300.0f, 1e-5f);
    EXPECT_EQ(ui.getScrollContent(rows[0]), NullEntity);
}

TEST_F(UIAPIFixture, FocusMovesByCodeAndByTab) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle a = ui.createButton(canvas, "A");
    const EntityHandle b = ui.createButton(canvas, "B");
    const EntityHandle c = ui.createButton(canvas, "C");
    ui.setAnchor(b, {0.0f, 0.0f});
    ui.setAnchor(c, {1.0f, 1.0f});
    ui.setNavigable(b, false);
    frame();

    ui.focus(a);
    EXPECT_EQ(ui.getFocusedElement(), a);
    registry.get<ECS::InputStateComponent>(input).keyPresses = {{GLFW_KEY_TAB, 0, false}};
    frame();
    EXPECT_EQ(ui.getFocusedElement(), c);   // b is not navigable
    EXPECT_FALSE(ui.isKeyboardCaptured());

    ui.focus(NullEntity);
    EXPECT_EQ(ui.getFocusedElement(), NullEntity);
    ui.focus(a);
    ui.setVisible(a, false);
    frame();
    frame();
    EXPECT_EQ(ui.getFocusedElement(), NullEntity);   // hidden: focus let go
}

TEST_F(UIAPIFixture, TextFieldsTakeTypingAndReportItForOneFrame) {
    const EntityHandle canvas = ui.createCanvas({1280.0f, 720.0f});
    const EntityHandle field = ui.createTextField(canvas, "Search");
    auto& state = registry.get<ECS::InputStateComponent>(input);
    frame();
    EXPECT_EQ(ui.getText(field), "");

    frame({640.0f, 360.0f}, true);
    frame({640.0f, 360.0f}, false);
    EXPECT_EQ(ui.getFocusedElement(), field);
    EXPECT_TRUE(ui.isKeyboardCaptured());

    state.textInput = U"abc";
    frame();
    EXPECT_EQ(ui.getText(field), "abc");
    EXPECT_TRUE(ui.textChanged(field));
    state.keyPresses = {{GLFW_KEY_ENTER, 0, false}};
    frame();
    EXPECT_TRUE(ui.textSubmitted(field));
    EXPECT_FALSE(ui.textChanged(field));

    ui.setText(field, "hello");
    ui.selectAll(field);
    state.keyPresses = {{GLFW_KEY_C, GLFW_MOD_CONTROL, false}};
    frame();
    EXPECT_FALSE(ui.textChanged(field));        // set by code, not the user
    EXPECT_EQ(context.clipboard().text(), "hello");

    ui.setTextFieldOptions(field, 2);
    state.textInput = U"xyz";
    frame();
    EXPECT_EQ(ui.getText(field), "xy");          // replaced the selection, two at most

    ui.setFontSize(field, 30.0f);
    EXPECT_EQ(registry.get<UI::UIText>(registry.get<UI::UITextField>(e(field)).textElement).fontSize, 30.0f);
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
