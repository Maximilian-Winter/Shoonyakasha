//
// Accessibility.cpp - Describing the screen canvases, and applying what
// screen readers ask for
//

#include "UI/Accessibility.h"
#include "UI/CanvasLayout.h"
#include "UI/RichText.h"
#include "UI/UISystems.h"
#include "UI/Utf8.h"

#include "ECS/Core.h"

#include <algorithm>
#include <cmath>

namespace Shoonyakasha {
namespace UI {

namespace {

entt::entity entityOf(uint32_t id) {
    return static_cast<entt::entity>(id);
}

uint32_t idOf(entt::entity e) {
    return static_cast<uint32_t>(e);
}

/// What a text shows: rich text without its markup.
std::string shownText(const UIText& text) {
    if (!text.richText) return text.text;
    return encodeUtf8(parseRichText(decodeUtf8(text.text)).text);
}

/// The first text at or under `e`, depth first: a widget's label.
const UIText* labelOf(const entt::registry& registry, entt::entity e) {
    if (const auto* text = registry.try_get<UIText>(e)) return text;
    if (const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e)) {
        for (const entt::entity child : hierarchy->children) {
            if (const UIText* label = labelOf(registry, child)) return label;
        }
    }
    return nullptr;
}

std::optional<AccessibleRole> roleOf(const entt::registry& registry, entt::entity e) {
    const auto* accessible = registry.try_get<UIAccessible>(e);
    if (accessible && accessible->role) return accessible->role;
    if (registry.all_of<UIButton>(e)) return AccessibleRole::Button;
    if (registry.all_of<UIToggle>(e)) return AccessibleRole::CheckBox;
    if (registry.all_of<UISlider>(e)) return AccessibleRole::Slider;
    if (registry.all_of<UITextField>(e)) return AccessibleRole::Edit;
    if (registry.all_of<UIScrollView>(e)) return AccessibleRole::Pane;
    if (const auto* text = registry.try_get<UIText>(e); text && !text->text.empty()) return AccessibleRole::Text;
    if (accessible) return AccessibleRole::Group;
    return std::nullopt;
}

/// Whether a node of `role` stands for its descendants too.
bool coversDescendants(AccessibleRole role) {
    return role == AccessibleRole::Button || role == AccessibleRole::CheckBox || role == AccessibleRole::Slider ||
           role == AccessibleRole::Edit;
}

/// Typed or set text as a field takes it: on one line, line breaks as spaces.
std::u32string fieldTextOf(std::u32string text, bool multiLine, size_t maxLength) {
    for (char32_t& c : text) {
        if (c == U'\r' || c == U'\t' || (!multiLine && c == U'\n')) c = U' ';
    }
    if (maxLength > 0 && text.size() > maxLength) text.resize(maxLength);
    return text;
}

struct Builder {
    const entt::registry& registry;
    const UIFocusState* focus;
    glm::vec2 toWindow{1.0f};
    AccessibilitySnapshot& out;

    int add(AccessibleNode node) {
        const int index = static_cast<int>(out.nodes.size());
        if (node.parent >= 0) out.nodes[static_cast<size_t>(node.parent)].children.push_back(index);
        if (node.focused) out.focused = index;
        out.nodes.push_back(std::move(node));
        return index;
    }

    Rect windowRect(const Rect& r, float scale) const {
        return {r.min * scale * toWindow, r.max * scale * toWindow};
    }

    void canvas(entt::entity e, const UICanvas& c) {
        const auto* accessible = registry.try_get<UIAccessible>(e);
        if (accessible && accessible->hidden) return;
        AccessibleNode node;
        node.id = idOf(e);
        node.role = accessible && accessible->role ? *accessible->role : AccessibleRole::Group;
        if (accessible) {
            node.name = accessible->name;
            node.description = accessible->description;
        }
        node.bounds = windowRect({glm::vec2(0.0f), c.size}, c.scale);
        const int index = add(std::move(node));
        children(e, index, e, c.scale);
        // A canvas with nothing to describe, such as a closed dialog's, is left out.
        if (out.nodes[static_cast<size_t>(index)].children.empty()) out.nodes.pop_back();
    }

