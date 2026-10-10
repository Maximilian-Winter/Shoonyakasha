//
// CanvasInput.cpp - Pointer input for the canvas UI
//

#include "UI/UISystems.h"
#include "UI/UIComponents.h"
#include "UI/UIHitTest.h"

#include "ECS/CameraController.h"
#include "ECS/Core.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Shoonyakasha {
namespace UI {

namespace {

std::optional<ECS::Ray> mainCameraRay(const entt::registry& registry, glm::vec2 pixel, glm::vec2 screen) {
    if (screen.x <= 0.0f || screen.y <= 0.0f) return std::nullopt;
    for (auto [entity, camera] : registry.view<const ECS::CameraComponent>().each()) {
        if (camera.isMainCamera) return ECS::screenPointToRay(pixel, screen, camera.viewMatrix, camera.projectionMatrix);
    }
    return std::nullopt;
}

/// Sets the slider's value from where the pointer is across its rect.
/// Returns whether the value changed.
bool dragSlider(entt::registry& registry, entt::entity entity, glm::vec2 pixel, const std::optional<ECS::Ray>& ray) {
    auto& slider = registry.get<UISlider>(entity);
    const auto* element = registry.try_get<UIRect>(entity);
    if (!element || element->canvas == entt::null) return false;
    const auto position = canvasPosition(registry, element->canvas, pixel, ray);
    const float width = element->rect.size().x;
    if (!position || width <= 0.0f) return false;

    const float t = std::clamp((position->x - element->rect.min.x) / width, 0.0f, 1.0f);
    float value = slider.minValue + t * (slider.maxValue - slider.minValue);
    if (slider.wholeNumbers) value = std::round(value);
    if (value == slider.value) return false;
    slider.value = value;
    return true;
}

// ─── Scrolling ──────────────────────────────────────────────────

/// The scroll views at and above `element`, innermost first.
std::vector<entt::entity> scrollViewsAbove(const entt::registry& registry, entt::entity element) {
    std::vector<entt::entity> views;
    for (entt::entity e = element; e != entt::null && registry.valid(e);) {
        if (registry.all_of<UICanvas>(e)) break;
        if (registry.all_of<UIScrollView>(e)) views.push_back(e);
        const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(e);
        e = hierarchy ? hierarchy->parent : entt::null;
    }
    return views;
}

bool scrolls(const UIScrollView& view, int axis) {
    return (axis == 0 ? view.horizontal : view.vertical) && view.range()[axis] > 0.0f;
}

float offsetOf(const UIScrollView& view, int axis) {
    return view.scrollPosition[axis] * view.range()[axis];
}

/// Moves the view to `offset` canvas units along `axis`, clamped. Returns
/// whether it moved.
bool setOffset(UIScrollView& view, int axis, float offset) {
    const float range = view.range()[axis];
    if (range <= 0.0f) return false;
    const float position = std::clamp(offset / range, 0.0f, 1.0f);
    if (position == view.scrollPosition[axis]) return false;
    view.scrollPosition[axis] = position;
    return true;
}

/// Scrolls the innermost view above `element` that can move, by `steps` of
/// its wheelSpeed along `axis`; positive steps move towards the end.
void wheelScroll(entt::registry& registry, entt::entity element, int axis, float steps) {
    for (const entt::entity e : scrollViewsAbove(registry, element)) {
        auto& view = registry.get<UIScrollView>(e);
        // A view that scrolls only sideways takes the vertical wheel too.
        const int viewAxis = axis == 1 && !view.vertical && view.horizontal ? 0 : axis;
        if (!scrolls(view, viewAxis)) continue;
        if (setOffset(view, viewAxis, offsetOf(view, viewAxis) + steps * view.wheelSpeed)) {
            view.velocity[viewAxis] = 0.0f;
            return;
        }
    }
}

void coast(entt::registry& registry, entt::entity dragged, float dt) {
    if (dt <= 0.0f) return;
    for (auto [entity, view] : registry.view<UIScrollView>().each()) {
        if (entity == dragged) continue;
        for (int axis = 0; axis < 2; ++axis) {
            float& v = view.velocity[axis];
            if (v == 0.0f) continue;
            if (!scrolls(view, axis) || !setOffset(view, axis, offsetOf(view, axis) + v * dt)) {
                v = 0.0f;   // at an end, or nothing to scroll
                continue;
            }
            v *= std::pow(std::clamp(view.decelerationRate, 0.0f, 1.0f), dt);
            if (std::abs(v) < 1.0f) v = 0.0f;
        }
    }
}

/// The scrollbar's track span along its axis, and its handle's start.
struct BarGeometry {
    float trackMin = 0.0f;
    float trackLength = 0.0f;
    float handleMin = 0.0f;
    float handleLength = 0.0f;
};

BarGeometry barGeometry(const UIRect& bar, const UIScrollbar& scrollbar) {
    const int axis = scrollbar.vertical ? 1 : 0;
    BarGeometry g;
    g.trackMin = bar.rect.min[axis];
    g.trackLength = bar.rect.max[axis] - bar.rect.min[axis];
    g.handleLength = scrollbar.size * g.trackLength;
    g.handleMin = g.trackMin + scrollbar.value * (g.trackLength - g.handleLength);
    return g;
}

void syncScrollbars(entt::registry& registry) {
    for (auto [entity, scrollbar] : registry.view<UIScrollbar>().each()) {
        const auto* view = registry.try_get<UIScrollView>(scrollbar.scrollView);
        if (!view) continue;
        const int axis = scrollbar.vertical ? 1 : 0;
        scrollbar.value = view->scrollPosition[axis];
        scrollbar.size = view->contentSize[axis] > 0.0f
            ? std::clamp(view->viewportSize[axis] / view->contentSize[axis], 0.0f, 1.0f)
            : 1.0f;
        if (auto* handle = registry.try_get<UIRect>(scrollbar.handle)) {
            handle->anchorMin[axis] = scrollbar.value * (1.0f - scrollbar.size);
            handle->anchorMax[axis] = handle->anchorMin[axis] + scrollbar.size;
        }
        if (scrollbar.autoHide) {
            if (auto* bar = registry.try_get<UIRect>(entity)) bar->visible = scrollbar.size < 0.999f;
        }
    }
}

void syncWidgets(entt::registry& registry) {
    syncScrollbars(registry);
    for (auto [entity, toggle] : registry.view<UIToggle>().each()) {
        if (auto* checkmark = registry.try_get<UIRect>(toggle.checkmark)) checkmark->visible = toggle.isOn;
    }
    for (auto [entity, slider] : registry.view<UISlider>().each()) {
        const float low = std::min(slider.minValue, slider.maxValue);
        const float high = std::max(slider.minValue, slider.maxValue);
        slider.value = std::clamp(slider.value, low, high);
        const float t = std::clamp(slider.normalized(), 0.0f, 1.0f);
        if (auto* fill = registry.try_get<UIRect>(slider.fill)) fill->anchorMax.x = t;
        if (auto* handle = registry.try_get<UIRect>(slider.handle)) handle->anchorMin.x = handle->anchorMax.x = t;
    }
}

} // namespace

CanvasInputSystem::CanvasInputSystem(const glm::vec2* screenSize, const glm::vec2* windowSize,
                                     UIPointerState* pointer, UIFocusState* focus)
    : m_screenSize(screenSize), m_windowSize(windowSize), m_pointer(pointer), m_focus(focus) {
    priority = kPriority;
    name = "CanvasInputSystem";
}

void CanvasInputSystem::update(entt::registry& registry, float deltaTime) {
    if (!enabled) return;

    for (auto [entity, interactable] : registry.view<UIInteractable>().each()) {
        interactable.hovered = false;
        interactable.pressed = false;
        interactable.focused = false;
        interactable.clicked = false;
        interactable.valueChanged = false;
    }

    const auto inputs = registry.view<const ECS::InputStateComponent>();
    const entt::entity inputEntity = inputs.front();
    const ECS::InputStateComponent* input =
        inputEntity != entt::null ? &inputs.get<const ECS::InputStateComponent>(inputEntity) : nullptr;
    UIPointerState pointer;
    if (!input || !m_screenSize) {
        m_captured = entt::null;
        m_dragView = entt::null;
        m_wasDown = false;
        coast(registry, entt::null, deltaTime);
        syncWidgets(registry);
        if (m_pointer) *m_pointer = pointer;
        return;
    }

    const glm::vec2 screen = *m_screenSize;
    const glm::vec2 window = m_windowSize && m_windowSize->x > 0.0f && m_windowSize->y > 0.0f ? *m_windowSize : screen;
    const glm::vec2 pixel = input->mouseCaptured ? screen * 0.5f : input->mousePosition * (screen / window);
    const auto ray = mainCameraRay(registry, pixel, screen);
    const bool down = input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);
    const bool pressedNow = down && !m_wasDown;

