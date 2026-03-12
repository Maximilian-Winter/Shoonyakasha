# Python Example Walkthroughs

This page walks through the two Python example applications included in `python/examples/`. These examples demonstrate the full Python API for the Shoonyakasha engine and serve as templates for building your own applications.

---

## demo.py -- Full-Featured Sponza Rendering

**Source:** [`python/examples/demo.py`](../../python/examples/demo.py)

**Purpose:** A complete rendering demo that loads the Sponza architectural scene with PBR+IBL lighting, GPU particle simulation, dynamic point light creation, and physics toggling. This is the primary Python showcase of the engine.

### Engine Setup

The demo begins by creating an `sk.Engine` instance with full configuration:

```python
PARTICLE_COUNT = 50000

engine = sk.Engine(
    title="Python Demo",
    width=1920,
    height=1080,
    log_file="python_demo.log",
    hdr_environment_path="cubemaps_hdrs/kloofendal_28d_misty_8k.hdr",
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    render_graph_parameters={"particleCount": PARTICLE_COUNT},
)
```

Key points:
- `pipeline_json_path` selects the JSON render pipeline. The `pbr_ibl_pipeline_v3.json` pipeline includes a deferred PBR pass, IBL lighting, bloom, and a GPU particle compute pass.
- `hdr_environment_path` loads an HDR environment map for image-based lighting (IBL).
- `render_graph_parameters` injects compile-time constants into the render graph. Here, `particleCount` tells the particle SSBO how many particles to allocate.

### on_init -- Scene Construction

```python
def on_init():
    camera = engine.create_camera(
        pos=(0.0, 5.0, 15.0), fov=60.0, speed=8.0,
        near_plane=0.1, far_plane=500.0,
    )

    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95),
        intensity=3.0,
    )

    result = engine.load_gltf_scene("./NewSponza_Main_glTF_003.gltf")

    physics = engine.physics
    physics.gravity = (0.0, -9.81, 0.0)
```

The initialization callback sets up:
1. **Camera** -- positioned at `(0, 5, 15)` with 60-degree FOV and 8 units/sec movement speed. Near/far planes at 0.1 and 500.0.
2. **Directional light** -- warm white sunlight angled downward.
3. **glTF scene** -- loads the Intel Sponza model. The `GltfResult` reports entity count, vertex count, and texture count.
4. **Physics** -- sets standard Earth gravity. The physics world is available even though no rigid bodies exist yet.

### on_update -- Per-Frame Logic

```python
def on_update(dt):
    # FPS tracking (print every 5 seconds)
    fps_timer += dt
    fps_frames += 1
    if fps_timer >= 5.0:
        print(f"[FPS] {fps_frames / fps_timer:.1f}")

    # Dynamic light spawning with 'L' key
    inp = engine.input
    if inp.is_key_down(76):  # 'L'
        scene = engine.scene
        cam = engine.camera_entity
        pos = scene.get_position(cam)
        engine.create_point_light(pos=pos, color=(1.0, 0.8, 0.6),
                                  intensity=5.0, range=20.0)
```

The update callback runs every frame with the delta time `dt`. It demonstrates:
- **FPS tracking** -- accumulates frames and prints average FPS every 5 seconds.
- **Runtime entity creation** -- when the user holds `L`, the engine queries the camera's current world position via `scene.get_position()` and creates a new warm-toned point light at that location. This shows how entities can be created dynamically at runtime.

### on_pre_render -- Compute Shader Parameters

```python
def on_pre_render(dt):
    particle_time += dt

    engine.set_custom_float("particles.gravity", 1.5)
    engine.set_custom_uint("particles.count", PARTICLE_COUNT)
    engine.set_custom_float("particles.boundaryRadius", 15.0)
    engine.set_custom_float("particles.damping", 0.998)
    engine.set_custom_float("particles.spawnHeight", 0.5)

    engine.set_custom_vec4("particles.attractorPos", (0.0, 5.0, 0.0, 25.0))

    wind_angle = particle_time * 0.2
    engine.set_custom_vec4("particles.wind", (
        math.sin(wind_angle) * 0.4, 0.1,
        math.cos(wind_angle) * 0.4, 0.3,
    ))
```

