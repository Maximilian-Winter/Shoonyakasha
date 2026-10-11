//
// AccessibilityTest.cpp - What screen readers see, and what they do
//
// Tier 2: EnTT registry and fonts, no GPU. On Windows the UI Automation
// provider is also driven in-process through IUIAutomation, on a plain
// Win32 window.
//

#include <gtest/gtest.h>

#include "ECS/Core.h"
#include "Platform/AccessibilityProvider.h"
#include "UI/Accessibility.h"
#include "UI/FontLibrary.h"
#include "UI/UISystems.h"
#include "UI/UIWidgets.h"
#include "UI/Utf8.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <UIAutomation.h>
#endif

#include <string>
#include <vector>

using namespace Shoonyakasha;
using namespace Shoonyakasha::UI;

namespace {

const std::string kFonts = std::string(SHOONYAKASHA_SOURCE_DIR) + "/assets/fonts/";

/// A screen canvas drawn at twice its units, in a window of half the
/// framebuffer's pixels, with one of each widget.
struct Fixture {
    entt::registry registry;
    FontLibrary fonts;
    glm::vec2 screen{1600.0f, 1200.0f};
    glm::vec2 window{800.0f, 600.0f};
    UIFocusState focus;
    AccessibilityState state;
    AccessibilitySystem system{&state, &screen, &window, &focus};
    entt::entity canvas = registry.create();
    entt::entity button, toggle, slider, field, secret, view, row, title, logo, decoration, hiddenButton, clipped;

    Fixture() {
        const uint32_t font = fonts.load(kFonts + "Roboto-Regular.ttf");
        auto& c = registry.emplace<UICanvas>(canvas);
        c.scaleFactor = 2.0f;
        state.enabled = true;

        title = place(createLabel(registry, canvas, "<b>Settings</b>", font), {10, 10}, {200, 30});
        registry.get<UIText>(title).richText = true;
        button = place(createButton(registry, canvas, "Play", font), {10, 50});
        toggle = place(createToggle(registry, canvas, "Sound", font, true), {10, 100});
        slider = place(createSlider(registry, canvas, 0.0f, 10.0f, 5.0f), {10, 140});
        registry.get<UISlider>(slider).wholeNumbers = true;
        field = place(createTextField(registry, canvas, "Your name", font), {10, 180});
        auto& f = registry.get<UITextField>(field);
        f.text = "Dakini";
        f.selectionAnchor = 1;
        f.caret = 4;
        secret = place(createTextField(registry, canvas, "Password", font), {300, 180});
        registry.get<UITextField>(secret).password = true;
        registry.get<UITextField>(secret).text = "hidden";

        view = place(createScrollView(registry, canvas, {200, 100}), {10, 230});
        const entt::entity content = registry.get<UIScrollView>(view).content;
        for (int i = 0; i < 6; ++i) {
            const entt::entity r = createElement(registry, content, {0, 40});
            const entt::entity label = createLabel(registry, r, "Row " + std::to_string(i), font);
            if (i == 0) row = label;
        }

        logo = place(createElement(registry, canvas, {64, 64}), {300, 10});
        registry.emplace<UIImage>(logo);
        registry.emplace<UIAccessible>(logo).name = "Logo";
        registry.get<UIAccessible>(logo).description = "The game's emblem";

        decoration = place(createElement(registry, canvas, {100, 40}), {300, 100});
        registry.emplace<UIAccessible>(decoration).hidden = true;
        hiddenButton = createButton(registry, decoration, "Never", font);

        // Scrolled out of its view: the last row.
        clipped = registry.get<ECS::HierarchyComponent>(content).children.back();
        step();
    }

    entt::entity place(entt::entity e, glm::vec2 at, glm::vec2 size = glm::vec2(-1.0f)) {
        auto& rect = registry.get<UIRect>(e);
        rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
        rect.anchoredPosition = at;
        if (size.x >= 0.0f) rect.sizeDelta = size;
        return e;
    }

    void step() {
        CanvasLayoutSystem(&screen, &fonts).update(registry, 0.0f);
        CanvasTextSystem(&fonts).update(registry, 0.0f);
        system.update(registry, 0.0f);
    }

    AccessibilitySnapshot snapshot() {
        return state.read([](const AccessibilitySnapshot& s) { return s; });
    }