    const auto hit = findPointerHit(registry, pixel, ray);
    const entt::entity hitElement = hit ? hit->element : entt::null;
    entt::entity hovered = interactableFor(registry, hitElement);
    if (hovered != entt::null && !registry.get<UIInteractable>(hovered).enabled) hovered = entt::null;

    if (m_captured != entt::null &&
        (!registry.valid(m_captured) || !registry.all_of<UIInteractable>(m_captured) ||
         !registry.get<UIInteractable>(m_captured).enabled)) {
        m_captured = entt::null;
    }
    if (m_captured == entt::null && pressedNow) m_captured = hovered;

    // ── Scrolling: the wheel, then dragging the content ──
    if (hitElement != entt::null && input->scrollDelta != glm::vec2(0.0f)) {
        // Turning the wheel towards the user (negative y) moves on down the
        // content; tilting it right (positive x) moves on to the right.
        if (input->scrollDelta.x != 0.0f) wheelScroll(registry, hitElement, 0, input->scrollDelta.x);
        if (input->scrollDelta.y != 0.0f) wheelScroll(registry, hitElement, 1, -input->scrollDelta.y);
    }
    updateDrag(registry, pixel, ray, down, pressedNow, hitElement, deltaTime);
    coast(registry, m_dragging ? m_dragView : entt::null, deltaTime);

