# InputAPI

`Shoonyakasha::Facade::InputAPI` -- keyboard and mouse polling, plus event callbacks.

> **Facade layer** -- this is the same API that Python wraps via Cython. All GLFW/internal event system details are hidden behind PIMPL. For the inheritance-based alternative, see [ApplicationBase](application-base.md).

**Header:** `#include "Facade/InputAPI.h"`
**Namespace:** `Shoonyakasha::Facade`

**See also:** [Python Input API](../python/input.md)

---

## Obtaining an InputAPI

You do not construct `InputAPI` directly. Access it through `EngineAPI::getInput()`:

```cpp
auto& input = engine.getInput();
if (input.isKeyDown(GLFW_KEY_W)) {
    // move forward
}
```

The `InputAPI` is valid for the lifetime of the `EngineAPI`.

---

## Key / Mouse Polling

Call these inside `onUpdate` to check current input state.

### isKeyDown

```cpp
bool isKeyDown(int keyCode) const;
```

Check if a key is currently held down.

| Parameter | Type | Description |
|-----------|------|-------------|
| `keyCode` | `int` | GLFW key code (e.g. `GLFW_KEY_W`, `GLFW_KEY_SPACE`, `GLFW_KEY_ESCAPE`). |

**Returns:** `bool` -- `true` if the key is currently pressed.

### isMouseButtonDown

```cpp
bool isMouseButtonDown(int button) const;
```

Check if a mouse button is currently held down.

| Parameter | Type | Description |
|-----------|------|-------------|
| `button` | `int` | GLFW mouse button code (e.g. `GLFW_MOUSE_BUTTON_LEFT`, `GLFW_MOUSE_BUTTON_RIGHT`). |

**Returns:** `bool` -- `true` if the button is currently pressed.

### getMousePosition

```cpp
glm::vec2 getMousePosition() const;
```

Get the current mouse cursor position in window coordinates.

**Returns:** `glm::vec2` -- `(x, y)` position in pixels from the top-left corner.

### getMouseDelta

```cpp
glm::vec2 getMouseDelta() const;
```

Get the mouse movement since the last frame.

**Returns:** `glm::vec2` -- `(dx, dy)` in pixels.

### getScrollDelta

```cpp
glm::vec2 getScrollDelta() const;
```

Get the scroll wheel delta since the last frame.

**Returns:** `glm::vec2` -- `(xOffset, yOffset)`. Typically `yOffset` is the vertical scroll amount.

### isMouseCaptured

```cpp
bool isMouseCaptured() const;
```

Check if the mouse cursor is currently captured (FPS-style camera mode where the cursor is hidden and locked to the window center).

**Returns:** `bool` -- `true` if mouse is captured.

---

## Event Callbacks

Register callbacks for specific input events. Each setter replaces any previously registered callback for that slot.

### setOnKeyEvent

```cpp
void setOnKeyEvent(std::function<void(int keyCode, bool pressed)> cb);
```

Set a callback for keyboard events.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `std::function<void(int, bool)>` | Called with the GLFW key code and `true` for press, `false` for release. |

### setOnMouseMove

```cpp
void setOnMouseMove(std::function<void(float x, float y)> cb);
```

Set a callback for mouse movement.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `std::function<void(float, float)>` | Called with the new cursor position `(x, y)` in window coordinates. |

### setOnMouseButton

```cpp
void setOnMouseButton(std::function<void(int button, bool pressed)> cb);
```

Set a callback for mouse button events.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `std::function<void(int, bool)>` | Called with the GLFW button code and `true` for press, `false` for release. |

### setOnMouseScroll

```cpp
void setOnMouseScroll(std::function<void(float xOffset, float yOffset)> cb);
```

Set a callback for scroll wheel events.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `std::function<void(float, float)>` | Called with the scroll offsets. |

---

## GLFW Key Code Reference

Key codes are GLFW constants. Common values:

| Constant | Value | Key |
|----------|-------|-----|
| `GLFW_KEY_W` | `87` | W |
| `GLFW_KEY_A` | `65` | A |
| `GLFW_KEY_S` | `83` | S |
| `GLFW_KEY_D` | `68` | D |
| `GLFW_KEY_SPACE` | `32` | Space |
| `GLFW_KEY_LEFT_SHIFT` | `340` | Left Shift |
| `GLFW_KEY_ESCAPE` | `256` | Escape |
| `GLFW_KEY_F1` .. `GLFW_KEY_F12` | `290`..`301` | F1-F12 |
| `GLFW_MOUSE_BUTTON_LEFT` | `0` | Left click |
| `GLFW_MOUSE_BUTTON_RIGHT` | `1` | Right click |
| `GLFW_MOUSE_BUTTON_MIDDLE` | `2` | Middle click |

For the full list, see the [GLFW input reference](https://www.glfw.org/docs/latest/group__keys.html).