    static const AccessibleNode* node(const AccessibilitySnapshot& s, entt::entity e) {
        const int i = s.find(static_cast<uint32_t>(e));
        return i >= 0 ? &s.nodes[static_cast<size_t>(i)] : nullptr;
    }
};

} // namespace

TEST(Accessibility, TheSnapshotDescribesEachWidgetOnce) {
    Fixture f;
    const AccessibilitySnapshot s = f.snapshot();
    ASSERT_FALSE(s.nodes.empty());
    EXPECT_EQ(s.nodes[0].id, static_cast<uint32_t>(f.canvas));
    EXPECT_EQ(s.nodes[0].parent, -1);
    EXPECT_EQ(s.nodes[0].role, AccessibleRole::Group);

    const AccessibleNode* title = Fixture::node(s, f.title);
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->role, AccessibleRole::Text);
    EXPECT_EQ(title->name, "Settings");                 // without its markup

    const AccessibleNode* button = Fixture::node(s, f.button);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->role, AccessibleRole::Button);
    EXPECT_EQ(button->name, "Play");                    // from its label
    EXPECT_TRUE(button->children.empty());              // the label is part of it
    EXPECT_TRUE(button->focusable);
    EXPECT_EQ(button->parent, 0);

    const AccessibleNode* toggle = Fixture::node(s, f.toggle);
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(toggle->role, AccessibleRole::CheckBox);
    EXPECT_EQ(toggle->name, "Sound");
    EXPECT_TRUE(toggle->toggled);

    const AccessibleNode* slider = Fixture::node(s, f.slider);
    ASSERT_NE(slider, nullptr);
    EXPECT_EQ(slider->role, AccessibleRole::Slider);
    EXPECT_FLOAT_EQ(slider->minimum, 0.0f);
    EXPECT_FLOAT_EQ(slider->maximum, 10.0f);
    EXPECT_FLOAT_EQ(slider->value, 5.0f);
    EXPECT_FLOAT_EQ(slider->smallChange, 1.0f);         // whole numbers

    const AccessibleNode* field = Fixture::node(s, f.field);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->role, AccessibleRole::Edit);
    EXPECT_EQ(field->name, "Your name");
    EXPECT_EQ(field->text, "Dakini");
    EXPECT_EQ(field->selectionStart, 1u);
    EXPECT_EQ(field->selectionEnd, 4u);
    const AccessibleNode* secret = Fixture::node(s, f.secret);
    ASSERT_NE(secret, nullptr);
    EXPECT_TRUE(secret->password);
    EXPECT_EQ(secret->text, "");                        // never read out

    const AccessibleNode* view = Fixture::node(s, f.view);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->role, AccessibleRole::Pane);
    EXPECT_TRUE(view->scrollsVertically);
    EXPECT_FALSE(view->scrollsHorizontally);
    EXPECT_LT(view->viewPercent.y, 100.0f);
    const AccessibleNode* row = Fixture::node(s, f.row);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->role, AccessibleRole::Text);
    EXPECT_EQ(s.nodes[static_cast<size_t>(row->parent)].id, static_cast<uint32_t>(f.view));   // in the view
    const AccessibleNode* clipped = Fixture::node(s, f.registry.get<ECS::HierarchyComponent>(f.clipped).children[0]);
    ASSERT_NE(clipped, nullptr);
    EXPECT_TRUE(clipped->offscreen);

    const AccessibleNode* logo = Fixture::node(s, f.logo);
    ASSERT_NE(logo, nullptr);
    EXPECT_EQ(logo->role, AccessibleRole::Group);
    EXPECT_EQ(logo->name, "Logo");
    EXPECT_EQ(logo->description, "The game's emblem");

    // Decoration and what is in it are left out.
    EXPECT_EQ(Fixture::node(s, f.decoration), nullptr);
    EXPECT_EQ(Fixture::node(s, f.hiddenButton), nullptr);

    // Bounds in window coordinates: canvas units times the canvas scale,
    // times window over framebuffer.
    const Rect& rect = f.registry.get<UIRect>(f.button).rect;
    EXPECT_NEAR(button->bounds.min.x, rect.min.x, 1e-3f);
    EXPECT_NEAR(button->bounds.max.y, rect.max.y, 1e-3f);
}