    void children(entt::entity e, int parent, entt::entity canvasEntity, float scale) {
        const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e);
        if (!hierarchy) return;
        for (const entt::entity child : hierarchy->children) element(child, parent, canvasEntity, scale);
    }

    void element(entt::entity e, int parent, entt::entity canvasEntity, float scale) {
        const auto* rect = registry.try_get<UIRect>(e);
        if (!rect || !rect->visibleInHierarchy || rect->canvas != canvasEntity) return;
        const auto* accessible = registry.try_get<UIAccessible>(e);
        if (accessible && accessible->hidden) return;

        const std::optional<AccessibleRole> role = roleOf(registry, e);
        if (!role) {
            children(e, parent, canvasEntity, scale);   // its descendants hang from its parent
            return;
        }

        AccessibleNode node;
        node.id = idOf(e);
        node.parent = parent;
        node.role = *role;
        const Rect clipped = intersect(rect->rect, rect->clip);
        node.offscreen = clipped.empty();
        node.bounds = windowRect(node.offscreen ? rect->rect : clipped, scale);

        if (const auto* interactable = registry.try_get<UIInteractable>(e)) {
            node.enabled = interactable->enabled;
            node.focusable = interactable->enabled && interactable->navigable;
        }
        node.focused = focus && focus->focused == e;
        if (const auto* toggle = registry.try_get<UIToggle>(e)) node.toggled = toggle->isOn;
        if (const auto* slider = registry.try_get<UISlider>(e)) {
            node.minimum = std::min(slider->minValue, slider->maxValue);
            node.maximum = std::max(slider->minValue, slider->maxValue);
            node.value = slider->value;
            node.smallChange = slider->wholeNumbers ? 1.0f : (node.maximum - node.minimum) / 10.0f;
        }
        if (const auto* field = registry.try_get<UITextField>(e)) {
            node.password = field->password;
            node.readOnly = field->readOnly;
            node.text = field->password ? std::string() : field->text;
            node.selectionStart = std::min(field->caret, field->selectionAnchor);
            node.selectionEnd = std::max(field->caret, field->selectionAnchor);
            node.name = field->placeholder;
        }
        if (const auto* view = registry.try_get<UIScrollView>(e)) {
            node.scrollsHorizontally = view->horizontal && view->contentSize.x > view->viewportSize.x;
            node.scrollsVertically = view->vertical && view->contentSize.y > view->viewportSize.y;
            node.scrollPercent = glm::clamp(view->scrollPosition, glm::vec2(0.0f), glm::vec2(1.0f)) * 100.0f;
            for (int axis = 0; axis < 2; ++axis) {
                node.viewPercent[axis] = view->contentSize[axis] > 0.0f
                    ? std::min(100.0f, view->viewportSize[axis] / view->contentSize[axis] * 100.0f)
                    : 100.0f;
            }
        }
        if (node.role == AccessibleRole::Button || node.role == AccessibleRole::CheckBox) {
            if (const UIText* label = labelOf(registry, e)) node.name = shownText(*label);
        } else if (node.role == AccessibleRole::Text) {
            node.name = shownText(registry.get<UIText>(e));
        }
        if (accessible) {
            if (!accessible->name.empty()) node.name = accessible->name;
            node.description = accessible->description;
        }

        const bool covers = coversDescendants(node.role);
        const int index = add(std::move(node));
        if (!covers) children(e, index, canvasEntity, scale);
    }
};

void apply(entt::registry& registry, UIFocusState* focus, const AccessibleAction& action) {
    const entt::entity e = entityOf(action.node);
    if (!registry.valid(e)) return;
    auto* interactable = registry.try_get<UIInteractable>(e);
    if (interactable && !interactable->enabled) return;

    switch (action.kind) {
    case AccessibleAction::Kind::Invoke:
    case AccessibleAction::Kind::Toggle:
        if (!interactable) return;
        interactable->clicked = true;
        if (auto* toggle = registry.try_get<UIToggle>(e)) {
            toggle->isOn = !toggle->isOn;
            interactable->valueChanged = true;
        }
        break;
    case AccessibleAction::Kind::SetValue:
        if (auto* slider = registry.try_get<UISlider>(e); slider && interactable) {
            float value = std::clamp(action.value, std::min(slider->minValue, slider->maxValue),
                                     std::max(slider->minValue, slider->maxValue));
            if (slider->wholeNumbers) value = std::round(value);
            if (value != slider->value) {
                slider->value = value;
                interactable->valueChanged = true;
            }
        }
        break;
    case AccessibleAction::Kind::SetText:
        if (auto* field = registry.try_get<UITextField>(e); field && !field->readOnly) {
            const std::u32string text = fieldTextOf(decodeUtf8(action.text), field->multiLine, field->maxLength);
            field->text = encodeUtf8(text);
            field->caret = field->selectionAnchor = text.size();
            field->changed = true;
            field->revealCaret = true;
        }
        break;
    case AccessibleAction::Kind::Select:
        if (auto* field = registry.try_get<UITextField>(e)) {
            const size_t length = decodeUtf8(field->text).size();
            field->selectionAnchor = std::min(action.selectionStart, length);
            field->caret = std::min(std::max(action.selectionEnd, action.selectionStart), length);
            field->revealCaret = true;
            field->blinkTime = 0.0f;
        }
        break;
    case AccessibleAction::Kind::Focus:
        if (focus && interactable && interactable->navigable) focus->focused = e;
        break;
    case AccessibleAction::Kind::Scroll:
        if (auto* view = registry.try_get<UIScrollView>(e)) {
            for (int axis = 0; axis < 2; ++axis) {
                if (action.scrollPercent[axis] >= 0.0f) {
                    view->scrollPosition[axis] = std::clamp(action.scrollPercent[axis] / 100.0f, 0.0f, 1.0f);
                }
            }
            view->velocity = glm::vec2(0.0f);
        }
        break;
    }
}

} // namespace

