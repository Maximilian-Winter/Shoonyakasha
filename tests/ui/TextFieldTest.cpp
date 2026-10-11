//
// TextFieldTest.cpp - Editing text by cluster, and text fields through the systems
//
// Tier 2: EnTT registry and fonts, no GPU. Each step runs the input system
// with this frame's typed text, key presses and pointer, then the layout and
// text systems.
//

#include <gtest/gtest.h>

#include "ECS/CameraController.h"
#include "ECS/Core.h"
#include "UI/FontLibrary.h"
#include "UI/TextEditing.h"
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

constexpr char32_t KA = 0x0915;
constexpr char32_t SSA = 0x0937;
constexpr char32_t VIRAMA = 0x094D;
constexpr char32_t SIGN_I = 0x093F;

using KeyPress = ECS::InputStateComponent::KeyPress;

TextEditBuffer buffer(std::u32string text, size_t caret) {
    TextEditBuffer b;
    b.text = std::move(text);
    b.caret = b.anchor = caret;
    return b;
}

} // namespace

// ─── TextEditBuffer ─────────────────────────────────────────────

TEST(TextEditing, CaretStopsComeFromClusters) {
    std::vector<TextCluster> clusters{{0, 3, 0, 0.0f, 10.0f}, {3, 2, 0, 10.0f, 8.0f}};
    EXPECT_EQ(caretStops(clusters, 5), (std::vector<size_t>{0, 3, 5}));
    EXPECT_EQ(caretStops({}, 3), (std::vector<size_t>{0, 1, 2, 3}));
}

TEST(TextEditing, BackspaceAndDeleteRemoveAWholeCluster) {
    // A conjunct (KA VIRAMA SSA) and KA with a vowel sign: two clusters.
    TextEditBuffer b = buffer({KA, VIRAMA, SSA, KA, SIGN_I}, 5);
    b.stops = {0, 3, 5};
    EXPECT_EQ(b.previousStop(5), 3u);
    EXPECT_TRUE(b.backspace());
    EXPECT_EQ(b.text, (std::u32string{KA, VIRAMA, SSA}));
    EXPECT_EQ(b.caret, 3u);

    b.moveTo(0, false);
    EXPECT_TRUE(b.deleteForward());
    EXPECT_TRUE(b.text.empty());
    EXPECT_FALSE(b.backspace());
}

TEST(TextEditing, InsertedTextIsAStopPerCodePointUntilLaidOut) {
    TextEditBuffer b = buffer({KA, VIRAMA, SSA}, 3);
    b.stops = {0, 3};
    EXPECT_TRUE(b.insert(U"ab"));
    EXPECT_EQ(b.caret, 5u);
    EXPECT_EQ(b.stops, (std::vector<size_t>{0, 3, 4, 5}));
    b.moveTo(0, false);
    EXPECT_TRUE(b.insert(U"x"));
    EXPECT_EQ(b.stops, (std::vector<size_t>{0, 1, 4, 5, 6}));
}

TEST(TextEditing, WordsAreRunsOfLettersOrPunctuation) {
    TextEditBuffer b = buffer(U"hello, world  foo", 0);
    EXPECT_EQ(b.nextWord(0), 5u);    // to the comma
    EXPECT_EQ(b.nextWord(5), 7u);    // over it and the space
    EXPECT_EQ(b.nextWord(7), 14u);
    EXPECT_EQ(b.nextWord(14), 17u);
    EXPECT_EQ(b.previousWord(17), 14u);
    EXPECT_EQ(b.previousWord(14), 7u);
    EXPECT_EQ(b.previousWord(7), 5u);
    EXPECT_EQ(b.wordAt(9), (std::pair<size_t, size_t>{7, 12}));
    EXPECT_EQ(b.wordAt(12), (std::pair<size_t, size_t>{12, 14}));
    EXPECT_EQ(b.wordAt(17), (std::pair<size_t, size_t>{14, 17}));

    b.moveTo(12, false);
    EXPECT_TRUE(b.backspace(true));
    EXPECT_EQ(b.text, U"hello,   foo");
}

TEST(TextEditing, InsertReplacesTheSelectionWithinMaxLength) {
    TextEditBuffer b = buffer(U"abcdef", 1);
    b.moveTo(4, true);
    EXPECT_EQ(b.selectedText(), U"bcd");
    EXPECT_TRUE(b.insert(U"XYZW", 6));
    EXPECT_EQ(b.text, U"aXYZef");          // three of the four fit
    EXPECT_EQ(b.caret, 4u);
    EXPECT_FALSE(b.insert(U"Q", 6));        // full
    EXPECT_EQ(b.text, U"aXYZef");
}

