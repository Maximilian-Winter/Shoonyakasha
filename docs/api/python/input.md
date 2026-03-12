# Input

> `shoonyakasha.Input` -- Keyboard, mouse, and scroll input.

The `Input` class provides both polling-based and event-driven access to
keyboard and mouse state. Accessed via `engine.input` -- do not construct
directly.

Polling methods are best used inside `on_update(dt)` where you need to know
the current state of a key or button every frame. Event callbacks fire once
per state change and are useful for discrete actions (toggling a menu,
firing a weapon, etc.).

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_update(dt):
    inp = engine.input
    if inp.is_key_down(87):  # W key
        print("Moving forward")

engine.set_on_update(on_update)
engine.set_on_init(lambda: engine.create_camera((0, 5, 15)))
engine.run()
```

> **GLFW Key Codes:** Input uses GLFW integer key codes. Common values:
>
> | Key | Code |
> |-----|------|
> | ESC | `256` |
> | W | `87` |
> | A | `65` |
> | S | `83` |
> | D | `68` |
> | SPACE | `32` |
> | LEFT_SHIFT | `340` |
> | LEFT_CTRL | `341` |
> | TAB | `258` |
> | ENTER | `257` |
>
> Mouse buttons: 0 = left, 1 = right, 2 = middle.

---

## Polling Methods

### is_key_down(key_code)

Check if a key is currently held down.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key_code` | `int` | -- | GLFW key code. |

**Returns:** `bool` -- `True` if the key is currently pressed.

**Example:**

```python
def on_update(dt):
    if engine.input.is_key_down(256):  # ESC
        print("Escape held")
```

---

### is_mouse_button_down(button)

Check if a mouse button is currently held down.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `button` | `int` | -- | Mouse button index (0 = left, 1 = right, 2 = middle). |

**Returns:** `bool` -- `True` if the button is currently pressed.

**Example:**

```python
def on_update(dt):
    if engine.input.is_mouse_button_down(0):  # left click held
        print("Firing!")
```

---

### get_mouse_position()

Get the current mouse cursor position in window coordinates.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `tuple[float, float]` -- Cursor position as `(x, y)` in pixels from the top-left corner.

**Example:**

```python
x, y = engine.input.get_mouse_position()
print(f"Mouse at ({x:.0f}, {y:.0f})")
```

---

### get_mouse_delta()

Get the mouse movement since the last frame.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `tuple[float, float]` -- Movement as `(dx, dy)` in pixels.

**Example:**

```python
dx, dy = engine.input.get_mouse_delta()
if abs(dx) > 0.1 or abs(dy) > 0.1:
    print(f"Mouse moved ({dx:.1f}, {dy:.1f})")
```

---

### get_scroll_delta()

Get the scroll wheel delta since the last frame.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `tuple[float, float]` -- Scroll offset as `(dx, dy)`. Vertical scroll is typically in `dy`.

**Example:**

```python
_, scroll_y = engine.input.get_scroll_delta()
if scroll_y != 0.0:
    zoom_level += scroll_y * 0.1
```

---

### is_mouse_captured()

Check whether the mouse cursor is captured (hidden and locked to the window
center). This is typically active when the engine is in FPS camera mode.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `bool` -- `True` if the mouse is captured (FPS mode active).

**Example:**

```python
if engine.input.is_mouse_captured():
    # Use mouse delta for camera look
    dx, dy = engine.input.get_mouse_delta()
```

---

## Event Callbacks

Event callbacks fire once per input event, independent of the frame loop.
They are useful for discrete actions that should not repeat every frame.

### set_on_key_event(callback)

Set a callback invoked whenever a key is pressed or released.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(key_code: int, pressed: bool)`. `key_code` is the GLFW key code. `pressed` is `True` on press, `False` on release. |

**Returns:** `None`

**Example:**

```python
def on_key_event(key_code, pressed):
    if key_code == 32 and pressed:  # SPACE pressed
        print("Jump!")
    if key_code == 256 and pressed:  # ESC pressed
        print("Menu toggle")

engine.input.set_on_key_event(on_key_event)
```

---

### set_on_mouse_move(callback)

Set a callback invoked whenever the mouse cursor moves.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(x: float, y: float)`. `x, y` are the new cursor position in window coordinates. |