The pre-render callback runs just before the frame is submitted to the GPU. It uses `set_custom_float`, `set_custom_uint`, and `set_custom_vec4` to write named parameters into the render graph's scene context. These values are automatically picked up by the particle compute shader via the JSON pipeline's dot-path bindings.

Key parameters:
- **gravity** -- downward pull strength (1.5).
- **boundaryRadius** -- particles beyond this distance are respawned.
- **damping** -- velocity decay per frame (0.998 = slow decay).
- **attractorPos** -- `(x, y, z, strength)` packed as vec4. The attractor pulls particles toward the atrium center.
- **wind** -- `(wx, wy, wz, turbulence)` packed as vec4. The wind direction rotates slowly over time using `sin`/`cos` of the elapsed time.

### on_key_pressed -- Physics Toggle

```python
def on_key_pressed(key_code):
    if key_code == 80:  # 'P'
        physics = engine.physics
        was_enabled = physics.enabled
        physics.enabled = not was_enabled
```

Pressing `P` toggles physics simulation on or off. This demonstrates the `PhysicsAPI.enabled` property.

### Callback Registration and Run

```python
engine.set_on_init(on_init)
engine.set_on_update(on_update)
engine.set_on_pre_render(on_pre_render)
engine.set_on_key_pressed(on_key_pressed)
engine.set_on_cleanup(on_cleanup)
engine.run()
```

All callbacks are registered on the engine before calling `engine.run()`, which enters the main loop and does not return until the window is closed.

### Key Takeaways

- **Custom uniforms for compute shaders** -- `set_custom_float/uint/vec4` lets Python drive GPU compute passes without any Vulkan code.
- **Dynamic entity creation** -- point lights (and any other entity) can be created at runtime during `on_update`.
- **Callback-driven architecture** -- the entire application is structured as a set of callbacks (`on_init`, `on_update`, `on_pre_render`, `on_key_pressed`, `on_cleanup`), with no main loop management required.
- **Render graph parameters** -- compile-time constants like `particleCount` are passed via `render_graph_parameters` at engine construction time.

---

## skinned_fox_demo.py -- Skeletal Animation

**Source:** [`python/examples/skinned_fox_demo.py`](../../python/examples/skinned_fox_demo.py)

**Purpose:** A skeletal animation demo that loads the Fox.glb model, plays animation clips, and demonstrates interactive clip switching, pause/resume, and speed control.

### Engine Setup

```python
engine = sk.Engine(
    title="Skinned Fox -- Python Demo",
    width=1280,
    height=720,
    log_file="skinned_fox_python.log",
    pipeline_json_path="skinned_pipeline.json",
    hdr_environment_path="cubemaps_hdrs/charolettenbrunn_park_4k.hdr",
)
```

The skinned animation pipeline requires a different JSON pipeline (`skinned_pipeline.json`) that includes vertex shader support for bone matrix transforms. No `render_graph_parameters` are needed since this demo does not use particles.

### on_init -- Loading a Skinned Model

```python
def on_init():
    engine.create_camera(
        pos=(0, 40, 200), fov=60.0, speed=50.0,
        near_plane=1.0, far_plane=2000.0,
    )

    engine.create_directional_light(
        direction=(-0.5, -1.0, -0.3),
        color=(1.0, 0.975, 0.95), intensity=3.0,
    )

    engine.create_point_light(
        pos=(3.0, 1.0, 2.0),
        color=(1.0, 0.85, 0.7), intensity=5.0, range=20.0,
    )

    result = engine.load_gltf_scene(
        "models/Fox.glb",
        load_textures=True, load_materials=True,
        create_entities=True, load_skins=True,
        load_animations=True, name_prefix="fox",
    )
```