TEST(Accessibility, FocusAndAnOverriddenRoleAreDescribed) {
    Fixture f;
    f.focus.focused = f.button;
    f.registry.get<UIAccessible>(f.logo).role = AccessibleRole::Button;
    f.step();
    const AccessibilitySnapshot s = f.snapshot();
    ASSERT_GE(s.focused, 0);
    EXPECT_EQ(s.nodes[static_cast<size_t>(s.focused)].id, static_cast<uint32_t>(f.button));
    EXPECT_TRUE(Fixture::node(s, f.button)->focused);
    EXPECT_EQ(Fixture::node(s, f.logo)->role, AccessibleRole::Button);
}

TEST(Accessibility, ACanvasWithNothingToDescribeIsLeftOut) {
    Fixture f;
    const entt::entity dialog = f.registry.create();
    f.registry.emplace<UICanvas>(dialog).sortOrder = 10;
    const entt::entity backdrop = createElement(f.registry, dialog, {100, 100});
    f.registry.get<UIRect>(backdrop).visible = false;   // closed
    f.step();
    const AccessibilitySnapshot s = f.snapshot();
    EXPECT_EQ(Fixture::node(s, dialog), nullptr);
    EXPECT_NE(Fixture::node(s, f.canvas), nullptr);
    for (const AccessibleNode& n : s.nodes) {
        for (const int child : n.children) EXPECT_LT(static_cast<size_t>(child), s.nodes.size());
    }
}

TEST(Accessibility, NothingIsDescribedWhileDisabled) {
    Fixture f;
    f.state.enabled = false;
    f.state.publish({});
    f.step();
    EXPECT_TRUE(f.snapshot().nodes.empty());
}

TEST(Accessibility, QueuedActionsApplyInTheNextUpdate) {
    Fixture f;
    auto queue = [&](AccessibleAction::Kind kind, entt::entity e) {
        AccessibleAction a;
        a.kind = kind;
        a.node = static_cast<uint32_t>(e);
        return a;
    };
    f.state.queue(queue(AccessibleAction::Kind::Invoke, f.button));
    f.state.queue(queue(AccessibleAction::Kind::Toggle, f.toggle));
    AccessibleAction value = queue(AccessibleAction::Kind::SetValue, f.slider);
    value.value = 7.4f;
    f.state.queue(value);
    AccessibleAction text = queue(AccessibleAction::Kind::SetText, f.field);
    text.text = "Vajra\nyogini";
    f.state.queue(text);
    AccessibleAction scroll = queue(AccessibleAction::Kind::Scroll, f.view);
    scroll.scrollPercent = {-1.0f, 50.0f};
    f.state.queue(scroll);
    f.state.queue(queue(AccessibleAction::Kind::Focus, f.slider));
    // Not before the next update.
    EXPECT_FALSE(f.registry.get<UIInteractable>(f.button).clicked);

    f.step();
    EXPECT_TRUE(f.registry.get<UIInteractable>(f.button).clicked);
    EXPECT_FALSE(f.registry.get<UIToggle>(f.toggle).isOn);
    EXPECT_TRUE(f.registry.get<UIInteractable>(f.toggle).valueChanged);
    EXPECT_FLOAT_EQ(f.registry.get<UISlider>(f.slider).value, 7.0f);   // whole numbers
    EXPECT_TRUE(f.registry.get<UIInteractable>(f.slider).valueChanged);
    const auto& field = f.registry.get<UITextField>(f.field);
    EXPECT_EQ(field.text, "Vajra yogini");                            // one line
    EXPECT_TRUE(field.changed);
    EXPECT_EQ(field.caret, 12u);
    EXPECT_FLOAT_EQ(f.registry.get<UIScrollView>(f.view).scrollPosition.y, 0.5f);
    EXPECT_EQ(f.focus.focused, f.slider);
    // The snapshot of the same update shows the result.
    EXPECT_FALSE(Fixture::node(f.snapshot(), f.toggle)->toggled);

    AccessibleAction select = queue(AccessibleAction::Kind::Select, f.field);
    select.selectionStart = 0;
    select.selectionEnd = 5;
    f.state.queue(select);
    // A disabled button is not invoked.
    f.registry.get<UIInteractable>(f.button).clicked = false;
    f.registry.get<UIInteractable>(f.button).enabled = false;
    f.state.queue(queue(AccessibleAction::Kind::Invoke, f.button));
    f.step();
    EXPECT_EQ(field.selectionAnchor, 0u);
    EXPECT_EQ(field.caret, 5u);
    EXPECT_FALSE(f.registry.get<UIInteractable>(f.button).clicked);
    EXPECT_TRUE(f.state.takeActions().empty());
}

