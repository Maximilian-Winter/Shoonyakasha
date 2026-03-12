# Engine

> `shoonyakasha.Engine` -- Main entry point for the Shoonyakasha engine.

The `Engine` class creates a Vulkan-backed window, compiles a JSON render graph,
and runs the main loop. All scene setup, input handling, and rendering happen
through callbacks you register before calling `run()`.

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="My Game",
    width=1920,
    height=1080,
    pipeline_json_path="pipeline.json",
)
engine.set_on_init(lambda: engine.create_camera((0, 5, 15)))
engine.run()
```

---

## Constructor

### Engine(title, width, height, log_file, log_level, hdr_environment_path, pipeline_json_path, max_frames_in_flight, render_graph_parameters)

Create a new engine instance with the given configuration.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `title` | `str` | `"Shoonyakasha Application"` | Window title text. |
| `width` | `int` | `1600` | Initial window width in pixels. |
| `height` | `int` | `900` | Initial window height in pixels. |
| `log_file` | `str` | `"application.log"` | Path to the log output file. |
| `log_level` | `int` | `1` | Log verbosity: 0 = Debug, 1 = Info, 2 = Warning, 3 = Error. |
| `hdr_environment_path` | `str` | `""` | Path to an HDR environment map for image-based lighting. Empty string disables IBL. |
| `pipeline_json_path` | `str` | `""` | Path to the JSON render graph definition. Required for rendering. |
| `max_frames_in_flight` | `int` | `2` | Number of Vulkan frames in flight (double/triple buffering). |
| `render_graph_parameters` | `dict` or `None` | `None` | Dictionary of `str` to `int` pairs passed to the render graph for SSBO sizing, dispatch counts, etc. |

**Returns:** `Engine`

**Example:**

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Particle Demo",
    width=1920,
    height=1080,
    hdr_environment_path="environment.hdr",
    pipeline_json_path="pbr_pipeline.json",
    render_graph_parameters={"particleCount": 50000},
)
```

---

## Core Methods

### run()

Start the engine main loop. This call **blocks** until the window is closed.

The GIL is released during the loop so that Python callbacks registered with
`set_on_update`, `set_on_init`, etc. can be invoked from the engine thread.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `None`

**Example:**

```python
engine = sk.Engine(pipeline_json_path="pipeline.json")
engine.set_on_init(lambda: engine.create_camera((0, 2, 10)))
engine.run()  # blocks until window closes
```

---

## Lifecycle Callbacks

All callback setters accept a single callable. The engine holds a strong
reference to each callback to prevent garbage collection.

### set_on_init(callback)

Register a callback invoked once after the engine and Vulkan context are fully
initialized. Use this to create cameras, load scenes, and set up physics.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback()`. No arguments, no return value. |

**Returns:** `None`

**Example:**

```python
def on_init():
    engine.create_camera((0, 5, 15))
    engine.load_gltf_scene("scene.gltf")

engine.set_on_init(on_init)
```

---

### set_on_post_init(callback)

Register a callback invoked once after `on_init` and after the first frame has
been prepared. Useful for setup that depends on the render graph being fully
compiled.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback()`. |

**Returns:** `None`

**Example:**

```python
engine.set_on_post_init(lambda: print("Engine fully ready"))
```

---

### set_on_update(callback)

Register a per-frame update callback. Called once every frame with the delta
time. Use this for game logic, input handling, and entity manipulation.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(dt: float)`. `dt` is seconds since the previous frame. |

**Returns:** `None`

**Example:**

```python
def on_update(dt):
    if engine.input.is_key_down(87):  # W key
        cam = engine.camera_entity
        pos = engine.scene.get_position(cam)
        engine.scene.set_position(cam, (pos[0], pos[1], pos[2] - 5.0 * dt))

engine.set_on_update(on_update)
```

---

### set_on_pre_render(callback)

Register a callback invoked each frame just before GPU rendering begins.
Ideal for updating custom shader uniforms and compute parameters.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(dt: float)`. `dt` is seconds since the previous frame. |

**Returns:** `None`

**Example:**

```python
import math

time = 0.0

def on_pre_render(dt):
    global time
    time += dt
    engine.set_custom_float("wind.strength", math.sin(time) * 0.5)

engine.set_on_pre_render(on_pre_render)
```

---

### set_on_post_render(callback)

