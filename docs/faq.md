# Frequently Asked Questions

Answers to common questions, organized by category.

---

## Setup

### How do I build the engine?

Use CLion with the MSVC toolchain, or open the project in Visual Studio. Load `CMakeLists.txt` as the project root. Do not build from the command line unless you have run `vcvars64.bat` first -- the MSVC environment variables will not be set otherwise.

See [Prerequisites](getting-started/prerequisites.md) for full setup instructions.

### How do I use the Python bindings?

Build the engine with the CMake option `BUILD_PYTHON=ON`. Then add the `python/` directory to your `PYTHONPATH`. After that you can simply `import shoonyakasha` -- no `pip install` or wheel packaging is required.

See [Python Quickstart](getting-started/python-quickstart.md) for a walkthrough.

### What Vulkan SDK version do I need?

The engine requires the LunarG Vulkan SDK. Any recent version (1.3 or later) should work.

### How do I verify everything works?

Build with `BUILD_TESTS=ON` and run the test suite. There are 582 tests in total (518 core + 64 facade). If all pass, the build is good.

---

## Rendering

### How do I create a custom render pipeline?

Write a JSON pipeline file that describes your passes, resources, and bindings. Then point the engine to it via the `pipeline_json_path` configuration option. The engine compiles the JSON into Vulkan resources and executes the pipeline automatically.

See [JSON Render Pipeline Guide](guides/json-render-pipeline.md) for the full specification.

### What is a dot-path?

A dot-path is a string expression like `"entity.transform.worldMatrix"` that resolves to runtime ECS data at render time. Dot-paths are used inside JSON pipeline buffer layouts to connect shader uniforms to live scene data without writing any binding code.

See [Dot-Path Syntax](guides/json-render-pipeline.md#dot-path-syntax) for the full syntax reference.

### How do I add post-processing effects?

Add a fullscreen pass in your JSON pipeline that reads the scene color attachment as an input and writes to the swapchain (or another intermediate target). Chain multiple fullscreen passes for multi-step effects. See the bloom pipeline example for a working reference.

### My scene is black or not rendering. What do I check?

Work through this checklist:

1. **Pipeline JSON path** -- is the path correct and the file valid?
2. **HDR environment** -- is an environment map loaded for image-based lighting?
3. **Camera** -- does a camera entity exist with `isMainCamera = true`?
4. **Lighting** -- does at least one light entity exist in the scene?
5. **Model** -- did the model load successfully without errors?

---

## Physics

### Physics is not working. What do I check?

Work through this checklist:

1. **Physics enabled** -- is `physics.enabled` set to `True`?
2. **Components** -- does the entity have both a `RigidBody` and a `Collider` component?
3. **Rebuild** -- after adding physics components from Python, have you called `rebuild_body()`?
4. **Gravity** -- is gravity configured (it defaults to zero if not set)?

### How do I change a collider shape at runtime?

Modify the fields on the `ColliderComponent`, then call `physics.rebuild_body(entity)` to reconstruct the underlying Bullet3 rigid body with the new shape.

---

## Python

### How do I pass data to shaders from Python?

Use the custom uniform setters on the engine object:

```python
engine.set_custom_float("time", elapsed)
engine.set_custom_vec3("tint", (1.0, 0.5, 0.0))
```

Then reference the value in your JSON pipeline as `scene.custom.<key>` (for example, `scene.custom.time`).

See [Custom Shader Uniforms](guides/custom-shader-uniforms.md) for the full guide.

### Can I use async/await with the engine?

No. The engine runs a synchronous main loop via `engine.run()`. All callbacks (on_update, on_key, etc.) execute on the main thread during the render loop. If you need background work, manage your own threads, but keep all engine API calls on the main thread.

### How do I access entity data?

All entity manipulation goes through `engine.scene`. Use the getter and setter methods:

```python
pos = scene.get_position(entity)
scene.set_light_color(entity, (1.0, 0.8, 0.6))
```

See the [Scene API](api/python/scene.md) reference for the complete method list.

### What are GLFW key codes?

Integer constants used for keyboard input in the `on_key` callback. Common values:

| Key   | Code |
|-------|------|
| ESC   | 256  |
| W     | 87   |
| A     | 65   |
| S     | 83   |
| D     | 68   |
| SPACE | 32   |
| SHIFT | 340  |

See the [Input API](api/python/input.md) reference for the full list.

---

## General

### What is the difference between ApplicationBase and EngineAPI?

They provide the same functionality through different programming styles:

- **ApplicationBase** is inheritance-based -- you subclass it and override virtual methods (`onInit`, `onUpdate`, etc.).
- **EngineAPI** is callback-based -- you set lambdas or function pointers (`on_init`, `on_update`, etc.).

The Python bindings wrap EngineAPI, so Python users always use the callback style.

See [C++ Quickstart](getting-started/cpp-quickstart.md) for examples of both approaches.

### How do I add a new ECS component?

**In C++:** Define a struct for the component data and register it with the `ComponentRegistry`.

**In Python:** Use the string-based API: `scene.add_component(entity, "ComponentName")`.

See [Entities and Components](guides/entities-and-components.md) for the full guide.

### How many tests does the engine have?

582 tests total: 518 core engine tests and 64 facade tests. Build with `BUILD_TESTS=ON` and run with `ctest` to execute them all.