TEST(TextEditing, Utf8RoundTrips) {
    const std::u32string text{U'a', 0x00E9, KA, 0x1F600};
    EXPECT_EQ(decodeUtf8(encodeUtf8(text)), text);
    EXPECT_EQ(encodeUtf8(std::u32string{0xD800}), "\xEF\xBF\xBD");
}

// ─── Through the systems ────────────────────────────────────────

namespace {

struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    glm::vec2 screen{800.0f, 600.0f};
    UIPointerState pointer;
    UIFocusState focus;
    UIClipboard clipboard;
    CanvasInputSystem input{&screen, nullptr, &pointer, &focus, &clipboard};
    entt::entity inputEntity = registry.create();
    entt::entity canvas = registry.create();
    entt::entity field = entt::null;

    Fixture() {
        const uint32_t latin = fonts.load(kFonts + "Roboto-Regular.ttf");
        const uint32_t deva = fonts.load(kFonts + "NotoSansDevanagari-Regular.ttf");
        fonts.setFallbacks(latin, {deva});
        registry.emplace<ECS::InputStateComponent>(inputEntity);
        registry.emplace<UICanvas>(canvas);

        // 240 x 36 at (100, 100); its text runs from x 108 to 332.
        field = createTextField(registry, canvas, "Name", latin);
        auto& rect = registry.get<UIRect>(field);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = {100.0f, 100.0f};
        step();
    }

    ECS::InputStateComponent& state() { return registry.get<ECS::InputStateComponent>(inputEntity); }
    UITextField& text() { return registry.get<UITextField>(field); }
    const UIRect& rectOf(entt::entity e) { return registry.get<UIRect>(e); }

    /// One frame of `dt` seconds.
    void step(std::vector<KeyPress> keys = {}, std::u32string typed = {}, float dt = 0.25f) {
        state().keyPresses = std::move(keys);
        state().textInput = std::move(typed);
        input.update(registry, dt);
        CanvasLayoutSystem(&screen, &fonts).update(registry, dt);
        CanvasTextSystem(&fonts).update(registry, dt);
        state().keyPresses.clear();
        state().textInput.clear();
    }

    void type(const std::u32string& typed) { step({}, typed); }
    void key(int key, int mods = 0) { step({{key, mods, false, 0}}); }

    void press(glm::vec2 at, float dt = 0.25f) {
        state().mousePosition = at;
        state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = true;
        step({}, {}, dt);
    }
    void release(float dt = 0.25f) {
        state().mouseButtons[GLFW_MOUSE_BUTTON_LEFT] = false;
        step({}, {}, dt);
    }
    void click(glm::vec2 at, float dt = 0.25f) {
        press(at, dt);
        release(dt);
    }

    /// Where on screen the caret would stand before code point `index`.
    glm::vec2 pointAt(size_t index) {
        const auto& cache = registry.get<UITextCache>(text().textElement);
        return {rectOf(text().textElement).rect.min.x + caretX(cache.layout, index), 118.0f};
    }
};

} // namespace

TEST(TextField, ClickingFocusesItAndTypingInserts) {
    Fixture f;
    f.click({200.0f, 118.0f});
    EXPECT_EQ(f.focus.focused, f.field);
    EXPECT_TRUE(f.focus.keyboardCaptured);

    f.type(std::u32string{U'h', 0x00E9} + U"llo");
    EXPECT_EQ(f.text().text, "h\xC3\xA9llo");
    EXPECT_EQ(f.text().caret, 5u);
    EXPECT_TRUE(f.text().changed);
    f.step();
    EXPECT_FALSE(f.text().changed);
}

TEST(TextField, KeysMoveSelectAndEdit) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"hello world");
    f.key(GLFW_KEY_LEFT, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().caret, 6u);
    f.key(GLFW_KEY_END, GLFW_MOD_SHIFT);
    EXPECT_EQ(f.text().selectionAnchor, 6u);
    EXPECT_EQ(f.text().caret, 11u);
    f.type(U"there");
    EXPECT_EQ(f.text().text, "hello there");

    f.key(GLFW_KEY_HOME);
    f.key(GLFW_KEY_DELETE);
    EXPECT_EQ(f.text().text, "ello there");
    f.key(GLFW_KEY_RIGHT, GLFW_MOD_SHIFT);
    f.key(GLFW_KEY_RIGHT);                       // collapses to the selection's end
    EXPECT_EQ(f.text().caret, 1u);
    EXPECT_EQ(f.text().selectionAnchor, 1u);
    f.key(GLFW_KEY_BACKSPACE, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().text, "llo there");
}