    if (m_captured != entt::null) {
        const entt::entity captured = m_captured;
        auto& interactable = registry.get<UIInteractable>(captured);
        interactable.hovered = hovered == captured;
        interactable.pressed = down;
        if (registry.all_of<UISlider>(captured) && dragSlider(registry, captured, pixel, ray)) {
            interactable.valueChanged = true;
        }
        if (registry.all_of<UIScrollbar>(captured) && dragScrollbar(registry, captured, pixel, ray, pressedNow)) {
            interactable.valueChanged = true;
        }
        if (!down) {
            if (interactable.hovered) {
                interactable.clicked = true;
                if (auto* toggle = registry.try_get<UIToggle>(captured)) {
                    toggle->isOn = !toggle->isOn;
                    interactable.valueChanged = true;
                }
            }
            m_captured = entt::null;
        }
        pointer.pressed = m_captured;
        pointer.hovered = interactable.hovered ? captured : entt::null;
    } else if (hovered != entt::null) {
        registry.get<UIInteractable>(hovered).hovered = true;
        pointer.hovered = hovered;
    }
    m_wasDown = down;

    updateFocus(registry, *input, pressedNow, hovered);

    if (hit) {
        pointer.canvas = hit->canvas;
        pointer.position = hit->position;
    }
    pointer.overUI = (hit && hit->element != entt::null) || pointer.pressed != entt::null || m_dragging;
    if (m_pointer) *m_pointer = pointer;

    syncWidgets(registry);
}