AccessibilitySystem::AccessibilitySystem(AccessibilityState* state, const glm::vec2* screenSize,
                                         const glm::vec2* windowSize, UIFocusState* focus)
    : m_state(state), m_screenSize(screenSize), m_windowSize(windowSize), m_focus(focus) {
    priority = kPriority;
    name = "AccessibilitySystem";
}

void AccessibilitySystem::update(entt::registry& registry, float) {
    if (!enabled || !m_state) return;
    for (const AccessibleAction& action : m_state->takeActions()) apply(registry, m_focus, action);
    if (!m_state->enabled) return;

    AccessibilitySnapshot snapshot;
    snapshot.frame = ++m_frame;
    Builder builder{registry, m_focus, glm::vec2(1.0f), snapshot};
    if (m_screenSize && m_windowSize && m_screenSize->x > 0.0f && m_screenSize->y > 0.0f &&
        m_windowSize->x > 0.0f && m_windowSize->y > 0.0f) {
        builder.toWindow = *m_windowSize / *m_screenSize;
    }

    // Screen canvases, the one drawn first first.
    std::vector<std::pair<int, entt::entity>> canvases;
    for (auto [entity, canvas] : registry.view<const UICanvas>().each()) {
        if (canvas.mode == UICanvas::Mode::ScreenOverlay) canvases.emplace_back(canvas.sortOrder, entity);
    }
    std::stable_sort(canvases.begin(), canvases.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [order, entity] : canvases) builder.canvas(entity, registry.get<UICanvas>(entity));

    m_state->publish(std::move(snapshot));
}

} // namespace UI
} // namespace Shoonyakasha
