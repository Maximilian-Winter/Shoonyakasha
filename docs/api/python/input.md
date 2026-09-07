# Input

Access: `engine.input`. [Other language reference](../cpp/input-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Keyboard and mouse polling/events, obtained through the engine. The current facade has no gamepad interface.

## Polling and callbacks

Poll held keys/buttons and mouse/scroll deltas during update. Key codes follow GLFW conventions; Python exports named values through `shoonyakasha.keys`. Positions are window coordinates; mouse and scroll deltas are per-frame values. C++ returns `glm::vec2`, Python a two-element tuple.

Register key events as `(key_code, pressed)`, mouse movement as `(x, y)`, button events as `(button, pressed)`, and scroll as `(x_offset, y_offset)`. Registration before `run()` is supported. Each setter replaces the active callback for its event.

The camera controller also processes input. See [cameras/controllers](../../guides/cameras-and-controllers.md) for its controls; this API does not expose a mouse-capture setter.

Python callback exceptions are printed by the bridge. Native C++ exceptions translated by Cython can still propagate from direct API calls.

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `is_key_down(key_code)` | bool | Check if a key is held down (GLFW key codes). |
| `is_mouse_button_down(button)` | bool | Check if a mouse button is held down. |
| `get_mouse_position()` | 2-tuple | Get mouse position as (x, y) tuple. |
| `get_mouse_delta()` | 2-tuple | Get mouse movement since last frame as (dx, dy) tuple. |
| `get_scroll_delta()` | 2-tuple | Get scroll wheel delta as (dx, dy) tuple. |
| `is_mouse_captured()` | bool | Check if mouse is captured (FPS mode). |
| `set_on_key_event(callback)` | None | Set key event callback: callback(key_code: int, pressed: bool). |
| `set_on_mouse_move(callback)` | None | Set mouse move callback: callback(x: float, y: float). |
| `set_on_mouse_button(callback)` | None | Set mouse button callback: callback(button: int, pressed: bool). |
| `set_on_mouse_scroll(callback)` | None | Set mouse scroll callback: callback(x_offset: float, y_offset: float). |

<!-- END SOURCE API -->
