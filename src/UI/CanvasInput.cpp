//
// CanvasInput.cpp - Pointer input for the canvas UI
//

#include "UI/UISystems.h"
#include "UI/TextEditing.h"
#include "UI/UIComponents.h"
#include "UI/UIHitTest.h"
#include "UI/Utf8.h"

#include "ECS/CameraController.h"
#include "ECS/Core.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <limits>
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

/// How far along `ray` a world canvas may be hit: as far as the scene seen
/// under the pointer, plus CanvasInputSystem's occlusion margin. Without
/// limit when the depth is not known or nothing was drawn there.
float worldHitLimit(const entt::registry& registry, const UIPointerDepth* depth, glm::vec2 pixel, glm::vec2 screen,
                    const std::optional<ECS::Ray>& ray) {
    constexpr float kNone = std::numeric_limits<float>::infinity();
    if (!depth || !depth->valid || !ray || depth->depth >= 1.0f) return kNone;
    for (auto [entity, camera] : registry.view<const ECS::CameraComponent>().each()) {
        if (!camera.isMainCamera) continue;
        const glm::vec3 seen = ECS::screenPointToWorld(pixel, screen, depth->depth, camera.viewMatrix,
                                                       camera.projectionMatrix);
        const float distance = glm::dot(seen - ray->origin, ray->direction);
        return distance * (1.0f + CanvasInputSystem::kOcclusionMarginPerUnit) + CanvasInputSystem::kOcclusionMargin;
    }
    return kNone;
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
                                     UIPointerState* pointer, UIFocusState* focus, UIClipboard* clipboard,
                                     const UIPointerDepth* depth)
    : m_screenSize(screenSize), m_windowSize(windowSize), m_pointer(pointer), m_depth(depth), m_focus(focus),
      m_clipboard(clipboard) {
    priority = kPriority;
    name = "CanvasInputSystem";
}