#ifdef _WIN32

namespace {

Platform::AccessibilityProvider* g_provider = nullptr;

LRESULT CALLBACK testWindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_provider) {
        if (const auto result = g_provider->handleMessage(message, wParam, lParam)) return static_cast<LRESULT>(*result);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

/// A Win32 window answering UI Automation from the fixture's snapshot, and
/// an in-process UI Automation client.
struct Automation {
    Fixture f;
    HWND hwnd = nullptr;
    std::unique_ptr<Platform::AccessibilityProvider> provider;
    IUIAutomation* automation = nullptr;
    bool comStarted = false;

    Automation() {
        comStarted = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = testWindowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"ShoonyakashaAccessibilityTest";
        RegisterClassW(&windowClass);
        hwnd = CreateWindowExW(0, windowClass.lpszClassName, L"Accessibility test", WS_OVERLAPPEDWINDOW, 100, 100,
                               800, 600, nullptr, nullptr, windowClass.hInstance, nullptr);
        f.state.enabled = false;
        provider = Platform::AccessibilityProvider::create(hwnd, f.state);
        g_provider = provider.get();
        CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                         reinterpret_cast<void**>(&automation));
    }

    ~Automation() {
        if (automation) automation->Release();
        g_provider = nullptr;
        provider.reset();
        if (hwnd) DestroyWindow(hwnd);
        if (comStarted) CoUninitialize();
    }

    /// The element named `name` among the window's descendants.
    IUIAutomationElement* find(IUIAutomationElement* root, const wchar_t* name) {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(name);
        IUIAutomationCondition* condition = nullptr;
        automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
        VariantClear(&value);
        IUIAutomationElement* found = nullptr;
        if (condition) {
            root->FindFirst(TreeScope_Descendants, condition, &found);
            condition->Release();
        }
        return found;
    }
};

std::wstring nameOf(IUIAutomationElement* element) {
    BSTR name = nullptr;
    element->get_CurrentName(&name);
    std::wstring out = name ? name : L"";
    SysFreeString(name);
    return out;
}

} // namespace