void CanvasInputSystem::updateDrag(entt::registry& registry, glm::vec2 pixel, const std::optional<ECS::Ray>& ray,
                                   bool down, bool pressedNow, entt::entity hitElement, float deltaTime) {
    if (m_dragView != entt::null && !registry.all_of<UIScrollView, UIRect>(m_dragView)) {
        m_dragView = entt::null;
        m_dragging = false;
    }

    if (pressedNow) {
        // A press on a slider or scrollbar drags that, not the view.
        const bool ownDrag = m_captured != entt::null && registry.any_of<UISlider, UIScrollbar>(m_captured);
        const auto views = ownDrag ? std::vector<entt::entity>{} : scrollViewsAbove(registry, hitElement);
        m_dragView = views.empty() ? entt::null : views.front();
        m_dragging = false;
        if (m_dragView != entt::null) {
            const auto start = canvasPosition(registry, registry.get<UIRect>(m_dragView).canvas, pixel, ray);
            if (start) {
                m_pressPosition = m_lastDragPosition = *start;
                registry.get<UIScrollView>(m_dragView).velocity = glm::vec2(0.0f);
            } else {
                m_dragView = entt::null;
            }
        }
    }

    if (m_dragView == entt::null) return;
    auto& view = registry.get<UIScrollView>(m_dragView);

    if (!down) {
        if (!m_dragging || !view.inertia) view.velocity = glm::vec2(0.0f);
        m_dragView = entt::null;
        m_dragging = false;
        return;
    }

    const auto position = canvasPosition(registry, registry.get<UIRect>(m_dragView).canvas, pixel, ray);
    if (!position) return;

    if (!m_dragging && glm::length(*position - m_pressPosition) > UIScrollView::kDragThreshold) {
        m_dragging = true;
        m_dragStartOffset = {offsetOf(view, 0), offsetOf(view, 1)};
        m_captured = entt::null;   // the press on a button under it is no longer a click
    }
    if (!m_dragging) return;

    const glm::vec2 moved = *position - m_pressPosition;
    for (int axis = 0; axis < 2; ++axis) {
        if (scrolls(view, axis)) setOffset(view, axis, m_dragStartOffset[axis] - moved[axis]);
    }
    if (deltaTime > 0.0f) {
        // Content follows the pointer, so the offset moves against it.
        const glm::vec2 velocity = -(*position - m_lastDragPosition) / deltaTime;
        view.velocity = glm::mix(view.velocity, velocity, 0.5f);
    }
    m_lastDragPosition = *position;
}

// ─── Focus and navigation ───────────────────────────────────────

