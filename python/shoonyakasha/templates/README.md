# {project}

A Shoonyakasha starter project. Run from this directory after installing the native Python extension. You need a Vulkan-capable driver/device and `glslc` on PATH or discoverable through VULKAN_SDK.

The starter loads `models/Box.gltf` from the shared engine assets. Outside the engine checkout, set `SHOONYAKASHA_ASSET_DIR` to the absolute path of its `assets` directory; the generator does not copy assets.

```sh
python main.py
```

## What is here

| File | |
|---|---|
| `main.py` | Compiles shaders, validates the pipeline, opens a window |
| `pipeline.json` | One forward pass that draws entities and presents |
| `shaders/basic.vert` | Standard vertex format, camera UBO, model push constant |
| `shaders/basic.frag` | Lambert shading — the thing to edit first |

## Things to try

Edit `shaders/basic.frag` and run again. `main.py` recompiles it for you.

Add a pass to `pipeline.json`. `sk.pipeline.validate()` reports supported preflight checks; it does not validate every native parser feature or shader interface.

Load a different model — anything under the shared `assets/models/`:

```python
engine.load_gltf_scene("models/Fox.glb")
```

`python tools/fetch_assets.py --list` in the engine repository shows the larger
assets, including full-resolution environment maps for image-based lighting.