**Returns:** `None`

**Example:**

```python
def on_mouse_move(x, y):
    print(f"Cursor at ({x:.0f}, {y:.0f})")

engine.input.set_on_mouse_move(on_mouse_move)
```

---

### set_on_mouse_button(callback)

Set a callback invoked whenever a mouse button is pressed or released.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(button: int, pressed: bool)`. `button` is the mouse button index (0 = left, 1 = right, 2 = middle). `pressed` is `True` on press, `False` on release. |

**Returns:** `None`

**Example:**

```python
def on_mouse_button(button, pressed):
    if button == 0 and pressed:
        print("Left click!")

engine.input.set_on_mouse_button(on_mouse_button)
```

---

### set_on_mouse_scroll(callback)

Set a callback invoked whenever the scroll wheel is moved.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(x_offset: float, y_offset: float)`. Vertical scrolling is typically in `y_offset`. |

**Returns:** `None`

**Example:**

```python
def on_mouse_scroll(x_offset, y_offset):
    if y_offset > 0:
        print("Scroll up")
    elif y_offset < 0:
        print("Scroll down")

engine.input.set_on_mouse_scroll(on_mouse_scroll)
```

---

## Patterns

### WASD Movement

Use polling in `on_update` for smooth, continuous movement:

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_update(dt):
    inp = engine.input
    scene = engine.scene
    cam = engine.camera_entity
    speed = 8.0

    fwd = scene.get_forward(cam)
    right = scene.get_right(cam)
    pos = list(scene.get_position(cam))

    # Forward / backward
    if inp.is_key_down(87):  # W
        pos[0] += fwd[0] * speed * dt
        pos[1] += fwd[1] * speed * dt
        pos[2] += fwd[2] * speed * dt
    if inp.is_key_down(83):  # S
        pos[0] -= fwd[0] * speed * dt
        pos[1] -= fwd[1] * speed * dt
        pos[2] -= fwd[2] * speed * dt

    # Strafe left / right
    if inp.is_key_down(65):  # A
        pos[0] -= right[0] * speed * dt
        pos[1] -= right[1] * speed * dt
        pos[2] -= right[2] * speed * dt
    if inp.is_key_down(68):  # D
        pos[0] += right[0] * speed * dt
        pos[1] += right[1] * speed * dt
        pos[2] += right[2] * speed * dt

    # Sprint
    if inp.is_key_down(340):  # LEFT_SHIFT
        speed *= 2.0

    scene.set_position(cam, tuple(pos))

engine.set_on_update(on_update)
engine.set_on_init(lambda: engine.create_camera((0, 5, 15)))
engine.run()
```

### Mouse Look

Use mouse delta for FPS-style camera rotation:

```python
import math

yaw = 0.0
pitch = 0.0

def on_update(dt):
    global yaw, pitch
    inp = engine.input

    if inp.is_mouse_captured():
        dx, dy = inp.get_mouse_delta()
        sensitivity = 0.002

        yaw -= dx * sensitivity
        pitch -= dy * sensitivity
        pitch = max(-math.pi / 2.0 + 0.01,
                    min(math.pi / 2.0 - 0.01, pitch))

        cam = engine.camera_entity
        engine.scene.set_rotation(cam, (pitch, yaw, 0.0))

engine.set_on_update(on_update)
```

### Polling vs Event Callbacks

| Approach | When to use | Example |
|----------|-------------|---------|
| **Polling** (`is_key_down`) | Continuous actions checked every frame in `on_update`. | Movement, holding to charge, camera look. |
| **Events** (`set_on_key_event`) | Discrete, one-shot actions that should fire once. | Toggle menu, fire weapon, place block. |

Both approaches can be used together. Polling is ideal inside `on_update`
for smooth, frame-rate-independent movement. Events are cleaner for actions
that should not repeat while a key is held.

---

## See Also

- [Engine](engine.md) -- Main engine entry point (parent of `input`)
- [Scene](scene.md) -- Entity and component management
- [Physics](physics.md) -- Rigid body physics
- [Constants](constants.md) -- Module-level constants
