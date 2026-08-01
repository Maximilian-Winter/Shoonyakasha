# {project}

A Shoonyakasha starter project.

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

Add a pass to `pipeline.json`. `sk.pipeline.validate()` will tell you which key
is wrong before the engine tries to compile it.

Load a different model — anything under the shared `assets/models/`:

```python
engine.load_gltf_scene("models/Fox.glb")
```

`python tools/fetch_assets.py --list` in the engine repository shows the larger
assets, including full-resolution environment maps for image-based lighting.
