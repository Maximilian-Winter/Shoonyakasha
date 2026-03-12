# Python Quickstart

Render a 3D scene with camera, lighting, and image-based lighting in under 30 lines of Python.

> **Prerequisite:** Make sure you have completed the [Prerequisites](prerequisites.md) setup and built with `BUILD_PYTHON=ON`.

---

## Minimal Example

Create a file called `my_first_scene.py` and paste the following:

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="My First Scene",
    width=1600, height=900,
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="cubemaps_hdrs/environment.hdr",
)

def on_init():
    engine.create_camera(pos=(0, 5, 15), fov=60.0, speed=8.0)
    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95),
        intensity=3.0,
    )
    result = engine.load_gltf_scene("NewSponza_Main_glTF_003.gltf")
    print(f"Loaded {result.total_vertices} vertices, "
          f"{result.total_materials} materials")

engine.set_on_init(on_init)
engine.run()
```

Run it from the directory that contains your pipeline JSON and assets:

```
set PYTHONPATH=H:\cpp_dev\Shoonyakasha\python;%PYTHONPATH%
python my_first_scene.py
```

You should see a window open with the Sponza atrium rendered with PBR materials and image-based lighting. Use the mouse and keyboard to fly around.

---

## What Happened

Here is what each part of the example does.

### `sk.Engine(...)`

Creates the engine instance with window configuration and rendering setup. This is the main entry point for all Shoonyakasha Python programs.

- **`title`** — the window title bar text.
- **`width`, `height`** — window dimensions in pixels.
- **`pipeline_json_path`** — path to the JSON file that defines the entire render pipeline. This is **required**. The JSON document declares buffer layouts, render passes, shader bindings, and data sources. The engine compiles it to Vulkan resources automatically. See the [JSON Render Pipeline](../guides/json-render-pipeline.md) guide for details.
- **`hdr_environment_path`** — path to an HDR environment map for image-based lighting (IBL). When provided, the engine generates irradiance and prefiltered environment cubemaps for PBR shading. Omit this parameter if you do not need IBL.

### `set_on_init(callback)`

Registers a callback that runs once after Vulkan is initialized and the render pipeline is compiled. This is where you set up your scene: cameras, lights, and loaded models.

The callback runs inside the engine's initialization phase, so all GPU resources are ready and the ECS is active.

### `create_camera(pos, fov, speed, ...)`

Creates a camera entity with a built-in free-fly controller. Returns an entity handle (an `int`).

- **`pos`** — initial position as an `(x, y, z)` tuple.
- **`fov`** — field of view in degrees (default `60.0`).
- **`speed`** — movement speed for the built-in WASD controller (default `8.0`).
- Optional: `near_plane` (default `0.1`) and `far_plane` (default `1000.0`).

The camera controller is active immediately. Right-click and drag to look around, WASD to move.

### `create_directional_light(direction, color, intensity)`

Creates a directional (sun) light entity. Returns an entity handle.

- **`direction`** — light direction as an `(x, y, z)` tuple. Does not need to be normalized.
- **`color`** — RGB color as an `(r, g, b)` tuple (default white `(1, 1, 1)`).
- **`intensity`** — brightness multiplier (default `2.0`).

### `load_gltf_scene(path)`

Loads a glTF 2.0 (`.gltf` or `.glb`) model file into the ECS. All meshes, materials, textures, and the node hierarchy are imported. Returns a `GltfResult` object with:

- `success` — `True` if loading succeeded.
- `entities` — list of created entity handles.
- `total_vertices`, `total_indices`, `total_textures`, `total_materials` — statistics.
- `animation_clips` — list of `(name, duration)` tuples if the model contains skeletal animations.
- `skeleton_count` — number of skeletons loaded.

### `run()`

Starts the engine main loop. This call **blocks** until the user closes the window. Rendering, input processing, physics stepping, and callback dispatch all happen inside this loop.

---

## Adding Interactivity

Register an `on_update` callback to react to input every frame. The callback receives `dt`, the time in seconds since the last frame.

```python
light_count = 0

