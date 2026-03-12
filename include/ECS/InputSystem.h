//
// ECS/InputSystem.h - The Bridge Between World and Perception
//
// 輸入系統 - Input System
//
// This system bridges the event-driven window input with the
// continuous state needed for smooth camera control.
//
// Events are fleeting moments; state is the flow of time.
//

#pragma once

#include "CameraController.h"
#include "Core/EventSystem.h"
#include "Vulkan/VulkanWindow.h"

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// Input System - Event to State Transformer
// ═══════════════════════════════════════════════════════════════
//
// Subscribes to events and updates InputStateComponent.
// Priority 1: runs before all other systems.
//

class InputSystem : public ISystem {
public:
    InputSystem(EventDispatcher& dispatcher, VulkanWindow& window)
        : m_dispatcher(dispatcher)
        , m_window(window)
        , m_inputEntity(entt::null)
    {
        priority = 1;  // Run first

        // Subscribe to all input events
        m_dispatcher.subscribe<KeyEvent>([this](const KeyEvent& event) {
            onKeyEvent(event);
        });

        m_dispatcher.subscribe<MouseMoveEvent>([this](const MouseMoveEvent& event) {
            onMouseMove(event);
        });

        m_dispatcher.subscribe<MouseButtonEvent>([this](const MouseButtonEvent& event) {
            onMouseButton(event);
        });

        m_dispatcher.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& event) {
            onMouseScroll(event);
        });
    }

    void initialize(entt::registry& registry) override {
        // Create the singleton input state entity if it doesn't exist
        auto view = registry.view<InputStateComponent>();
        if (view.empty()) {
            m_inputEntity = registry.create();
            registry.emplace<InputStateComponent>(m_inputEntity);
            registry.emplace<NameComponent>(m_inputEntity, "InputState");
        } else {
            m_inputEntity = *view.begin();
        }
    }

    void update(entt::registry& registry, float deltaTime) override {
        if (!enabled) return;

        auto* input = registry.try_get<InputStateComponent>(m_inputEntity);
        if (!input) return;

        // Sync mouse capture state with window
        input->mouseCaptured = m_window.isMouseCaptured();

        // Process any pending events (they're already handled via callbacks)
        // This update is just for frame synchronization

        // Handle Escape key to toggle mouse capture
        // (This can be customized via CameraControllerComponent.keyCaptureMouse)
        if (m_toggleCaptureRequested) {
            m_window.setMouseCaptured(!m_window.isMouseCaptured());
            input->firstMouseCapture = true;
            m_toggleCaptureRequested = false;
        }
    }

    // ─────────────────────────────────────────────────────────────
    // Direct State Access (for systems that need immediate state)
    // ─────────────────────────────────────────────────────────────

    InputStateComponent* getInputState(entt::registry& registry) {
        return registry.try_get<InputStateComponent>(m_inputEntity);
    }

    // ─────────────────────────────────────────────────────────────
    // Mouse Capture Control
    // ─────────────────────────────────────────────────────────────

    void setMouseCaptured(bool captured) {
        m_window.setMouseCaptured(captured);
    }

    bool isMouseCaptured() const {
        return m_window.isMouseCaptured();
    }

    void toggleMouseCapture() {
        m_toggleCaptureRequested = true;
    }

private:
    EventDispatcher& m_dispatcher;
    VulkanWindow& m_window;
    entt::entity m_inputEntity;
    bool m_toggleCaptureRequested = false;

    // Temporary storage for events between frames
    InputStateComponent m_pendingState;

    void onKeyEvent(const KeyEvent& event) {
        if (event.keyCode >= 0 && event.keyCode <= GLFW_KEY_LAST) {
            m_pendingState.keys[event.keyCode] = event.pressed;

            // Handle escape to toggle mouse capture
            if (event.keyCode == GLFW_KEY_ESCAPE && event.pressed) {
                m_toggleCaptureRequested = true;
            }
        }
    }

    void onMouseMove(const MouseMoveEvent& event) {
        m_pendingState.mousePosition = glm::vec2(
            static_cast<float>(event.x),
            static_cast<float>(event.y)
        );
    }

    void onMouseButton(const MouseButtonEvent& event) {
        if (event.button >= 0 && event.button <= GLFW_MOUSE_BUTTON_LAST) {
            m_pendingState.mouseButtons[event.button] = event.pressed;
        }
    }

    void onMouseScroll(const MouseScrollEvent& event) {
        m_pendingState.scrollDelta += glm::vec2(
            static_cast<float>(event.xOffset),
            static_cast<float>(event.yOffset)
        );
    }

public:
    // Called by update to sync pending state to component
    void syncToComponent(InputStateComponent& component) {
        component.keys = m_pendingState.keys;
        component.mousePosition = m_pendingState.mousePosition;
        component.mouseButtons = m_pendingState.mouseButtons;
        component.scrollDelta = m_pendingState.scrollDelta;

        // Reset scroll delta after reading
        m_pendingState.scrollDelta = glm::vec2(0.0f);
    }
};

// ═══════════════════════════════════════════════════════════════
// Standalone Input Handler - For Non-ECS Usage
// ═══════════════════════════════════════════════════════════════
//
// If you're not using the full ECS system, this class provides
// a simple way to integrate input handling with the camera controller.
//

class StandaloneInputHandler {
public:
    StandaloneInputHandler(EventDispatcher& dispatcher, VulkanWindow& window)
        : m_window(window)
    {
        dispatcher.subscribe<KeyEvent>([this](const KeyEvent& event) {
            if (event.keyCode >= 0 && event.keyCode <= GLFW_KEY_LAST) {
                m_inputState.keys[event.keyCode] = event.pressed;

                // Toggle mouse capture on Escape
                if (event.keyCode == GLFW_KEY_ESCAPE && event.pressed) {
                    m_window.setMouseCaptured(!m_window.isMouseCaptured());
                    m_inputState.firstMouseCapture = true;
                }
            }
        });

        dispatcher.subscribe<MouseMoveEvent>([this](const MouseMoveEvent& event) {
            m_inputState.mousePosition = glm::vec2(
                static_cast<float>(event.x),
                static_cast<float>(event.y)
            );
        });

        dispatcher.subscribe<MouseButtonEvent>([this](const MouseButtonEvent& event) {
            if (event.button >= 0 && event.button <= GLFW_MOUSE_BUTTON_LAST) {
                m_inputState.mouseButtons[event.button] = event.pressed;
            }
        });

        dispatcher.subscribe<MouseScrollEvent>([this](const MouseScrollEvent& event) {
            m_inputState.scrollDelta += glm::vec2(
                static_cast<float>(event.xOffset),
                static_cast<float>(event.yOffset)
            );
        });
    }

    // Call at the beginning of each frame before updating camera
    void beginFrame() {
        m_inputState.mouseCaptured = m_window.isMouseCaptured();
        m_inputState.beginFrame();
    }

    // Call at the end of each frame
    void endFrame() {
        m_inputState.scrollDelta = glm::vec2(0.0f);
    }

    InputStateComponent& getInputState() { return m_inputState; }
    const InputStateComponent& getInputState() const { return m_inputState; }

private:
    InputStateComponent m_inputState;
    VulkanWindow& m_window;
};

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;