Register a callback invoked each frame after GPU rendering has completed.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback()`. No arguments. |

**Returns:** `None`

**Example:**

```python
engine.set_on_post_render(lambda: None)  # no-op placeholder
```

---

### set_on_key_pressed(callback)

Register a callback invoked when a key is pressed. Receives the GLFW key code
as an integer.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(key_code: int)`. `key_code` is a GLFW key constant. |

**Returns:** `None`

**Example:**

```python
def on_key_pressed(key_code):
    if key_code == 256:  # GLFW_KEY_ESCAPE
        print("Escape pressed")

engine.set_on_key_pressed(on_key_pressed)
```

---

### set_on_resize(callback)

Register a callback invoked when the window is resized.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback(width: int, height: int)`. New window dimensions in pixels. |

**Returns:** `None`

**Example:**

```python
def on_resize(width, height):
    print(f"Window resized to {width}x{height}")

engine.set_on_resize(on_resize)
```

---

### set_on_cleanup(callback)

Register a callback invoked once when the engine is shutting down, before
Vulkan resources are destroyed. Use this for final logging or state saving.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `callback` | `callable` | -- | Signature: `callback()`. No arguments. |

**Returns:** `None`

**Example:**

```python
engine.set_on_cleanup(lambda: print("Goodbye!"))
```

---

## Sub-API Properties

These read-only properties provide access to the engine's subsystem APIs.
The wrapper objects are created lazily on first access and cached for the
lifetime of the `Engine` instance.

### scene

Access the [Scene](scene.md) API for entity and component management.

**Type:** `Scene` (read-only property)

**Example:**

```python
scene = engine.scene
entity = scene.create_entity("my_cube")
scene.set_position(entity, (1.0, 2.0, 3.0))
```

---

### input

Access the [Input](input.md) API for keyboard, mouse, and scroll polling.

**Type:** `Input` (read-only property)

**Example:**

```python
inp = engine.input
if inp.is_key_down(32):  # spacebar
    print("Jump!")
```

---

### physics

Access the [Physics](physics.md) API for rigid body simulation.

**Type:** `Physics` (read-only property)

**Example:**

```python
phys = engine.physics
phys.gravity = (0.0, -9.81, 0.0)
phys.enabled = True
```

---

## Entity Helpers

Convenience methods on `Engine` for common entity creation tasks.

### create_camera(pos, fov, speed, near_plane, far_plane)

Create a camera entity and set it as the active main camera.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pos` | `tuple[float, float, float]` | -- | Initial position as `(x, y, z)`. Required. |
| `fov` | `float` | `60.0` | Vertical field of view in degrees. |
| `speed` | `float` | `8.0` | Camera movement speed (units per second). |
| `near_plane` | `float` | `0.1` | Near clipping plane distance. |
| `far_plane` | `float` | `1000.0` | Far clipping plane distance. |

**Returns:** `int` -- Entity handle for the new camera.

**Example:**

```python
camera = engine.create_camera(
    pos=(0.0, 5.0, 15.0),
    fov=60.0,
    speed=8.0,
    near_plane=0.1,
    far_plane=500.0,
)
```

---

### create_directional_light(direction, color, intensity)

Create a directional light entity (e.g., sunlight).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `direction` | `tuple[float, float, float]` | -- | Light direction as `(x, y, z)`. Required. |
| `color` | `tuple[float, float, float]` | `(1.0, 1.0, 1.0)` | Light color as `(r, g, b)`, each in `[0, 1]`. |
| `intensity` | `float` | `2.0` | Light intensity multiplier. |

**Returns:** `int` -- Entity handle for the new light.

**Example:**

```python
sun = engine.create_directional_light(
    direction=(-0.5, -1.0, -0.3),
    color=(1.0, 0.975, 0.95),
    intensity=3.0,
)
```

---

### create_point_light(pos, color, intensity, range)

Create a point light entity at a given position.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pos` | `tuple[float, float, float]` | -- | World position as `(x, y, z)`. Required. |
| `color` | `tuple[float, float, float]` | `(1.0, 1.0, 1.0)` | Light color as `(r, g, b)`. |
| `intensity` | `float` | `5.0` | Light intensity multiplier. |
| `range` | `float` | `15.0` | Maximum light range in world units. |

**Returns:** `int` -- Entity handle for the new light.

**Example:**

```python
lamp = engine.create_point_light(
    pos=(3.0, 4.0, 0.0),
    color=(1.0, 0.8, 0.6),
    intensity=5.0,
    range=20.0,
)
```

---

### camera_entity

Read-only property returning the entity handle of the current main camera.

**Type:** `int` (read-only property)

**Example:**

```python
cam = engine.camera_entity
pos = engine.scene.get_position(cam)
print(f"Camera at {pos}")
```

---

### delta_time

Read-only property returning the frame delta time in seconds.

**Type:** `float` (read-only property)

**Example:**

```python
def on_update(dt):
    # dt and engine.delta_time are the same value
    assert abs(dt - engine.delta_time) < 1e-6