def on_update(dt):
    global light_count
    inp = engine.input

    # Spawn a point light at the camera position when 'L' is pressed
    if inp.is_key_down(76):  # L key
        cam_pos = engine.scene.get_position(engine.camera_entity)
        engine.create_point_light(
            pos=cam_pos,
            color=(1.0, 0.8, 0.6),
            intensity=5.0,
            range=20.0,
        )
        light_count += 1
        print(f"Point light #{light_count} at {cam_pos}")

engine.set_on_update(on_update)
```

Key points:

- **`engine.input`** — access the input polling API. Call `is_key_down(key_code)` with GLFW key codes (integers).
- **`engine.camera_entity`** — the entity handle of the active camera.
- **`engine.scene`** — access the scene/entity management API. Use it to query positions, modify transforms, toggle visibility, and more.
- **`create_point_light(pos, color, intensity, range)`** — creates a point light at runtime. You can spawn lights, load additional models, or modify entities from any callback.

You can also respond to individual key press events using `set_on_key_pressed`:

```python
def on_key_pressed(key_code):
    if key_code == 80:  # P key
        physics = engine.physics
        physics.enabled = not physics.enabled
        print(f"Physics {'enabled' if physics.enabled else 'paused'}")

engine.set_on_key_pressed(on_key_pressed)
```

The difference: `on_update` fires every frame (use `is_key_down` for held keys), while `on_key_pressed` fires once per key press (good for toggles).

---

## Default Controls

The built-in camera controller provides these controls out of the box:

| Input | Action |
|-------|--------|
| **W / A / S / D** | Move forward / left / backward / right |
| **Q / E** | Move down / up |
| **Right mouse button** | Hold to look around (FPS-style) |
| **Shift** | Sprint (move faster while held) |
| **Scroll wheel** | Zoom (adjust FOV) |

No setup is required for these controls. They are active as soon as `create_camera` is called.

---

## Available Callbacks

The `Engine` class supports the following callbacks, registered before or after calling `run()`:

| Method | Signature | When it fires |
|--------|-----------|---------------|
| `set_on_init` | `callback()` | Once, after Vulkan init and pipeline compilation |
| `set_on_post_init` | `callback()` | Once, after `on_init` completes |
| `set_on_update` | `callback(dt: float)` | Every frame, before rendering |
| `set_on_pre_render` | `callback(dt: float)` | Every frame, just before draw calls |
| `set_on_post_render` | `callback()` | Every frame, after draw calls |
| `set_on_key_pressed` | `callback(key_code: int)` | On each key press event |
| `set_on_resize` | `callback(width: int, height: int)` | When the window is resized |
| `set_on_cleanup` | `callback()` | Once, before shutdown |

---

## Data Types

Shoonyakasha's Python API uses plain Python types everywhere:

- **Vectors** are tuples: `(x, y, z)` for positions/directions, `(r, g, b)` for colors, `(x, y, z, w)` for vec4.
- **Entity handles** are `int` values. Compare with `sk.NULL_ENTITY` to check for invalid handles.
- **Matrices** are tuple-of-tuples (4x4, column-major) when returned by the API.

---

## Next Steps

Now that you have a scene rendering, explore these guides to go further:

- **[Loading Scenes](../guides/loading-scenes.md)** — glTF loading options, skinned meshes, multiple models
- **[Cameras and Controllers](../guides/cameras-and-controllers.md)** — camera types, orbit mode, custom controllers
- **[Lighting and IBL](../guides/lighting-and-ibl.md)** — point lights, spotlights, environment maps
- **[Physics](../guides/physics.md)** — add rigid bodies, collision shapes, and gravity
- **[Animation](../guides/animation.md)** — play skeletal animations from glTF models
- **[Engine API Reference](../api/python/engine.md)** — full Python API for the `Engine` class