TEST(UIAutomation, TheProviderAnswersAndOperatesThroughUIAutomation) {
    Automation a;
    ASSERT_NE(a.hwnd, nullptr);
    ASSERT_NE(a.provider, nullptr);
    ASSERT_NE(a.automation, nullptr);

    // Asking for the window's element enables the snapshot.
    IUIAutomationElement* root = nullptr;
    ASSERT_EQ(a.automation->ElementFromHandle(a.hwnd, &root), S_OK);
    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(a.f.state.enabled);
    a.f.step();
    a.provider->snapshotPublished();

    // The button: its name, role, bounds on the screen, and Invoke.
    IUIAutomationElement* button = a.find(root, L"Play");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(nameOf(button), L"Play");
    CONTROLTYPEID type = 0;
    button->get_CurrentControlType(&type);
    EXPECT_EQ(type, UIA_ButtonControlTypeId);
    RECT bounds{};
    button->get_CurrentBoundingRectangle(&bounds);
    POINT origin{0, 0};
    ClientToScreen(a.hwnd, &origin);
    const Rect& rect = a.f.registry.get<UIRect>(a.f.button).rect;
    EXPECT_NEAR(bounds.left - origin.x, rect.min.x, 1.0f);
    EXPECT_NEAR(bounds.bottom - origin.y, rect.max.y, 1.0f);
    IUIAutomationInvokePattern* invoke = nullptr;
    button->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern),
                                reinterpret_cast<void**>(&invoke));
    ASSERT_NE(invoke, nullptr);
    EXPECT_EQ(invoke->Invoke(), S_OK);
    invoke->Release();

    // Its parent is the canvas, whose parent is the window.
    IUIAutomationTreeWalker* walker = nullptr;
    a.automation->get_RawViewWalker(&walker);
    IUIAutomationElement* canvas = nullptr;
    walker->GetParentElement(button, &canvas);
    ASSERT_NE(canvas, nullptr);
    canvas->get_CurrentControlType(&type);
    EXPECT_EQ(type, UIA_GroupControlTypeId);
    canvas->Release();
    walker->Release();

    // The toggle's state, and the slider's value set.
    IUIAutomationElement* toggle = a.find(root, L"Sound");
    ASSERT_NE(toggle, nullptr);
    IUIAutomationTogglePattern* togglePattern = nullptr;
    toggle->GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern),
                                reinterpret_cast<void**>(&togglePattern));
    ASSERT_NE(togglePattern, nullptr);
    ToggleState toggleState = ToggleState_Off;
    togglePattern->get_CurrentToggleState(&toggleState);
    EXPECT_EQ(toggleState, ToggleState_On);
    togglePattern->Release();

    IUIAutomationCondition* sliders = nullptr;
    VARIANT sliderType;
    VariantInit(&sliderType);
    sliderType.vt = VT_I4;
    sliderType.lVal = UIA_SliderControlTypeId;
    a.automation->CreatePropertyCondition(UIA_ControlTypePropertyId, sliderType, &sliders);
    IUIAutomationElement* slider = nullptr;
    root->FindFirst(TreeScope_Descendants, sliders, &slider);
    sliders->Release();
    ASSERT_NE(slider, nullptr);
    IUIAutomationRangeValuePattern* range = nullptr;
    slider->GetCurrentPatternAs(UIA_RangeValuePatternId, __uuidof(IUIAutomationRangeValuePattern),
                                reinterpret_cast<void**>(&range));
    ASSERT_NE(range, nullptr);
    double maximum = 0.0;
    range->get_CurrentMaximum(&maximum);
    EXPECT_DOUBLE_EQ(maximum, 10.0);
    EXPECT_EQ(range->SetValue(3.0), S_OK);
    EXPECT_NE(range->SetValue(30.0), S_OK);        // out of range
    range->Release();

    // The text field's value and selection.
    IUIAutomationElement* field = a.find(root, L"Your name");
    ASSERT_NE(field, nullptr);
    IUIAutomationValuePattern* value = nullptr;
    field->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern),
                               reinterpret_cast<void**>(&value));
    ASSERT_NE(value, nullptr);
    BSTR text = nullptr;
    value->get_CurrentValue(&text);
    EXPECT_STREQ(text, L"Dakini");
    SysFreeString(text);
    value->Release();
    IUIAutomationTextPattern* textPattern = nullptr;
    field->GetCurrentPatternAs(UIA_TextPatternId, __uuidof(IUIAutomationTextPattern),
                               reinterpret_cast<void**>(&textPattern));
    ASSERT_NE(textPattern, nullptr);
    IUIAutomationTextRangeArray* selection = nullptr;
    textPattern->GetSelection(&selection);
    ASSERT_NE(selection, nullptr);
    IUIAutomationTextRange* selected = nullptr;
    selection->GetElement(0, &selected);
    ASSERT_NE(selected, nullptr);
    BSTR selectedText = nullptr;
    selected->GetText(-1, &selectedText);
    EXPECT_STREQ(selectedText, L"aki");
    SysFreeString(selectedText);
    // A word of the document, moved to and selected.
    IUIAutomationTextRange* document = nullptr;
    textPattern->get_DocumentRange(&document);
    ASSERT_NE(document, nullptr);
    document->ExpandToEnclosingUnit(TextUnit_Character);
    int moved = 0;
    document->Move(TextUnit_Character, 2, &moved);
    EXPECT_EQ(moved, 2);
    BSTR character = nullptr;
    document->GetText(-1, &character);
    EXPECT_STREQ(character, L"k");
    SysFreeString(character);
    EXPECT_EQ(document->Select(), S_OK);
    document->Release();
    selected->Release();
    selection->Release();
    textPattern->Release();

    // What was asked happens in the next update.
    a.f.step();
    EXPECT_TRUE(a.f.registry.get<UIInteractable>(a.f.button).clicked);
    EXPECT_FLOAT_EQ(a.f.registry.get<UISlider>(a.f.slider).value, 3.0f);
    EXPECT_EQ(a.f.registry.get<UITextField>(a.f.field).selectionAnchor, 2u);
    EXPECT_EQ(a.f.registry.get<UITextField>(a.f.field).caret, 3u);

    // A widget that is gone is reported gone.
    a.f.registry.get<UIRect>(a.f.toggle).visible = false;
    a.f.step();
    BSTR gone = nullptr;
    EXPECT_EQ(toggle->get_CurrentName(&gone), UIA_E_ELEMENTNOTAVAILABLE);
    SysFreeString(gone);

    field->Release();
    slider->Release();
    toggle->Release();
    button->Release();
    root->Release();
}

#endif
