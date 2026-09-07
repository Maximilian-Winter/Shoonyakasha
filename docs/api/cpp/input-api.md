# InputAPI

Access: `engine.getInput()`. [Other language reference](../python/input.md).

Include `Facade/InputAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/InputAPI.h).

Keyboard and mouse polling/events, obtained through the engine. The current facade has no gamepad interface.

## Polling and callbacks

Poll held keys/buttons and mouse/scroll deltas during update. Key codes follow GLFW conventions; Python exports named values through `shoonyakasha.keys`. Positions are window coordinates; mouse and scroll deltas are per-frame values. C++ returns `glm::vec2`, Python a two-element tuple.

Register key events as `(key_code, pressed)`, mouse movement as `(x, y)`, button events as `(button, pressed)`, and scroll as `(x_offset, y_offset)`. Registration before `run()` is supported. Each setter replaces the active callback for its event.

The camera controller also processes input. See [cameras/controllers](../../guides/cameras-and-controllers.md) for its controls; this API does not expose a mouse-capture setter.

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
InputAPI();
~InputAPI();
bool isKeyDown(int keyCode) const;
bool isMouseButtonDown(int button) const;
glm::vec2 getMousePosition() const;
glm::vec2 getMouseDelta() const;
glm::vec2 getScrollDelta() const;
bool isMouseCaptured() const;
void setOnKeyEvent(std::function<void(int keyCode, bool pressed)> cb);
void setOnMouseMove(std::function<void(float x, float y)> cb);
void setOnMouseButton(std::function<void(int button, bool pressed)> cb);
void setOnMouseScroll(std::function<void(float xOffset, float yOffset)> cb);
```

<!-- END SOURCE API -->
