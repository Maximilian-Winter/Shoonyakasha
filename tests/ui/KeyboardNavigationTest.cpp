//
// KeyboardNavigationTest.cpp - Focus, keyboard and gamepad navigation
//
// Tier 2: EnTT registry, no GPU. Each step lays the canvases out and runs the
// input system with this frame's key presses and gamepad state.
//

#include <gtest/gtest.h>

#include "Core/EventSystem.h"
#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"

#include <GLFW/glfw3.h>

#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

struct Fixture {
    entt::registry registry;
    glm::vec2 screen{800.0f, 600.0f};
    UIPointerState pointer;
    UIFocusState focus;
    CanvasInputSystem input{&screen, nullptr, &pointer, &focus};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();

    Fixture() {
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas);
    }

    /// A 100 x 40 button with its top-left at `position`.
    entt::entity button(glm::vec2 position, entt::entity parent = entt::null) {
        const entt::entity b = createButton(registry, parent == entt::null ? canvas : parent, "B", 0, {100, 40});
        auto& rect = registry.get<UIRect>(b);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = position;
        return b;
    }

    ECS::InputStateComponent& state() { return registry.get<ECS::InputStateComponent>(inputEntity); }

    /// One frame: input against the last layout, then layout.
    void step(std::vector<ECS::InputStateComponent::KeyPress> keys = {}) {
        state().keyPresses = std::move(keys);
        input.update(registry, 0.0f);
        CanvasLayoutSystem(&screen).update(registry, 0.0f);
        state().keyPresses.clear();
    }

    void key(int key, int mods = 0, bool repeat = false) { step({{key, mods, repeat}}); }

    void click(glm::vec2 at) {
        state().mousePosition = at;
        state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = true;
        step();
        state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = false;
        step();
    }

    void pad(int button) {
        state().gamepad.connected = true;
        state().gamepad.buttons[button] = true;
        step();
        state().gamepad.buttons[button] = false;
        step();
    }
};

} // namespace

TEST(KeyboardNavigation, KeyEventsCarryTheirActionAndModifiers) {
    const KeyEvent repeat(GLFW_KEY_A, KeyEvent::Repeat, GLFW_MOD_SHIFT);
    EXPECT_TRUE(repeat.pressed);
    EXPECT_EQ(repeat.action, KeyEvent::Repeat);
    EXPECT_EQ(repeat.mods, GLFW_MOD_SHIFT);
    const KeyEvent legacy(GLFW_KEY_A, false);
    EXPECT_EQ(legacy.action, KeyEvent::Release);
    EXPECT_EQ(CharEvent(U'x').codepoint, U'x');
}

TEST(KeyboardNavigation, PressingFocusesAndPressingElsewhereClears) {
    Fixture f;
    const auto a = f.button({100, 100});
    f.step();
    f.click({150, 120});
    EXPECT_EQ(f.focus.focused, a);
    EXPECT_TRUE(f.registry.get<UIInteractable>(a).focused);
    f.click({600, 500});
    EXPECT_EQ(f.focus.focused, entt::entity{entt::null});
    EXPECT_FALSE(f.registry.get<UIInteractable>(a).focused);
}

TEST(KeyboardNavigation, TabCyclesInDrawOrderAndShiftTabGoesBack) {
    Fixture f;
    const auto a = f.button({100, 100});
    const auto b = f.button({250, 100});
    const auto c = f.button({400, 100});
    f.step();

    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, a);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, b);
    f.key(GLFW_KEY_TAB);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, a);   // wrapped
    f.key(GLFW_KEY_TAB, GLFW_MOD_SHIFT);
    EXPECT_EQ(f.focus.focused, c);
}

TEST(KeyboardNavigation, TabSkipsWhatCannotTakeFocus) {
    Fixture f;
    const auto a = f.button({100, 100});
    const auto hidden = f.button({250, 100});
    const auto disabled = f.button({400, 100});
    const auto skipped = f.button({550, 100});
    const auto d = f.button({100, 250});
    f.registry.get<UIRect>(hidden).visible = false;
    f.registry.get<UIInteractable>(disabled).enabled = false;
    f.registry.get<UIInteractable>(skipped).navigable = false;
    f.step();

    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, a);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, d);
}

TEST(KeyboardNavigation, ArrowsMoveToTheNearestElementThatWay) {
    Fixture f;
    const auto a = f.button({100, 100});
    const auto b = f.button({250, 100});
    const auto d = f.button({100, 250});
    f.step();

    f.key(GLFW_KEY_RIGHT);           // nothing focused: the first
    EXPECT_EQ(f.focus.focused, a);
    f.key(GLFW_KEY_RIGHT);
    EXPECT_EQ(f.focus.focused, b);
    f.key(GLFW_KEY_RIGHT);           // nothing further right: stays
    EXPECT_EQ(f.focus.focused, b);
    f.key(GLFW_KEY_DOWN);
    EXPECT_EQ(f.focus.focused, d);
    f.key(GLFW_KEY_UP);              // straight up beats up and to the side
    EXPECT_EQ(f.focus.focused, a);
}

