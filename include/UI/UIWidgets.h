//
// UIWidgets.h - Building canvas UI elements and widgets
//
// Each function adds an element as the last child of `parent` (a canvas or an
// element), so it draws over its earlier siblings, and returns it. The element
// is centred in its parent at its size; set its UIRect to place it. Colours
// are sRGB with straight alpha.
//

#pragma once

#include "UI/UIComponents.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Shoonyakasha {
namespace UI {

/// An element with only a UIRect, of `size` canvas units.
entt::entity createElement(entt::registry& registry, entt::entity parent, glm::vec2 size);

/// Makes `element` the last child of `parent`, out of any parent it had.
void setParent(entt::registry& registry, entt::entity element, entt::entity parent);

/// A UIText filling its parent, centred, with font `font` (a FontLibrary id).
entt::entity createLabel(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                         float fontSize = 20.0f);

/// A panel with UIButton and UIInteractable, and a centred label child.
entt::entity createButton(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                          glm::vec2 size = {160.0f, 40.0f});

/// A row of a box, which shows a checkmark while on, and a label to its right.
/// The interactable is the row, so clicking the label also flips it.
entt::entity createToggle(entt::registry& registry, entt::entity parent, const std::string& text, uint32_t font,
                          bool isOn = false, glm::vec2 size = {200.0f, 28.0f});

/// A horizontal slider: a track, a fill from its left to the value and a
/// handle on the value.
entt::entity createSlider(entt::registry& registry, entt::entity parent, float minValue, float maxValue,
                          float value, glm::vec2 size = {200.0f, 24.0f});

} // namespace UI
} // namespace Shoonyakasha
