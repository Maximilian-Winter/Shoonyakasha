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

/// A scroll view of `size` that clips and scrolls a column; returns the
/// view. Add children to its content (UIScrollView::content), a column that
/// grows with them. Scrolling only vertically, the column spans the view's
/// width less its scrollbar; with `horizontal` it keeps its children's widths
/// and scrolls sideways too. Scrollbars kScrollbarThickness wide run inside
/// the right and bottom edges, hidden while the content fits.
entt::entity createScrollView(entt::registry& registry, entt::entity parent, glm::vec2 size,
                              bool horizontal = false, bool vertical = true);

constexpr float kScrollbarThickness = 10.0f;

/// A single-line text field: a panel with UITextField and a UIInteractable
/// that captures the keyboard, and inside it, inset by kTextFieldPadding, a
/// viewport that clips the selection, the text and the caret. The text is
/// size.y * 0.5 high.
entt::entity createTextField(entt::registry& registry, entt::entity parent, const std::string& placeholder,
                             uint32_t font, glm::vec2 size = {240.0f, 36.0f});

constexpr float kTextFieldPadding = 8.0f;

} // namespace UI
} // namespace Shoonyakasha
