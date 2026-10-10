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

void syncWidgets(entt::registry& registry) {
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
                                     UIPointerState* pointer)
    : m_screenSize(screenSize), m_windowSize(windowSize), m_pointer(pointer) {
    priority = kPriority;
    name = "CanvasInputSystem";
}

void CanvasInputSystem::update(entt::registry& registry, float /*deltaTime*/) {
    if (!enabled) return;

    for (auto [entity, interactable] : registry.view<UIInteractable>().each()) {
        interactable.hovered = false;
        interactable.pressed = false;
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
        m_wasDown = false;
        syncWidgets(registry);
        if (m_pointer) *m_pointer = pointer;
        return;
    }

    const glm::vec2 screen = *m_screenSize;
    const glm::vec2 window = m_windowSize && m_windowSize->x > 0.0f && m_windowSize->y > 0.0f ? *m_windowSize : screen;
    const glm::vec2 pixel = input->mouseCaptured ? screen * 0.5f : input->mousePosition * (screen / window);
    const auto ray = mainCameraRay(registry, pixel, screen);
    const bool down = input->isMouseButtonDown(GLFW_MOUSE_BUTTON_LEFT);

    const auto hit = findPointerHit(registry, pixel, ray);
    entt::entity hovered = hit ? interactableFor(registry, hit->element) : entt::null;
    if (hovered != entt::null && !registry.get<UIInteractable>(hovered).enabled) hovered = entt::null;

    if (m_captured != entt::null &&
        (!registry.valid(m_captured) || !registry.all_of<UIInteractable>(m_captured) ||
         !registry.get<UIInteractable>(m_captured).enabled)) {
        m_captured = entt::null;
    }
    if (m_captured == entt::null && down && !m_wasDown) m_captured = hovered;

    if (m_captured != entt::null) {
        const entt::entity captured = m_captured;
        auto& interactable = registry.get<UIInteractable>(captured);
        interactable.hovered = hovered == captured;
        interactable.pressed = down;
        if (registry.all_of<UISlider>(captured) && dragSlider(registry, captured, pixel, ray)) {
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

    if (hit) {
        pointer.canvas = hit->canvas;
        pointer.position = hit->position;
    }
    pointer.overUI = (hit && hit->element != entt::null) || pointer.pressed != entt::null;
    if (m_pointer) *m_pointer = pointer;

    syncWidgets(registry);
}

} // namespace UI
} // namespace Shoonyakasha
