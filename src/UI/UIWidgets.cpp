//
// UIWidgets.cpp - Building canvas UI elements and widgets
//

#include "UI/UIWidgets.h"

#include "ECS/Core.h"

namespace Shoonyakasha {
namespace UI {

namespace {

const glm::vec4 kSurface{0.22f, 0.24f, 0.30f, 1.0f};
const glm::vec4 kTrack{0.12f, 0.13f, 0.16f, 1.0f};
const glm::vec4 kAccent{0.35f, 0.62f, 0.95f, 1.0f};
const glm::vec4 kHandle{0.92f, 0.93f, 0.95f, 1.0f};

/// An interactable whose dark surfaces lighten under the pointer: the
/// product with the state colour is clamped, so factors above 1 brighten.
UIInteractable& addInteractable(entt::registry& registry, entt::entity e) {
    auto& interactable = registry.emplace<UIInteractable>(e);
    interactable.hoverColor = {1.35f, 1.35f, 1.35f, 1.0f};
    interactable.pressedColor = {0.8f, 0.8f, 0.8f, 1.0f};
    interactable.focusedColor = {1.2f, 1.45f, 2.1f, 1.0f};   // a dark surface turns blue
    return interactable;
}

/// An element stretched over its parent, inset by `inset` on every side.
entt::entity stretched(entt::registry& registry, entt::entity parent, float inset = 0.0f) {
    const entt::entity e = createElement(registry, parent, glm::vec2(-2.0f * inset));
    auto& rect = registry.get<UIRect>(e);
    rect.anchorMin = {0.0f, 0.0f};
    rect.anchorMax = {1.0f, 1.0f};
    return e;
}

} // namespace

void setParent(entt::registry& registry, entt::entity element, entt::entity parent) {
    auto& hierarchy = registry.get_or_emplace<ECS::HierarchyComponent>(element);
    if (hierarchy.parent != entt::null) {
        if (auto* old = registry.try_get<ECS::HierarchyComponent>(hierarchy.parent)) old->removeChild(element);
    }
    hierarchy.parent = parent;
    if (parent != entt::null) registry.get_or_emplace<ECS::HierarchyComponent>(parent).addChild(element);
}

entt::entity createElement(entt::registry& registry, entt::entity parent, glm::vec2 size) {
    const entt::entity e = registry.create();
    registry.emplace<UIRect>(e).sizeDelta = size;
    setParent(registry, e, parent);
    return e;
}

entt::entity createLabel(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                         float fontSize) {
    const entt::entity e = stretched(registry, parent);
    auto& label = registry.emplace<UIText>(e);
    label.text = text;
    label.font = font;
    label.fontSize = fontSize;
    label.alignH = TextAlignH::Center;
    label.alignV = TextAlignV::Middle;
    label.wrap = false;
    return e;
}

entt::entity createButton(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                          glm::vec2 size) {
    const entt::entity button = createElement(registry, parent, size);
    registry.emplace<UIPanel>(button).color = kSurface;
    auto& interactable = addInteractable(registry, button);
    registry.emplace<UIButton>(button);
    interactable.tintTargets.push_back(createLabel(registry, button, text, font, size.y * 0.5f));
    return button;
}

entt::entity createToggle(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                          bool isOn, glm::vec2 size) {
    const entt::entity toggle = createElement(registry, parent, size);
    addInteractable(registry, toggle);

    const entt::entity box = createElement(registry, toggle, glm::vec2(size.y));
    auto& boxRect = registry.get<UIRect>(box);
    boxRect.anchorMin = boxRect.anchorMax = {0.0f, 0.5f};
    boxRect.pivot = {0.0f, 0.5f};
    registry.emplace<UIPanel>(box).color = kTrack;

    const entt::entity checkmark = stretched(registry, box, size.y * 0.2f);
    registry.emplace<UIImage>(checkmark).color = kAccent;

    const entt::entity label = createLabel(registry, toggle, text, font, size.y * 0.7f);
    auto& labelRect = registry.get<UIRect>(label);
    // Narrowed by the box and a gap, and moved right by half that, so it
    // starts just past the box.
    labelRect.sizeDelta = {-(size.y + 8.0f), 0.0f};
    labelRect.anchoredPosition = {size.y * 0.5f + 4.0f, 0.0f};
    registry.get<UIText>(label).alignH = TextAlignH::Left;
    registry.get<UIInteractable>(toggle).tintTargets = {box, checkmark, label};

    auto& state = registry.emplace<UIToggle>(toggle);
    state.isOn = isOn;
    state.checkmark = checkmark;
    registry.get<UIRect>(checkmark).visible = isOn;
    return toggle;
}

entt::entity createSlider(entt::registry& registry, entt::entity parent, float minValue, float maxValue,
                          float value, glm::vec2 size) {
    const entt::entity slider = createElement(registry, parent, size);
    addInteractable(registry, slider);

    const float trackHeight = size.y * 0.3f;
    const entt::entity track = createElement(registry, slider, {0.0f, trackHeight});
    auto& trackRect = registry.get<UIRect>(track);
    trackRect.anchorMin = {0.0f, 0.5f};
    trackRect.anchorMax = {1.0f, 0.5f};
    registry.emplace<UIPanel>(track).color = kTrack;

    const entt::entity fill = createElement(registry, slider, {0.0f, trackHeight});
    auto& fillRect = registry.get<UIRect>(fill);
    fillRect.anchorMin = {0.0f, 0.5f};
    fillRect.anchorMax = {0.0f, 0.5f};
    fillRect.pivot = {0.0f, 0.5f};
    registry.emplace<UIImage>(fill).color = kAccent;

    const entt::entity handle = createElement(registry, slider, {size.y * 0.6f, size.y});
    auto& handleRect = registry.get<UIRect>(handle);
    handleRect.anchorMin = handleRect.anchorMax = {0.0f, 0.5f};
    registry.emplace<UIPanel>(handle).color = kHandle;
    registry.get<UIInteractable>(slider).tintTargets = {fill, handle};

    auto& state = registry.emplace<UISlider>(slider);
    state.minValue = minValue;
    state.maxValue = maxValue;
    state.value = value;
    state.fill = fill;
    state.handle = handle;
    const float t = state.normalized();
    fillRect.anchorMax.x = t;
    handleRect.anchorMin.x = handleRect.anchorMax.x = t;
    return slider;
}

namespace {

entt::entity createScrollbar(entt::registry& registry, entt::entity view, bool vertical, bool otherBar) {
    const float inset = otherBar ? kScrollbarThickness + 2.0f : 0.0f;
    const entt::entity bar = createElement(registry, view, glm::vec2(0.0f));
    auto& rect = registry.get<UIRect>(bar);
    if (vertical) {
        rect.anchorMin = {1.0f, 0.0f};
        rect.anchorMax = {1.0f, 1.0f};
        rect.pivot = {1.0f, 0.0f};
        rect.sizeDelta = {kScrollbarThickness, -inset};
    } else {
        rect.anchorMin = {0.0f, 1.0f};
        rect.anchorMax = {1.0f, 1.0f};
        rect.pivot = {0.0f, 1.0f};
        rect.sizeDelta = {-inset, kScrollbarThickness};
    }
    registry.emplace<UIPanel>(bar).color = kTrack;
    addInteractable(registry, bar);

    // Its span along the bar is set from the view's position each frame.
    const entt::entity handle = stretched(registry, bar);
    registry.emplace<UIPanel>(handle).color = {0.45f, 0.48f, 0.56f, 1.0f};

    auto& scrollbar = registry.emplace<UIScrollbar>(bar);
    scrollbar.vertical = vertical;
    scrollbar.scrollView = view;
    scrollbar.handle = handle;
    return bar;
}

} // namespace

entt::entity createScrollView(entt::registry& registry, entt::entity parent, glm::vec2 size,
                              bool horizontal, bool vertical) {
    const entt::entity view = createElement(registry, parent, size);
    registry.emplace<UIClip>(view);

    const entt::entity content = createElement(registry, view, glm::vec2(0.0f));
    auto& rect = registry.get<UIRect>(content);
    rect.anchorMin = rect.anchorMax = rect.pivot = {0.0f, 0.0f};
    auto& column = registry.emplace<UIVerticalLayout>(content);
    auto& fitter = registry.emplace<UIContentSizeFitter>(content);
    fitter.vertical = UIContentSizeFitter::Fit::Preferred;
    if (horizontal) {
        fitter.horizontal = UIContentSizeFitter::Fit::Preferred;
    } else {
        // As wide as the view, less its scrollbar.
        rect.anchorMax.x = 1.0f;
        rect.sizeDelta.x = vertical ? -(kScrollbarThickness + 2.0f) : 0.0f;
        column.childForceExpand.x = true;
    }

    auto& scrollView = registry.emplace<UIScrollView>(view);
    scrollView.content = content;
    scrollView.horizontal = horizontal;
    scrollView.vertical = vertical;
    if (vertical) scrollView.verticalScrollbar = createScrollbar(registry, view, true, horizontal);
    if (horizontal) scrollView.horizontalScrollbar = createScrollbar(registry, view, false, vertical);
    return view;
}

entt::entity createTextField(entt::registry& registry, entt::entity parent, const std::string& placeholder,
                             uint32_t font, glm::vec2 size) {
    const entt::entity field = createElement(registry, parent, size);
    registry.emplace<UIPanel>(field).color = kTrack;
    addInteractable(registry, field).capturesKeyboard = true;

    const entt::entity viewport = stretched(registry, field, kTextFieldPadding);
    registry.emplace<UIClip>(viewport);

    // Placed each frame by CanvasTextSystem; the selection draws behind the
    // text and the caret over it.
    const entt::entity selection = createElement(registry, viewport, glm::vec2(0.0f));
    registry.emplace<UIImage>(selection).raycastTarget = false;
    registry.get<UIRect>(selection).visible = false;

    const entt::entity text = stretched(registry, viewport);
    auto& label = registry.emplace<UIText>(text);
    label.font = font;
    label.fontSize = size.y * 0.5f;
    label.alignH = TextAlignH::Left;
    label.alignV = TextAlignV::Middle;
    label.wrap = false;

    const entt::entity caret = createElement(registry, viewport, glm::vec2(0.0f));
    registry.emplace<UIImage>(caret).raycastTarget = false;
    registry.get<UIRect>(caret).visible = false;

    auto& state = registry.emplace<UITextField>(field);
    state.placeholder = placeholder;
    state.textElement = text;
    state.selectionElement = selection;
    state.caretElement = caret;
    return field;
}

} // namespace UI
} // namespace Shoonyakasha
