//
// Facade/InputAPI.cpp - Delegates to StandaloneInputHandler + EventDispatcher
//

#include "InputAPIImpl.h"  // Impl definition shared with EngineAPI.cpp

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

InputAPI::InputAPI()
    : m_impl(std::make_unique<Impl>())
{}

InputAPI::~InputAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Key / Mouse Polling
// ═══════════════════════════════════════════════════════════════

bool InputAPI::isKeyDown(int keyCode) const {
    if (!m_impl->inputHandler) return false;
    return m_impl->inputHandler->getInputState().isKeyDown(keyCode);
}

bool InputAPI::isMouseButtonDown(int button) const {
    if (!m_impl->inputHandler) return false;
    return m_impl->inputHandler->getInputState().isMouseButtonDown(button);
}

glm::vec2 InputAPI::getMousePosition() const {
    if (!m_impl->inputHandler) return glm::vec2(0.f);
    return m_impl->inputHandler->getInputState().mousePosition;
}

glm::vec2 InputAPI::getMouseDelta() const {
    if (!m_impl->inputHandler) return glm::vec2(0.f);
    return m_impl->inputHandler->getInputState().mouseDelta;
}

glm::vec2 InputAPI::getScrollDelta() const {
    if (!m_impl->inputHandler) return glm::vec2(0.f);
    return m_impl->inputHandler->getInputState().scrollDelta;
}

bool InputAPI::isMouseCaptured() const {
    if (!m_impl->inputHandler) return false;
    return m_impl->inputHandler->getInputState().mouseCaptured;
}

// ═══════════════════════════════════════════════════════════════
// Event Callbacks
// ═══════════════════════════════════════════════════════════════

void InputAPI::setOnKeyEvent(std::function<void(int, bool)> cb) {
    m_impl->onKeyEventCb = std::move(cb);
}

void InputAPI::setOnMouseMove(std::function<void(float, float)> cb) {
    m_impl->onMouseMoveCb = std::move(cb);
}

void InputAPI::setOnMouseButton(std::function<void(int, bool)> cb) {
    m_impl->onMouseButtonCb = std::move(cb);
}

void InputAPI::setOnMouseScroll(std::function<void(float, float)> cb) {
    m_impl->onMouseScrollCb = std::move(cb);
}

} // namespace Facade
} // namespace Shoonyakasha
