<div align="center">

<img src="logo.png" alt="Shoonyakasha Engine" width="300">

# Shoonyakasha

*शून्याकाश — Void-Space Engine*

</div>

---

> **Status:** Shoonyakasha is in early development. The core engine works and the examples run, but the API may change as things evolve. Feedback and contributions are welcome.

<!-- Add a hero screenshot here: screenshots/hero.png -->
<!-- A rendered scene (e.g. Sponza with IBL and particles) works well -->

## What is this?

Shoonyakasha is a Vulkan-based C++ game engine library with Python bindings. Its main idea is that render pipelines are declared in JSON rather than coded in C++ — buffer layouts, render passes, shader bindings, and data sources are all described in a JSON file that the engine compiles into Vulkan resources at runtime. You can build 3D applications in C++ or drive the entire engine from Python through its Cython bridge.

## Quick Start — Python

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="My App",
    width=1280, height=720,
    pipeline_json_path="pipeline.json",
    hdr_environment_path="environment.hdr",
)

def on_init():
    engine.create_camera(pos=(0, 5, 15), fov=60, speed=8)
    engine.create_directional_light(
        direction=(-0.5, -1, -0.3),
        color=(1, 0.975, 0.95),
        intensity=3.0,
    )
    engine.load_gltf_scene("scene.gltf")

engine.set_on_init(on_init)
engine.run()
```

## Quick Start — C++

```cpp
#include "Facade/EngineAPI.h"

using namespace Shoonyakasha::Facade;

int main() {
    EngineConfig config;
    config.title = "My App";
    config.width = 1280;
    config.height = 720;
    config.pipelineJsonPath = "pipeline.json";
    config.hdrEnvironmentPath = "environment.hdr";

    EngineAPI engine(config);

    engine.setOnInit([&]() {
        engine.createCamera(glm::vec3(0, 5, 15), 60.f, 8.f, 0.1f, 500.f);
        engine.createDirectionalLight(
            glm::vec3(-0.5f, -1.f, -0.3f),
            glm::vec3(1.f, 0.975f, 0.95f),
            3.f
        );
        engine.loadGltfScene("scene.gltf");
    });

    engine.run();
    return 0;
}
```

## JSON Render Pipelines

Instead of writing C++ code to allocate buffers, create render passes, and bind descriptor sets, you describe your pipeline in JSON. The engine compiles it into Vulkan resources and resolves data bindings at runtime through dot-paths.

```json
{
  "name": "CameraUBO",
  "usage": "uniform_buffer",
  "packing": "std140",
  "updateFrequency": "per_frame",
  "fields": [
    { "name": "view",       "type": "mat4", "source": "scene.camera.view" },
    { "name": "projection", "type": "mat4", "source": "scene.camera.projection" },
    { "name": "position",   "type": "vec3", "source": "scene.camera.position" }
  ]
}
```

A dot-path like `scene.camera.view` tells the engine to walk the ECS at render time, find the camera's view matrix, and write it into the buffer. No glue code needed — add a new data source by referencing it by name in JSON.

You can change your entire rendering pipeline — add a shadow pass, swap a shader, restructure your buffers — by editing the JSON file. No C++ recompilation required.

## Features

**Rendering**
- Vulkan rendering with explicit synchronization
- PBR materials and image-based lighting (IBL)
- Declarative JSON render pipelines with runtime dot-path data binding
- Post-processing (bloom)
- Async compute (GPU particle simulation)
- Skeletal animation with skinned meshes

**Entity Component System**
- EnTT-based ECS with 17+ component types
- Hierarchical transforms with parent-child relationships
- Entity builder pattern and component registry

**Physics**
- Bullet3 rigid body dynamics and collision detection
- Box, sphere, capsule, mesh, and plane colliders
- Raycasting and gravity control

**Asset Loading**
- glTF 2.0 import (meshes, materials, node hierarchies, animations)
- HDR environment maps for IBL
- Automatic texture and material setup

**Python Integration**
- Cython bindings with near-native performance
- Full engine control: scenes, physics, input, rendering
- Callback-driven lifecycle (init, update, render, input, cleanup)

<!-- screenshots/particles.png — 50K GPU particle simulation -->
<!-- screenshots/animation.png — Skeletal animation playback -->

## Building from Source

### Prerequisites

- C++20 compiler (MSVC 2022, GCC 12+, or Clang 15+)
- CMake 3.12+
- Vulkan SDK
- vcpkg or manually installed: GLFW3, nlohmann_json, EnTT, Bullet3, GLM

### Build

```bash
mkdir build && cd build
cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON
cmake --build . --config Release
```

For Python bindings:

```bash
cmake .. -DBUILD_PYTHON=ON
cmake --build . --config Release
```

The compiled `_shoonyakasha.pyd` (Windows) or `.so` (Linux) will be in `python/shoonyakasha/`.

## Project Structure

```
include/           C++ headers organized by subsystem
src/               Implementation files
python/            Cython bindings and Python package
  shoonyakasha/    Python module
  examples/        Python demo scripts
examples/          8 C++ example applications
tests/             Automated test suite (582 tests)
docs/              Guides, API reference, architecture docs
third_party/       VulkanMemoryAllocator, cgltf, stb, tinyobjloader
cmake/             CMake configuration
```

## Documentation

Full documentation is in the [`docs/`](docs/index.md) directory, including:

- Getting started guides for [Python](docs/getting-started/python-quickstart.md) and [C++](docs/getting-started/cpp-quickstart.md)
- Guides on [JSON pipelines](docs/guides/json-render-pipeline.md), [physics](docs/guides/physics.md), [animation](docs/guides/animation.md), and more
- API reference for both Python and C++
- Architecture documentation

## Examples

| Example | Description |
|---------|-------------|
| `facade_test` | Full application using only the Facade API |
| `declarative_sponza_test` | Architectural scene with PBR and IBL |
| `particle_test` | 50K+ GPU particle simulation with async compute |
| `bloom_test` | Post-processing bloom effect |
| `particle_flow_example` | Particle systems with SSBO data flow |
| `ssbo_data_flow_example` | Storage buffer patterns and data binding |
| `skinned_mesh_test` | Skeletal animation playback |
| `physics_test` | Rigid body dynamics and collision |
| `pbr_physics_particles` | Combined physics, particles, and PBR |

## Philosophy

**Shoonyakasha** (शून्याकाश) combines two Sanskrit words: *sunya* (शून्य, emptiness) and *akasa* (आकाश, space). Emptiness here means clean interfaces that don't get in the way. Space means a flexible architecture with room for what you haven't imagined yet.

Read more in the [Philosophy](docs/philosophy.md) document.

## Dedication

*This engine is dedicated to Vajrayogini, the sky-dancing wisdom dakini, and her fierce retinue of dakinis who cut through illusion with compassion. May this code serve the benefit of all beings, transforming pixels into wisdom, emptiness into form.*

## License

MIT License. See [LICENSE](LICENSE) for details.