TEST(TextField, CopyCutAndPasteUseTheClipboard) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"one two");
    f.key(GLFW_KEY_A, GLFW_MOD_CONTROL);
    f.key(GLFW_KEY_C, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.clipboard.text(), "one two");

    f.key(GLFW_KEY_END);
    f.key(GLFW_KEY_LEFT, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);   // select "two"
    f.key(GLFW_KEY_X, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().text, "one ");
    EXPECT_EQ(f.clipboard.text(), "two");

    f.clipboard.setText("a\r\nb\tc");
    f.key(GLFW_KEY_V, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().text, "one a b c");       // one line

    // AltGr arrives as Ctrl+Alt and is not a shortcut.
    f.key(GLFW_KEY_A, GLFW_MOD_CONTROL | GLFW_MOD_ALT);
    EXPECT_EQ(f.text().selectionAnchor, f.text().caret);
}

TEST(TextField, ADevanagariConjunctIsDeletedAsOneCluster) {
    Fixture f;
    f.focus.focused = f.field;
    f.text().text = encodeUtf8(std::u32string{U'a', KA, VIRAMA, SSA});
    f.text().caret = f.text().selectionAnchor = 4;
    f.step();                                    // lays out the new text

    f.key(GLFW_KEY_LEFT);
    EXPECT_EQ(f.text().caret, 1u);               // over the conjunct in one step
    f.key(GLFW_KEY_END);
    f.key(GLFW_KEY_BACKSPACE);
    EXPECT_EQ(f.text().text, "a");
    EXPECT_TRUE(f.text().changed);
}

TEST(TextField, MaxLengthLimitsTypingAndPasting) {
    Fixture f;
    f.focus.focused = f.field;
    f.text().maxLength = 5;
    f.type(U"abcdefg");
    EXPECT_EQ(f.text().text, "abcde");
    EXPECT_TRUE(f.text().changed);
    f.type(U"x");
    EXPECT_EQ(f.text().text, "abcde");
    EXPECT_FALSE(f.text().changed);

    f.clipboard.setText("XYZ");
    f.key(GLFW_KEY_LEFT, GLFW_MOD_SHIFT);
    f.key(GLFW_KEY_V, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.text().text, "abcdX");
}

TEST(TextField, EnterSubmitsForOneFrameAndReadOnlyKeepsTheText) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"ok");
    f.key(GLFW_KEY_ENTER);
    EXPECT_TRUE(f.text().submitted);
    EXPECT_EQ(f.focus.focused, f.field);         // Enter does not activate or leave it
    f.step();
    EXPECT_FALSE(f.text().submitted);

    f.text().readOnly = true;
    f.type(U"no");
    f.key(GLFW_KEY_BACKSPACE);
    EXPECT_EQ(f.text().text, "ok");
    f.key(GLFW_KEY_HOME);
    EXPECT_EQ(f.text().caret, 0u);               // it can still be moved through
}

TEST(TextField, TypedTextAndKeysKeepTheirOrder) {
    Fixture f;
    f.focus.focused = f.field;
    // "a", then Left, then "b", all in one frame.
    f.step({{GLFW_KEY_LEFT, 0, false, 1}}, U"ab");
    EXPECT_EQ(f.text().text, "ba");
}

TEST(TextField, ClickPlacesTheCaretAndDraggingSelects) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"hello world");

    f.key(GLFW_KEY_HOME);
    f.press(f.pointAt(6) + glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(f.text().caret, 6u);
    EXPECT_EQ(f.text().selectionAnchor, 6u);
    f.press(f.pointAt(11) - glm::vec2(1.0f, 0.0f));   // still held: a drag
    EXPECT_EQ(f.text().selectionAnchor, 6u);
    EXPECT_EQ(f.text().caret, 11u);
    f.release();

    // Pressing past the end of the text puts the caret at the end.
    f.click({320.0f, 118.0f});
    EXPECT_EQ(f.text().caret, 11u);
    EXPECT_EQ(f.text().selectionAnchor, 11u);
}

TEST(TextField, DoubleClickSelectsAWord) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"hello world");
    const glm::vec2 inWorld = f.pointAt(8);
    f.click(inWorld, 0.1f);
    f.click(inWorld, 0.1f);
    EXPECT_EQ(f.text().selectionAnchor, 6u);
    EXPECT_EQ(f.text().caret, 11u);

    // Slower presses are two single clicks.
    f.click(inWorld, 0.3f);
    f.click(inWorld, 0.3f);
    EXPECT_EQ(f.text().selectionAnchor, f.text().caret);
}