void CanvasInputSystem::update(entt::registry& registry, float deltaTime) {
    if (!enabled) return;
    m_time += deltaTime;

    for (auto [entity, interactable] : registry.view<UIInteractable>().each()) {
        interactable.hovered = false;
        interactable.pressed = false;
        interactable.focused = false;
        interactable.clicked = false;
        interactable.valueChanged = false;
    }
    for (auto [entity, field] : registry.view<UITextField>().each()) {
        field.changed = false;
        field.submitted = false;
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

    pointer.pixel = pixel;
    const auto hit = findPointerHit(registry, pixel, ray, worldHitLimit(registry, m_depth, pixel, screen, ray));
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
        if (down && registry.all_of<UITextField>(captured)) {
            const bool shift = input->isKeyDown(GLFW_KEY_LEFT_SHIFT) || input->isKeyDown(GLFW_KEY_RIGHT_SHIFT);
            pointTextField(registry, captured, pixel, ray, pressedNow, shift);
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
    const entt::entity focused = m_focus ? m_focus->focused : entt::null;
    if (auto* field = focused != entt::null ? registry.try_get<UITextField>(focused) : nullptr) {
        if (focused != m_lastFocused) field->blinkTime = 0.0f;   // the caret shows at once
        editTextField(registry, focused, *input);
    }
    m_lastFocused = focused;

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
        // A press on a slider, scrollbar or text field drags that, not the view.
        const bool ownDrag = m_captured != entt::null &&
                             registry.any_of<UISlider, UIScrollbar, UITextField>(m_captured);
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

// ─── Text fields ────────────────────────────────────────────────

namespace {

/// The field's text, caret and selection, with caret stops from its text
/// element's layout when that layout is of the text as it is now.
TextEditBuffer bufferOf(const entt::registry& registry, const UITextField& field) {
    TextEditBuffer buffer;
    buffer.text = decodeUtf8(field.text);
    buffer.caret = std::min(field.caret, buffer.text.size());
    buffer.anchor = std::min(field.selectionAnchor, buffer.text.size());
    const auto* cache = registry.try_get<UITextCache>(field.textElement);
    if (cache && !buffer.text.empty() && cache->text == field.displayText()) {
        buffer.stops = caretStops(cache->layout.clusters, buffer.text.size());
    }
    return buffer;
}

void store(UITextField& field, const TextEditBuffer& buffer, bool textChanged) {
    if (textChanged) {
        field.text = encodeUtf8(buffer.text);
        field.changed = true;
    }
    field.caret = buffer.caret;
    field.selectionAnchor = buffer.anchor;
    field.blinkTime = 0.0f;
}

/// `text` for a single line: line breaks and tabs become spaces, and other
/// control characters are dropped.
std::u32string singleLine(std::u32string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char32_t c = text[i];
        if (c == U'\r' && i + 1 < text.size() && text[i + 1] == U'\n') continue;
        if (c == U'\n' || c == U'\r' || c == U'\t') out.push_back(U' ');
        else if (c >= 0x20 && c != 0x7F) out.push_back(c);
    }
    return out;
}

} // namespace

void CanvasInputSystem::pointTextField(entt::registry& registry, entt::entity entity, glm::vec2 pixel,
                                       const std::optional<ECS::Ray>& ray, bool pressedNow, bool shift) {
    auto& field = registry.get<UITextField>(entity);
    const auto* textRect = registry.try_get<UIRect>(field.textElement);
    if (!textRect || textRect->canvas == entt::null) return;
    const auto position = canvasPosition(registry, textRect->canvas, pixel, ray);
    if (!position) return;

    TextEditBuffer buffer = bufferOf(registry, field);
    size_t index = buffer.text.size();
    const auto* cache = registry.try_get<UITextCache>(field.textElement);
    if (cache && !buffer.stops.empty()) {
        index = caretAt(cache->layout, buffer.text.size(), position->x - textRect->rect.min.x);
    }

    if (pressedNow) {
        const bool doubleClick = m_lastPressField == entity && m_time - m_lastPressTime <= kDoubleClickTime;
        m_lastPressField = entity;
        m_lastPressTime = doubleClick ? -1.0f : m_time;   // a third press starts over
        m_selectingWords = doubleClick;
        if (!doubleClick) {
            buffer.moveTo(index, shift);
        } else if (field.password) {
            buffer.anchor = 0;                             // a mask has no words to show
            buffer.caret = buffer.text.size();
        } else {
            const auto [start, end] = buffer.wordAt(index);
            buffer.anchor = start;
            buffer.caret = end;
        }
    } else if (!m_selectingWords) {
        if (index == buffer.caret) return;
        buffer.moveTo(index, true);
    } else {
        return;
    }
    store(field, buffer, false);
}

void CanvasInputSystem::editTextField(entt::registry& registry, entt::entity entity,
                                      const ECS::InputStateComponent& input) {
    if (input.textInput.empty() && input.keyPresses.empty()) return;
    auto& field = registry.get<UITextField>(entity);
    TextEditBuffer buffer = bufferOf(registry, field);
    const bool editable = !field.readOnly;
    bool changed = false;

    // Characters typed and keys pressed, in the order they came.
    const std::u32string& typed = input.textInput;
    size_t typedDone = 0;
    auto typeUpTo = [&](size_t end) {
        end = std::min(end, typed.size());
        if (end <= typedDone) return;
        if (editable) changed |= buffer.insert(singleLine(typed.substr(typedDone, end - typedDone)), field.maxLength);
        typedDone = end;
    };

    for (const auto& key : input.keyPresses) {
        typeUpTo(key.textOffset);
        const bool shift = (key.mods & GLFW_MOD_SHIFT) != 0;
        const bool ctrl = (key.mods & GLFW_MOD_CONTROL) != 0;
        // Ctrl or Cmd, but not AltGr, which comes as Ctrl+Alt.
        const bool shortcut = (key.mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0 && !(key.mods & GLFW_MOD_ALT);
        auto copy = [&] {
            if (m_clipboard && buffer.hasSelection() && !field.password) {
                m_clipboard->setText(encodeUtf8(buffer.selectedText()));
            }
        };

        switch (key.key) {
            case GLFW_KEY_LEFT:
                if (buffer.hasSelection() && !shift && !ctrl) buffer.moveTo(buffer.selectionStart(), false);
                else buffer.moveTo(ctrl ? buffer.previousWord(buffer.caret) : buffer.previousStop(buffer.caret), shift);
                break;
            case GLFW_KEY_RIGHT:
                if (buffer.hasSelection() && !shift && !ctrl) buffer.moveTo(buffer.selectionEnd(), false);
                else buffer.moveTo(ctrl ? buffer.nextWord(buffer.caret) : buffer.nextStop(buffer.caret), shift);
                break;
            case GLFW_KEY_HOME:
                buffer.moveTo(0, shift);
                break;
            case GLFW_KEY_END:
                buffer.moveTo(buffer.text.size(), shift);
                break;
            case GLFW_KEY_BACKSPACE:
                if (editable) changed |= buffer.backspace(ctrl);
                break;
            case GLFW_KEY_DELETE:
                if (editable) changed |= buffer.deleteForward(ctrl);
                break;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER:
                if (!key.repeat) field.submitted = true;
                break;
            case GLFW_KEY_A:
                if (shortcut) {
                    buffer.anchor = 0;
                    buffer.caret = buffer.text.size();
                }
                break;
            case GLFW_KEY_C:
                if (shortcut) copy();
                break;
            case GLFW_KEY_X:
                if (shortcut && editable && buffer.hasSelection() && !field.password) {
                    copy();
                    changed |= buffer.backspace();
                }
                break;
            case GLFW_KEY_V:
                if (shortcut && editable && m_clipboard) {
                    changed |= buffer.insert(singleLine(decodeUtf8(m_clipboard->text())), field.maxLength);
                }
                break;
            default:
                break;
        }
    }
    typeUpTo(typed.size());
    store(field, buffer, changed);
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
