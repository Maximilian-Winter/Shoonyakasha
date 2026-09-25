# Engine

Access: `sk.Engine(...)`. [Other language reference](../cpp/engine-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Owns the application and provides callbacks and convenience constructors. `run()` blocks until the window closes. Keep sub-API access and scene setup in the engine's lifetime.

## Initialization and callbacks

| Callback | Arguments | When it runs |
|---|---|---|
| Init | none | Vulkan, assets, and ECS exist; before graph compilation |
| Post-init | none | After graph compilation and event setup |
| Update | `dt` in seconds | Application update, before standard buffer upload |
| Pre-render | `dt` in seconds | After standard buffers update; facade also updates skeletal animation here |
| Post-render | none | After presentation |
| Key pressed | integer key code | Key-press event |
| Resize | width, height | Window resize event |
| Cleanup | none | Application shutdown hook |

Input can be accessed immediately and input callbacks registered before `run`. Scene and ECS access require initialization. Physics exists before `run`, but native setters are no-ops until it is wired; configure it in init.

Use update for custom values that must enter the current frame's automatic buffers. Init is the right place for scene loading; post-init is the first hook with a compiled render graph.

## Configuration and results

C++ configuration is `EngineConfig` in [FacadeTypes.h](../../../include/Facade/FacadeTypes.h). Defaults: 1600×900, title `Shoonyakasha Application`, log file `application.log`, log level 1 (Info), two frames in flight, empty environment and pipeline paths. A pipeline path is required to run. Log levels are 0 Debug, 1 Info, 2 Warning, 3 Error. Render graph parameters are string→unsigned-integer values for allocation/count configuration.

Python exposes the constructor arguments listed below. It does **not** expose C++ `enableValidation`; the C++ default is true with a warning/fallback when the layer is unavailable.

Creation helpers return entity handles. glTF loading returns a result whose `success` and `error` must be checked. Capture/start/stop return success booleans. Custom-value setters publish values under `scene.custom.<key>`; pass the key without that prefix.

See [loading scenes](../../guides/loading-scenes.md), [capture](../../guides/frame-capture.md), [sprites/UI/text](../../guides/sprites-ui-text.md), and [custom uniforms](../../guides/custom-shader-uniforms.md).

Python callback exceptions are printed by the bridge. Native C++ exceptions translated by Cython can still propagate from direct API calls.

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `Engine(title="Shoonyakasha Application", width=1600, height=900, log_file="application.log", log_level=1, hdr_environment_path="", pipeline_json_path="", max_frames_in_flight=2, render_graph_parameters=None)` | Engine | Create engine with configuration. |
| `run()` | None | Run the engine. Blocks until the window is closed. |
| `set_on_init(callback)` | None | Set initialization callback: callback(). |
| `set_on_post_init(callback)` | None | Set post-initialization callback: callback(). |
| `set_on_update(callback)` | None | Set per-frame update callback: callback(dt: float). |
| `set_on_pre_render(callback)` | None | Set pre-render callback: callback(dt: float). |
| `set_on_post_render(callback)` | None | Set post-render callback: callback(). |
| `set_on_key_pressed(callback)` | None | Set key-press callback: callback(key_code: int). |
| `set_on_resize(callback)` | None | Set window resize callback: callback(width: int, height: int). |
| `set_on_cleanup(callback)` | None | Set cleanup callback: callback(). |
| `scene (read-only property)` | Scene | Access scene/entity management API. |
| `input (read-only property)` | Input | Access input polling/events API. |
| `physics (read-only property)` | Physics | Access physics simulation API. |
| `ecs (read-only property)` | Ecs | Access low-level ECS API (custom components/systems). |
| `create_camera(pos, fov=60.0, speed=8.0, near_plane=0.1, far_plane=1000.0)` | int | Create a camera entity. |
| `capture_screenshot(path)` | bool | Write the last presented frame to disk. |
| `start_recording(path, fps=30, quality=18, codec="libx264", ffmpeg_path="")` | bool | Record every presented frame to a video file. |
| `stop_recording()` | bool | Finish the recording and finalise the file. |
| `is_recording (read-only property)` | bool | Whether a recording is in progress. |
| `recorded_frame_count (read-only property)` | int | Frames written to the current or most recent recording. |
| `load_gltf_scene(path, **kwargs)` | GltfResult | Load a glTF scene. |
| `create_directional_light(direction, color=(1.0, 1.0, 1.0), intensity=2.0)` | int | Create a directional light entity. |
| `create_point_light(pos, color=(1.0, 1.0, 1.0), intensity=5.0, range=15.0)` | int | Create a point light entity. |
| `create_sprite(world_pos, texture_path, size=(1.0, 1.0), tint=(1.0, 1.0, 1.0, 1.0))` | int | Create a world-space sprite (billboard quad in 3D world coordinates). |
| `create_ui_panel(anchor, offset_pixels, size_pixels, texture_path="", color=(1.0, 1.0, 1.0, 1.0))` | int | Create a screen-space UI panel anchored to a viewport corner/edge/center. |
| `create_text(text, anchor, offset_pixels, font_path, font_size=24.0, color=(1.0, 1.0, 1.0, 1.0))` | int | Create a screen-space text label anchored to a viewport corner/edge/center. |
| `camera_entity (read-only property)` | int | Get the camera entity handle. |
| `delta_time (read-only property)` | float | Get frame delta time in seconds. |
| `set_custom_float(key, value)` | None | Set custom float for shader uniforms (dot-path key). |
| `set_custom_vec2(key, value)` | None | Set custom vec2 for shader uniforms. |
| `set_custom_vec3(key, value)` | None | Set custom vec3 for shader uniforms. |
| `set_custom_vec4(key, value)` | None | Set custom vec4 for shader uniforms. |
| `set_custom_mat4(key, value)` | None | Set custom mat4 for shader uniforms. |
| `set_custom_uint(key, value)` | None | Set custom uint for shader uniforms. |
| `set_sun_shadows(cascades=4, max_distance=60.0, split_lambda=0.75, resolution=2048, caster_extension=50.0)` | None | Configure the sun's shadow cascades. |
| `get_sun_shadow_cascade(index)` | 4×4 tuple (column-major) | World-to-light-clip matrix of a sun cascade this frame, as four columns. |
| `set_pass_enabled(pass_name, enabled)` | bool | Turn a pipeline pass on or off from the next frame. |
| `is_pass_enabled(pass_name)` | bool | Whether a pipeline pass is enabled; False if there is no such pass. |

<!-- END SOURCE API -->