TEST(KeyboardNavigation, EnterClicksAButtonAndSpaceFlipsAToggle) {
    Fixture f;
    const auto a = f.button({100, 100});
    const auto toggle = createToggle(f.registry, f.canvas, "T", 0);
    f.step();

    f.focus.focused = a;
    f.key(GLFW_KEY_ENTER);
    EXPECT_TRUE(f.registry.get<UIInteractable>(a).clicked);
    f.step();
    EXPECT_FALSE(f.registry.get<UIInteractable>(a).clicked);   // one frame
    f.key(GLFW_KEY_ENTER, 0, true);                             // a held key's repeats do nothing
    EXPECT_FALSE(f.registry.get<UIInteractable>(a).clicked);

    f.focus.focused = toggle;
    f.key(GLFW_KEY_SPACE);
    EXPECT_TRUE(f.registry.get<UIToggle>(toggle).isOn);
    EXPECT_TRUE(f.registry.get<UIInteractable>(toggle).valueChanged);
}

TEST(KeyboardNavigation, LeftAndRightStepAFocusedSlider) {
    Fixture f;
    const auto slider = createSlider(f.registry, f.canvas, 0.0f, 2.0f, 1.0f);
    const auto below = f.button({350, 450});
    f.step();

    f.focus.focused = slider;
    f.key(GLFW_KEY_RIGHT);
    EXPECT_FLOAT_EQ(f.registry.get<UISlider>(slider).value, 1.2f);
    EXPECT_TRUE(f.registry.get<UIInteractable>(slider).valueChanged);
    f.key(GLFW_KEY_LEFT, 0, true);   // repeats step too
    f.key(GLFW_KEY_LEFT, 0, true);
    EXPECT_FLOAT_EQ(f.registry.get<UISlider>(slider).value, 0.8f);
    f.key(GLFW_KEY_DOWN);            // up and down still move focus
    EXPECT_EQ(f.focus.focused, below);
}

TEST(KeyboardNavigation, EscapeAndGamepadBClearFocus) {
    Fixture f;
    const auto a = f.button({100, 100});
    f.step();
    f.focus.focused = a;
    f.key(GLFW_KEY_ESCAPE);
    EXPECT_EQ(f.focus.focused, entt::entity{entt::null});
    f.focus.focused = a;
    f.pad(GLFW_GAMEPAD_BUTTON_B);
    EXPECT_EQ(f.focus.focused, entt::entity{entt::null});
}

TEST(KeyboardNavigation, GamepadMovesOnEachPressAndActivates) {
    Fixture f;
    const auto a = f.button({100, 100});
    const auto b = f.button({250, 100});
    f.step();

    f.pad(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
    EXPECT_EQ(f.focus.focused, a);
    f.state().gamepad.connected = true;
    f.state().gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = true;
    f.step();
    f.step();                        // held: one move only
    EXPECT_EQ(f.focus.focused, b);
    f.state().gamepad.buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = false;
    f.step();

    f.state().gamepad.buttons[GLFW_GAMEPAD_BUTTON_A] = true;
    f.step();
    EXPECT_TRUE(f.registry.get<UIInteractable>(b).clicked);
}

TEST(KeyboardNavigation, NavigationStaysOnTheTopmostCanvas) {
    Fixture f;
    f.button({100, 100});
    const entt::entity dialog = f.registry.create();
    f.registry.emplace<UICanvas>(dialog).sortOrder = 5;
    const auto ok = f.button({300, 300}, dialog);
    const auto cancel = f.button({450, 300}, dialog);
    f.step();

    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, ok);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, cancel);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, ok);   // never out of the dialog
}

TEST(KeyboardNavigation, AnElementThatCapturesTheKeyboardKeepsAllButTabAndEscape) {
    Fixture f;
    const auto field = f.button({100, 100});
    const auto other = f.button({250, 100});
    f.registry.get<UIInteractable>(field).capturesKeyboard = true;
    f.step();

    f.focus.focused = field;
    f.key(GLFW_KEY_RIGHT);
    EXPECT_EQ(f.focus.focused, field);
    EXPECT_TRUE(f.focus.keyboardCaptured);
    f.key(GLFW_KEY_ENTER);
    EXPECT_FALSE(f.registry.get<UIInteractable>(field).clicked);
    f.key(GLFW_KEY_TAB);
    EXPECT_EQ(f.focus.focused, other);
    EXPECT_FALSE(f.focus.keyboardCaptured);
}

TEST(KeyboardNavigation, FocusedElementsTakeTheirFocusColour) {
    UIInteractable interactable;
    interactable.focused = true;
    EXPECT_EQ(interactable.stateColor(), interactable.focusedColor);
    interactable.hovered = true;
    EXPECT_EQ(interactable.stateColor(), interactable.hoverColor);
}