TEST(TextField, TheCaretIsKeptInView) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"the quick brown fox jumps over the lazy dog, twice over");
    const UITextField& field = f.text();
    const Rect viewport = f.rectOf(f.registry.get<ECS::HierarchyComponent>(field.textElement).parent).rect;
    EXPECT_GT(field.scroll, 0.0f);
    EXPECT_NEAR(f.rectOf(field.caretElement).rect.max.x, viewport.max.x, 1e-3f);   // just in view
    f.step();                                    // and laid out there again from its anchors
    EXPECT_NEAR(f.rectOf(field.caretElement).rect.max.x, viewport.max.x, 1e-3f);
    EXPECT_NEAR(f.rectOf(field.textElement).rect.min.x, viewport.min.x - field.scroll, 1e-3f);

    f.key(GLFW_KEY_HOME);
    EXPECT_EQ(field.scroll, 0.0f);
    EXPECT_NEAR(f.rectOf(field.caretElement).rect.min.x, viewport.min.x, 1e-3f);
    EXPECT_NEAR(f.rectOf(field.textElement).rect.min.x, viewport.min.x, 1e-3f);

    // With the text gone there is nothing to scroll.
    f.key(GLFW_KEY_END);
    f.key(GLFW_KEY_A, GLFW_MOD_CONTROL);
    f.key(GLFW_KEY_BACKSPACE);
    EXPECT_EQ(field.scroll, 0.0f);
}

TEST(TextField, SelectionSpansTheSelectedClusters) {
    Fixture f;
    f.focus.focused = f.field;
    f.type(U"hello world");
    f.key(GLFW_KEY_LEFT, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT);
    const UITextField& field = f.text();
    const UIRect& selection = f.rectOf(field.selectionElement);
    EXPECT_TRUE(selection.visibleInHierarchy);
    EXPECT_NEAR(selection.rect.min.x, f.pointAt(6).x, 1e-3f);
    EXPECT_NEAR(selection.rect.max.x, f.pointAt(11).x, 1e-3f);
    EXPECT_NEAR(selection.rect.size().y, f.registry.get<UIText>(field.textElement).fontSize, 1e-3f);
    // Drawn behind the text, the caret over it.
    EXPECT_LT(selection.drawOrder, f.rectOf(field.textElement).drawOrder);
    EXPECT_GT(f.rectOf(field.caretElement).drawOrder, f.rectOf(field.textElement).drawOrder);

    f.focus.focused = entt::null;
    f.step();
    EXPECT_FALSE(f.rectOf(field.selectionElement).visibleInHierarchy);
    EXPECT_FALSE(f.rectOf(field.caretElement).visibleInHierarchy);
}

TEST(TextField, ShowsThePlaceholderAndBlinksTheCaret) {
    Fixture f;
    const UITextField& field = f.text();
    const auto& label = f.registry.get<UIText>(field.textElement);
    EXPECT_EQ(label.text, "Name");
    EXPECT_EQ(label.color, field.placeholderColor);

    f.focus.focused = f.field;
    f.step({}, {}, 0.1f);                         // focusing restarts the blink
    EXPECT_TRUE(f.rectOf(field.caretElement).visibleInHierarchy);
    f.step({}, {}, 0.5f);
    EXPECT_FALSE(f.rectOf(field.caretElement).visibleInHierarchy);
    f.type(U"x");                                 // typing restarts it
    EXPECT_TRUE(f.rectOf(field.caretElement).visibleInHierarchy);
    EXPECT_EQ(label.text, "x");
    EXPECT_EQ(label.color, field.textColor);
}

TEST(TextField, PasswordIsMaskedAndNotCopied) {
    Fixture f;
    f.focus.focused = f.field;
    f.text().password = true;
    f.type(std::u32string{U's', 0x00E9} + U"cret");
    EXPECT_EQ(f.registry.get<UIText>(f.text().textElement).text, encodeUtf8(std::u32string(6, 0x2022)));
    f.key(GLFW_KEY_A, GLFW_MOD_CONTROL);
    f.key(GLFW_KEY_C, GLFW_MOD_CONTROL);
    f.key(GLFW_KEY_X, GLFW_MOD_CONTROL);
    EXPECT_EQ(f.clipboard.text(), "");
    EXPECT_EQ(f.text().text, "s\xC3\xA9" "cret");
}