```

---

### load_gltf_scene(path, **kwargs)

Load a glTF 2.0 scene from disk. Creates entities, meshes, materials, textures,
and optionally skeletons and animation clips.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `str` | -- | Path to a `.gltf` or `.glb` file. Required. |
| `load_textures` | `bool` | `True` | Load and upload textures to the GPU. |
| `load_materials` | `bool` | `True` | Parse and apply PBR materials. |
| `create_entities` | `bool` | `True` | Create ECS entities for each mesh node. |
| `load_skins` | `bool` | `True` | Load skeleton/skin data for skeletal animation. |
| `load_animations` | `bool` | `True` | Load animation clips from the file. |
| `flatten_hierarchy` | `bool` | `True` | Flatten the node hierarchy into world-space transforms. |
| `max_texture_size` | `int` | `0` | Maximum texture dimension. `0` means unlimited. |
| `generate_mipmaps` | `bool` | `True` | Generate mipmaps for loaded textures. |
| `srgb_albedo` | `bool` | `True` | Interpret albedo textures as sRGB. |
| `name_prefix` | `str` | `""` | Prefix added to all entity names from this file. |

**Returns:** [`GltfResult`](gltf-result.md) -- Object containing success status,
entity handles, and load statistics.

**Example:**

```python
result = engine.load_gltf_scene(
    "models/Sponza.gltf",
    load_animations=False,
    max_texture_size=2048,
)

if result.success:
    print(f"Loaded {len(result.entities)} entities")
    print(f"  Vertices:  {result.total_vertices}")
    print(f"  Textures:  {result.total_textures}")
    print(f"  Materials: {result.total_materials}")
else:
    print(f"Load failed: {result.error}")
```

---

## Custom Uniforms

These methods set values in the engine's scene-context uniform store, accessible
from shaders via dot-path keys defined in the JSON render graph. Use them in
`on_pre_render` to drive compute shaders, post-processing effects, or any
custom rendering logic.

### set_custom_float(key, value)

Set a custom `float` uniform.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key` | `str` | -- | Dot-path key (e.g., `"particles.gravity"`). |
| `value` | `float` | -- | The float value. |

**Returns:** `None`

**Example:**

```python
engine.set_custom_float("particles.gravity", 1.5)
```

---

### set_custom_vec2(key, value)

Set a custom `vec2` uniform.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key` | `str` | -- | Dot-path key. |
| `value` | `tuple[float, float]` | -- | Two-component vector as `(x, y)`. |

**Returns:** `None`

**Example:**

```python
engine.set_custom_vec2("screen.resolution", (1920.0, 1080.0))
```

---

### set_custom_vec3(key, value)

Set a custom `vec3` uniform.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key` | `str` | -- | Dot-path key. |
| `value` | `tuple[float, float, float]` | -- | Three-component vector as `(x, y, z)`. |

**Returns:** `None`

**Example:**

```python
engine.set_custom_vec3("fog.color", (0.7, 0.8, 0.9))
```

---

### set_custom_vec4(key, value)

Set a custom `vec4` uniform.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key` | `str` | -- | Dot-path key. |
| `value` | `tuple[float, float, float, float]` | -- | Four-component vector as `(x, y, z, w)`. |

**Returns:** `None`

**Example:**

```python
# (x, y, z) = position, w = strength
engine.set_custom_vec4("particles.attractorPos", (0.0, 5.0, 0.0, 25.0))
```

---

### set_custom_uint(key, value)

Set a custom `uint32` uniform.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `key` | `str` | -- | Dot-path key. |
| `value` | `int` | -- | Unsigned 32-bit integer value. |

**Returns:** `None`

**Example:**

```python
engine.set_custom_uint("particles.count", 50000)
```

---

## See Also

- [Constants](constants.md) -- Module-level constants for camera types, light types, etc.
- [GltfResult](gltf-result.md) -- Return type of `load_gltf_scene()`
- [Scene](scene.md) -- Entity and component management
- [Input](input.md) -- Keyboard and mouse input
- [Physics](physics.md) -- Rigid body physics