Key points:
- **Camera placement** -- the Fox model is approximately 80 units tall, so the camera is positioned at `(0, 40, 200)` with high speed (50 units/sec) and a far plane of 2000 to accommodate the scale.
- **Two-light setup** -- a directional sunlight plus a warm golden point light for fill.
- **Skinned glTF loading** -- the critical flags are `load_skins=True` and `load_animations=True`. These instruct the glTF loader to parse skeleton hierarchies and animation clip data from the file.

### Animation Discovery and Auto-Play

```python
    for i, (name, dur) in enumerate(result.animation_clips):
        print(f"  [{i}] {name} ({dur:.2f}s)")

    scene = engine.scene
    for entity in result.entities:
        if scene.get_animation_clip_count(entity) > 0:
            animated_entities.append(entity)
            scene.set_animation_looping(entity, True)
            scene.play_animation(entity, 0)
```

After loading, the demo inspects `result.animation_clips` to list all available clips by name and duration (e.g., "Survey", "Walk", "Run"). It then iterates through all loaded entities, finds those with animation clips, enables looping, and auto-plays clip index 0.

### Interactivity -- Clip Switching and Speed Control

```python
def on_key_pressed(key):
    scene = engine.scene
    for entity in animated_entities:
        clip_count = scene.get_animation_clip_count(entity)

        if key == 52 and clip_count > 0:       # '4'
            scene.play_animation(entity, 0)
        elif key == 53 and clip_count > 1:     # '5'
            scene.play_animation(entity, 1)
        elif key == 54 and clip_count > 2:     # '6'
            scene.play_animation(entity, 2)

        elif key == 32:                        # Space
            if scene.is_animation_playing(entity):
                scene.stop_animation(entity)
            else:
                current = scene.get_current_animation_clip(entity)
                if current >= 0:
                    scene.play_animation(entity, current)

        elif key in (61, 334):                 # '+' / '='
            anim_speed = min(anim_speed * 1.5, 10.0)
            scene.set_animation_speed(entity, anim_speed)

        elif key in (45, 333):                 # '-'
            anim_speed = max(anim_speed / 1.5, 0.1)
            scene.set_animation_speed(entity, anim_speed)
```

Controls:
| Key | Action |
|-----|--------|
| `4` | Play clip 0 (typically "Survey") |
| `5` | Play clip 1 (typically "Walk") |
| `6` | Play clip 2 (typically "Run") |
| `Space` | Pause/resume the current animation |
| `+` / `=` | Speed up by 1.5x (max 10x) |
| `-` | Slow down by 1.5x (min 0.1x) |

### Minimal Main Loop

```python
engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key_pressed)
engine.run()
```

This demo only uses two callbacks -- `on_init` and `on_key_pressed`. No `on_update` or `on_pre_render` is needed because the skeletal animation system runs automatically each frame inside the engine.

### Key Takeaways

- **Animation clip discovery** -- `result.animation_clips` returns a list of `(name, duration)` tuples. `scene.get_animation_clip_count(entity)` tells you how many clips an entity has.
- **Playback control** -- `play_animation(entity, clip_index)`, `stop_animation(entity)`, `is_animation_playing(entity)`, and `get_current_animation_clip(entity)` provide full control over animation state.
- **Speed adjustment** -- `set_animation_speed(entity, speed)` scales playback rate. Values below 1.0 slow down, above 1.0 speed up.
- **Looping** -- `set_animation_looping(entity, True)` enables continuous looping; otherwise the clip plays once and stops.

---

## Running the Examples

Both examples must be run from the correct working directory so that asset paths (pipeline JSON, HDR maps, glTF models) resolve correctly.

**demo.py:**
```bash
cd examples/declarative_sponza_test
python ../../python/examples/demo.py
```

**skinned_fox_demo.py:**
```bash
cd examples/skinned_mesh_test
python ../../python/examples/skinned_fox_demo.py
```

Ensure that the engine is built with `-DBUILD_PYTHON=ON` and that `python/shoonyakasha/_shoonyakasha.pyd` exists. Add the `python/` directory to your `PYTHONPATH` or let the scripts' `sys.path.insert` handle it automatically.
