//
// Facade/InputAPIImpl.h - InputAPI::Impl definition (internal only)
//
// Shared between InputAPI.cpp and EngineAPI.cpp so that EngineAPI
// can wire the internal pointers during onInit().
//

#pragma once

#include "Facade/InputAPI.h"

// InputSystem.h's transitive includes need these first
#include <vulkan/vulkan.h>
#include "ECS/Systems.h"

#include "ECS/InputSystem.h"
#include "Core/EventSystem.h"

namespace Shoonyakasha {
namespace Facade {

struct InputAPI::Impl {
    ECS::StandaloneInputHandler* inputHandler = nullptr;
    EventDispatcher* dispatcher = nullptr;

    // User callbacks — stored here so event subscriptions can forward to them
    std::function<void(int, bool)>    onKeyEventCb;
    std::function<void(float, float)> onMouseMoveCb;
    std::function<void(int, bool)>    onMouseButtonCb;
    std::function<void(float, float)> onMouseScrollCb;

    // Wire from EngineAPI — Impl is a nested class so it can access private m_impl
    static void wire(InputAPI& api,
                     ECS::StandaloneInputHandler* handler,
                     EventDispatcher* dispatcher) {
        api.m_impl->inputHandler = handler;
        api.m_impl->dispatcher   = dispatcher;
        api.m_impl->subscribeEvents();
    }

    void subscribeEvents() {
        if (!dispatcher) return;

        dispatcher->subscribe<KeyEvent>([this](const KeyEvent& e) {
            if (onKeyEventCb) onKeyEventCb(e.keyCode, e.pressed);
        });

        dispatcher->subscribe<MouseMoveEvent>([this](const MouseMoveEvent& e) {
            if (onMouseMoveCb) onMouseMoveCb(static_cast<float>(e.x),
                                              static_cast<float>(e.y));
        });

        dispatcher->subscribe<MouseButtonEvent>([this](const MouseButtonEvent& e) {
            if (onMouseButtonCb) onMouseButtonCb(e.button, e.pressed);
        });

        dispatcher->subscribe<MouseScrollEvent>([this](const MouseScrollEvent& e) {
            if (onMouseScrollCb) onMouseScrollCb(static_cast<float>(e.xOffset),
                                                  static_cast<float>(e.yOffset));
        });
    }
};

} // namespace Facade
} // namespace Shoonyakasha
