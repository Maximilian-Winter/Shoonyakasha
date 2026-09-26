# Python quickstart

Build/install the package using the [Python installation instructions](../../BUILDING.md#python-bindings). On Windows, use the documented `x64-windows-static-md` triplet. Keep repository assets available and put `glslc` on `PATH` or set `VULKAN_SDK`.

## Verify the installation

```sh
python -c "import shoonyakasha as sk; print(sk.extension_available()); print(sk.Engine)"
```

This must print `True` and the engine class. Utilities import without the extension, so `import shoonyakasha` alone is insufficient.

## Create and run a project

From the repository root:

```sh
python -m shoonyakasha.init my_game
cd my_game
python main.py
```

The generator writes `main.py`, `pipeline.json`, two GLSL shaders, and a README. The program compiles shaders, reports pipeline diagnostics, loads bundled `models/Box.gltf`, and opens a forward-rendered scene. No HDR environment or Sponza download is needed.

Use WASD to move and the right mouse button to look around. Edit `shaders/basic.frag` or the pipeline and restart to see changes.

Outside the checkout, set `SHOONYAKASHA_ASSET_DIR` to the absolute path of its `assets` directory. Assets are not copied into generated projects or packaged into the wheel.

## Application structure

This complete application uses the generated project's pipeline and shaders:

```python
import shoonyakasha as sk

sk.shaders.compile_dir("shaders")
sk.pipeline.check("pipeline.json")  # raises ValueError on validation errors

engine = sk.Engine(title="My scene", pipeline_json_path="pipeline.json")

def on_init():
    result = engine.load_gltf_scene("models/Box.gltf")
    if not result.success:
        print(result.error)
    engine.create_camera((0.0, 1.5, 5.0))

engine.set_on_init(on_init)
engine.run()
```

Leave out `pipeline_json_path` and the engine renders with the [default pipeline](../../python/shoonyakasha/pipelines/default/README.md) instead: deferred PBR with cascaded sun shadows, no shaders of your own needed.

`run()` blocks until the window closes. Create entities in `on_init`: Vulkan and the scene exist, but graph compilation follows this callback. `on_post_init` runs after compilation. Update application state in `on_update(dt)`; `dt` is seconds.

`engine.scene` and `engine.ecs` require initialization. Configure physics in `on_init`; setters called before the native physics system is connected do not persist. Input callbacks can be registered before `run()`.

The callback bridge prints exceptions; do not rely on raising in a callback to terminate the native loop. Script systems have a separate failure counter and auto-disable policy.

## Next steps

- [JSON pipeline walkthrough](../guides/json-render-pipeline.md).
- [Python examples](../examples/python-examples.md) for PBR/IBL, animation, and 2D UI.
- [Engine reference](../api/python/engine.md), [entities](../guides/entities-and-components.md), and [script ECS](../guides/script-ecs.md).
