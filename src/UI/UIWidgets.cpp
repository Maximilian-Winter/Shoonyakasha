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
    addInteractable(registry, button);
    registry.emplace<UIButton>(button);
    createLabel(registry, button, text, font, size.y * 0.5f);
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

} // namespace UI
} // namespace Shoonyakasha
