<div align="center">

<img src="logo.png" alt="Shoonyakasha Engine" width="300">

# Shoonyakasha

*शून्याकाश — Void-Space Engine*

</div>

Shoonyakasha is a C++20 Vulkan engine library with Python bindings and **JSON-defined render pipelines**. Declare resources, graphics and compute passes, shader bindings, buffer layouts, and data sources; the engine compiles the graph and resolves its bindings at runtime. Build applications through the C++ facade or Python, or extend the renderer through the lower-level C++ APIs.

**Status:** Early development. APIs may change. Supported settings do not necessarily have a ready-made rendering effect; see the feature boundaries below and the [documentation](docs/index.md).

## Example gallery

| 3D rendering | Compute particles |
|---|---|
| [<img src="docs/images/examples/cpp/instancing_test.png" alt="Pastel boxes with independent transforms and shared geometry" width="400">](docs/examples/cpp-examples.md#instancing-instancing_test) | [<img src="docs/images/examples/cpp/particle_flow_example.png" alt="Multicolored GPU particles surrounding the bundled red box" width="400">](docs/examples/cpp-examples.md#particle-flow-particle_flow_example) |
| Instancing with bundled boxes | Particle flow with the bundled-box fallback |
| **Skeletal animation** | **2D and UI** |
| [<img src="docs/images/examples/python/skinned_fox_demo.png" alt="Bundled animated Fox model rendered through Python" width="400">](docs/examples/python-examples.md#animated-fox-skinned_fox_demo) | [<img src="docs/images/examples/python/full_showcase.png" alt="Orbiting sprites, colored halos, health HUD and system-status text" width="400">](docs/examples/python-examples.md#full-showcase-full_showcase) |
| Fox animation through Python | Sprites, blend modes, text, and Python ECS |

Browse every preview in the [C++ guide](docs/examples/cpp-examples.md) and [Python guide](docs/examples/python-examples.md). These are native runtime captures; the guides explain asset substitutions and the missing Pong preview.

## JSON is the rendering interface

This fragment defines a camera buffer populated from engine state:

```json
{
  "bufferLayouts": {
    "CameraUBO": {
      "usage": "uniform_buffer",
      "packing": "std140",
      "updateFrequency": "per_frame",
      "fields": [
        { "name": "view", "type": "mat4", "source": "scene.camera.view" },
        { "name": "projection", "type": "mat4", "source": "scene.camera.projection" }
      ]
    }
  }
}
```

A complete pipeline also declares passes, resources, and bindings. Change their configuration without recompiling the engine; compile changed GLSL shaders to SPIR-V and restart the application. Dot-paths address supported engine data, material values, and application-provided `scene.custom.*` values. New renderer behavior or native data sources can still require C++.

Start with the [pipeline walkthrough](docs/guides/json-render-pipeline.md), [JSON reference](docs/reference/pipeline-json.md), or [complete starter pipeline](python/shoonyakasha/templates/pipeline.json).

## Features

| Area | Current capabilities | Learn more |
|---|---|---|
| Declarative rendering | Graphics/compute passes, resource dependencies, queue selection, vertex formats, descriptors, push constants, UBO/SSBO layouts, packing, and dot-path bindings | [JSON pipelines](docs/guides/json-render-pipeline.md) |
| Rendering examples | Forward shading, deferred PBR, HDR image-based lighting, tonemapping, bloom, and compute-driven particles | [Examples](examples/README.md) |
| Render control | Depth/culling/blend state, custom blend factors, opaque/transparent/skinned/sprite execution, layer masks, and sorting | [Pipeline reference](docs/reference/pipeline-json.md) |
| GPU data flow | Shared storage buffers, parameterized counts and dispatch, initialization, readback, and render-target saving | [Compute and data flow](docs/guides/compute-and-data-flow.md) |
| Scenes and ECS | EnTT entities, named component registry, parent/child transforms, camera controllers, lights, material parameters, and partial scene serialization | [Entities](docs/guides/entities-and-components.md) |
| Assets and instancing | glTF/GLB meshes, materials, textures, node hierarchies, shared geometry for repeated mesh nodes, skins, and animation clips | [Loading scenes](docs/guides/loading-scenes.md), [instancing](docs/guides/instancing.md) |
| Animation | Skeletal clip playback, time/speed/loop control, and GPU bone buffers | [Animation](docs/guides/animation.md) |
| 2D and UI | World sprites, anchored screen panels, UV rectangles, colors, glyph-atlas text, layering, and draw order | [Sprites, UI, and text](docs/guides/sprites-ui-text.md) |
| Physics | Bullet rigid bodies; box, sphere, capsule, and plane shapes; gravity, forces, impulses, velocity, and fixed-step settings | [Physics](docs/guides/physics.md) |
| Python | Cython facade bindings, lifecycle/input callbacks, arbitrary Python ECS components, and per-frame script systems | [Python quickstart](docs/getting-started/python-quickstart.md), [script ECS](docs/guides/script-ecs.md) |
| Developer tools | Project scaffolding, incremental shader compilation, pipeline diagnostics, asset lookup/fetch, frame-graph analysis/export, and Vulkan validation | [Python utilities](docs/api/python/utilities.md), [frame graph](docs/api/cpp/frame-graph.md) |
| Capture | Screenshots of the presented frame and video recording through an external ffmpeg executable | [Frame capture](docs/guides/frame-capture.md) |

The mesh collider enum currently uses a box fallback; raycasting and constraints are not exposed by the physics API. Text supports ASCII glyphs 32–126. Scene JSON is a partial component snapshot, not a complete game save. Geometry sharing does not imply automatic batching into one instanced draw. Capture uses synchronous GPU readback. The linked guides explain these limits and C++/Python differences.

## Get started

Install the compiler, Vulkan SDK, and vcpkg dependencies using [BUILDING.md](BUILDING.md). Python use also requires building the native extension.

After installing the Python package, run these commands **from the repository root**:

```sh
python -m shoonyakasha.init my_game
cd my_game
python main.py
```

The starter contains Python code, a pipeline, and shaders. It compiles shaders on startup and loads the bundled box from the repository's `assets/` directory. For projects outside the checkout, set `SHOONYAKASHA_ASSET_DIR` to that directory. See the [Python quickstart](docs/getting-started/python-quickstart.md).

For C++, build and run `FacadeTest` following the [C++ quickstart](docs/getting-started/cpp-quickstart.md). It demonstrates scene loading, camera/input callbacks, and the facade without Vulkan or EnTT in application code. The [build guide](BUILDING.md) also covers linking the installed library through CMake.

## Documentation and examples

- [Documentation index](docs/index.md): guides, C++/Python references, and architecture.
- [Example catalog](examples/README.md): rendering, compute, animation, physics, and 2D games.
- [Asset guide](assets/README.md): bundled assets, optional downloads, and licenses.
- [Documentation maintenance](docs/maintenance.md): checks and source-of-truth checklist.

Pipelines and shaders are resolved from the application's working directory. Models, environments, textures, and fonts use the shared asset resolver. Run examples from their own source directories; some artwork is optional or separately licensed.

## Project structure

| Directory | Contents |
|---|---|
| `include/`, `src/` | C++ interfaces and implementations |
| `python/` | Cython bindings, Python utilities, and starter template |
| `examples/` | C++ and Python examples grouped by topic |
| `assets/`, `tools/` | Shared assets and development tools |
| `tests/` | C++ unit tests and Python checks |
| `docs/` | Current documentation and labeled historical material |
| `cmake/`, `third_party/` | Build helpers and vendored dependencies |

## Philosophy

**Shoonyakasha** (शून्याकाश) combines *sunya* (emptiness) and *akasa* (space): clean interfaces and room to create. Read the [Philosophy](docs/philosophy.md).

## Dedication

*This engine is dedicated to Vajrayogini, the sky-dancing wisdom dakini, and her fierce retinue of dakinis who cut through illusion with compassion. May this code serve the benefit of all beings, transforming pixels into wisdom, emptiness into form.*

## License

MIT. See [LICENSE](LICENSE). Assets have their own [provenance and licenses](assets/README.md).