namespace {

bool canFocus(const entt::registry& registry, entt::entity e) {
    if (e == entt::null || !registry.valid(e)) return false;
    const auto* interactable = registry.try_get<UIInteractable>(e);
    const auto* rect = registry.try_get<UIRect>(e);
    return interactable && interactable->enabled && rect && rect->visibleInHierarchy && rect->canvas != entt::null;
}

glm::vec2 centreOf(const entt::registry& registry, entt::entity e) {
    const Rect& r = registry.get<UIRect>(e).rect;
    return (r.min + r.max) * 0.5f;
}

/// The canvas navigation moves on: the focused element's, else the topmost
/// screen canvas with a navigable element.
entt::entity navigationCanvas(const entt::registry& registry, entt::entity focused) {
    if (canFocus(registry, focused)) return registry.get<UIRect>(focused).canvas;
    entt::entity best = entt::null;
    int bestOrder = 0;
    for (auto [entity, interactable] : registry.view<const UIInteractable>().each()) {
        if (!interactable.navigable || !canFocus(registry, entity)) continue;
        const entt::entity canvas = registry.get<UIRect>(entity).canvas;
        const auto& c = registry.get<UICanvas>(canvas);
        if (c.mode != UICanvas::Mode::ScreenOverlay) continue;
        if (best == entt::null || c.sortOrder > bestOrder) {
            best = canvas;
            bestOrder = c.sortOrder;
        }
    }
    return best;
}

/// Navigable elements of `canvas`, in draw order.
std::vector<entt::entity> navigable(const entt::registry& registry, entt::entity canvas) {
    std::vector<std::pair<uint32_t, entt::entity>> found;
    for (auto [entity, interactable] : registry.view<const UIInteractable>().each()) {
        if (!interactable.navigable || !canFocus(registry, entity)) continue;
        const auto& rect = registry.get<UIRect>(entity);
        if (rect.canvas == canvas) found.emplace_back(rect.drawOrder, entity);
    }
    std::sort(found.begin(), found.end());
    std::vector<entt::entity> out;
    for (const auto& [order, entity] : found) out.push_back(entity);
    return out;
}

entt::entity nextInOrder(const entt::registry& registry, entt::entity focused, int step) {
    const auto list = navigable(registry, navigationCanvas(registry, focused));
    if (list.empty()) return focused;
    const auto it = std::find(list.begin(), list.end(), focused);
    if (it == list.end()) return step > 0 ? list.front() : list.back();
    const auto n = static_cast<long>(list.size());
    const long index = (static_cast<long>(it - list.begin()) + step + n) % n;
    return list[static_cast<size_t>(index)];
}

/// The nearest navigable element from the focused one in `direction` (y
/// down), weighing sideways distance double. Nothing focused: the first.
entt::entity nextInDirection(const entt::registry& registry, entt::entity focused, glm::vec2 direction) {
    const auto list = navigable(registry, navigationCanvas(registry, focused));
    if (list.empty()) return focused;
    if (!canFocus(registry, focused)) return list.front();
    const glm::vec2 from = centreOf(registry, focused);
    entt::entity best = focused;
    float bestScore = 0.0f;
    for (const entt::entity e : list) {
        if (e == focused) continue;
        const glm::vec2 d = centreOf(registry, e) - from;
        const float along = glm::dot(d, direction);
        if (along <= 0.5f) continue;
        const float across = std::abs(d.x * direction.y - d.y * direction.x);
        const float score = along + 2.0f * across;
        if (best == focused || score < bestScore) {
            best = e;
            bestScore = score;
        }
    }
    return best;
}

/// Clicks a button or flips a toggle.
void activate(entt::registry& registry, entt::entity e) {
    auto& interactable = registry.get<UIInteractable>(e);
    interactable.clicked = true;
    if (auto* toggle = registry.try_get<UIToggle>(e)) {
        toggle->isOn = !toggle->isOn;
        interactable.valueChanged = true;
    }
}

/// Moves a slider a tenth of its range, or by one in whole numbers.
void stepSlider(entt::registry& registry, entt::entity e, int direction) {
    auto& slider = registry.get<UISlider>(e);
    const float step = slider.wholeNumbers ? 1.0f : (slider.maxValue - slider.minValue) / 10.0f;
    const float low = std::min(slider.minValue, slider.maxValue);
    const float high = std::max(slider.minValue, slider.maxValue);
    const float value = std::clamp(slider.value + static_cast<float>(direction) * step, low, high);
    if (value == slider.value) return;
    slider.value = value;
    registry.get<UIInteractable>(e).valueChanged = true;
}

} // namespace

