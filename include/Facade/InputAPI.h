//
// Facade/InputAPI.h - Python-friendly input polling and event callbacks
//
// No GLFW, no EnTT, no event templates in this header.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>

namespace Shoonyakasha {
namespace Facade {

class InputAPI {
public:
    InputAPI();
    ~InputAPI();

    // Non-copyable
    InputAPI(const InputAPI&) = delete;
    InputAPI& operator=(const InputAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Key / Mouse Polling (call inside onUpdate)
    // ═══════════════════════════════════════════════════════════

    /// Check if a key is currently held down (GLFW key codes).
    bool isKeyDown(int keyCode) const;

    /// Check if a mouse button is currently held down.
    bool isMouseButtonDown(int button) const;

    /// Get current mouse position in window coordinates.
    glm::vec2 getMousePosition() const;

    /// Get mouse movement since last frame.
    glm::vec2 getMouseDelta() const;

    /// Get scroll wheel delta since last frame.
    glm::vec2 getScrollDelta() const;

    /// Check if mouse is currently captured (FPS mode).
    bool isMouseCaptured() const;

    // ═══════════════════════════════════════════════════════════
    // Event Callbacks
    // ═══════════════════════════════════════════════════════════

    void setOnKeyEvent(std::function<void(int keyCode, bool pressed)> cb);
    void setOnMouseMove(std::function<void(float x, float y)> cb);
    void setOnMouseButton(std::function<void(int button, bool pressed)> cb);
    void setOnMouseScroll(std::function<void(float xOffset, float yOffset)> cb);

    // Impl is forward-declared (opaque) — public so EngineAPI can wire internals
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
