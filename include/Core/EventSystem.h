//
// Created by maxim on 28.06.2024.
//

#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <any>
#include <memory>
#include <typeindex>

namespace Shoonyakasha {

class Event {
public:
    virtual ~Event() = default;
};

class EventDispatcher {
public:
    template<typename T>
    using EventCallback = std::function<void(const T&)>;

    template<typename T>
    void subscribe(EventCallback<T> callback) {
        auto& callbacks = m_callbacks[typeid(T)];
        callbacks.push_back([callback](const std::any& event) {
            callback(std::any_cast<const T&>(event));
        });
    }

    template<typename T>
    void publish(const T& event) {
        auto it = m_callbacks.find(typeid(T));
        if (it != m_callbacks.end()) {
            for (const auto& callback : it->second) {
                callback(event);
            }
        }
    }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> m_callbacks;
};


// Specific event types
struct WindowResizeEvent : public Event {
    int width;
    int height;
    WindowResizeEvent(int w, int h) : width(w), height(h) {}
};

struct KeyEvent : public Event {
    int keyCode;
    bool pressed;
    KeyEvent(int code, bool isPressed) : keyCode(code), pressed(isPressed) {}
};

struct MouseMoveEvent : public Event {
    int x;
    int y;
    MouseMoveEvent(int xPos, int yPos) : x(xPos), y(yPos) {}
};
// Additional event types
struct MouseButtonEvent : public Event {
    int button;
    bool pressed;
    MouseButtonEvent(int b, bool p) : button(b), pressed(p) {}
};

struct MouseScrollEvent : public Event {
    double xOffset;
    double yOffset;
    MouseScrollEvent(double x, double y) : xOffset(x), yOffset(y) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::Event;
using Shoonyakasha::EventDispatcher;
using Shoonyakasha::WindowResizeEvent;
using Shoonyakasha::KeyEvent;
using Shoonyakasha::MouseMoveEvent;
using Shoonyakasha::MouseButtonEvent;
using Shoonyakasha::MouseScrollEvent;