void CanvasInputSystem::updateFocus(entt::registry& registry, const ECS::InputStateComponent& input,
                                    bool pressedNow, entt::entity hovered) {
    if (!m_focus) return;
    UIFocusState& focus = *m_focus;
    if (!canFocus(registry, focus.focused)) focus.focused = entt::null;

    // A press focuses what it lands on, or clears focus.
    if (pressedNow) focus.focused = canFocus(registry, hovered) ? hovered : entt::null;

    auto move = [&](entt::entity to) { if (to != entt::null) focus.focused = to; };
    auto captured = [&] {
        return focus.focused != entt::null && registry.get<UIInteractable>(focus.focused).capturesKeyboard;
    };
    auto arrow = [&](glm::vec2 direction) {
        // A focused slider takes Left and Right.
        if (direction.y == 0.0f && focus.focused != entt::null && registry.all_of<UISlider>(focus.focused)) {
            stepSlider(registry, focus.focused, direction.x > 0.0f ? 1 : -1);
        } else {
            move(nextInDirection(registry, focus.focused, direction));
        }
    };
    auto activateFocused = [&] {
        if (focus.focused != entt::null) activate(registry, focus.focused);
    };

    for (const auto& key : input.keyPresses) {
        if (key.key == GLFW_KEY_ESCAPE) {
            if (!key.repeat) focus.focused = entt::null;
        } else if (key.key == GLFW_KEY_TAB) {
            move(nextInOrder(registry, focus.focused, (key.mods & GLFW_MOD_SHIFT) ? -1 : 1));
        } else if (captured()) {
            continue;   // the focused element takes every other key
        } else if (key.key == GLFW_KEY_ENTER || key.key == GLFW_KEY_KP_ENTER || key.key == GLFW_KEY_SPACE) {
            if (!key.repeat) activateFocused();
        } else if (key.key == GLFW_KEY_LEFT) {
            arrow({-1.0f, 0.0f});
        } else if (key.key == GLFW_KEY_RIGHT) {
            arrow({1.0f, 0.0f});
        } else if (key.key == GLFW_KEY_UP) {
            arrow({0.0f, -1.0f});
        } else if (key.key == GLFW_KEY_DOWN) {
            arrow({0.0f, 1.0f});
        }
    }

    // The gamepad, on each button's press.
    const auto& pad = input.gamepad;
    auto pressed = [&](int button) { return pad.connected && pad.buttons[button] && !m_lastPadButtons[button]; };
    if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT)) arrow({-1.0f, 0.0f});
    if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT)) arrow({1.0f, 0.0f});
    if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_UP)) arrow({0.0f, -1.0f});
    if (pressed(GLFW_GAMEPAD_BUTTON_DPAD_DOWN)) arrow({0.0f, 1.0f});
    if (pressed(GLFW_GAMEPAD_BUTTON_A)) activateFocused();
    if (pressed(GLFW_GAMEPAD_BUTTON_B)) focus.focused = entt::null;
    m_lastPadButtons = pad.connected ? pad.buttons : decltype(m_lastPadButtons){};

    if (focus.focused != entt::null) registry.get<UIInteractable>(focus.focused).focused = true;
    focus.keyboardCaptured = captured();
}

bool CanvasInputSystem::dragScrollbar(entt::registry& registry, entt::entity bar, glm::vec2 pixel,
                                      const std::optional<ECS::Ray>& ray, bool pressedNow) {
    auto& scrollbar = registry.get<UIScrollbar>(bar);
    auto* view = registry.try_get<UIScrollView>(scrollbar.scrollView);
    const auto& rect = registry.get<UIRect>(bar);
    if (!view || rect.canvas == entt::null) return false;
    const auto position = canvasPosition(registry, rect.canvas, pixel, ray);
    if (!position) return false;

    const int axis = scrollbar.vertical ? 1 : 0;
    const float p = (*position)[axis];
    const BarGeometry g = barGeometry(rect, scrollbar);
    const float travel = g.trackLength - g.handleLength;
    float value = view->scrollPosition[axis];

    if (pressedNow) {
        if (p >= g.handleMin && p <= g.handleMin + g.handleLength) {
            m_barGrab = p - g.handleMin;               // drag the handle from where it was taken
        } else {
            m_barGrab = -1.0f;                         // page towards the pointer, once
            value += p < g.handleMin ? -scrollbar.size : scrollbar.size;
        }
    } else if (m_barGrab >= 0.0f && travel > 0.0f) {
        value = (p - m_barGrab - g.trackMin) / travel;
    }

    value = std::clamp(value, 0.0f, 1.0f);
    if (value == view->scrollPosition[axis]) return false;
    view->scrollPosition[axis] = value;
    view->velocity[axis] = 0.0f;
    scrollbar.value = value;
    return true;
}

} // namespace UI
} // namespace Shoonyakasha
