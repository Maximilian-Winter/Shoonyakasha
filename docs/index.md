# Shoonyakasha

**शून्याकाश — Void-Space Engine**

> *"In emptiness, all forms arise and dissolve. In space, infinite possibilities dance."*

Shoonyakasha is a modern, modular Vulkan-based C++ game engine library with high-performance Python bindings. It combines a declarative JSON-driven render pipeline with an Entity Component System architecture, letting you build real-time 3D applications in C++ or drive the entire engine from Python through its Cython bridge.

---

## Feature Highlights

- **Vulkan-powered rendering** — modern, explicit graphics API for maximum performance
- **EnTT Entity Component System** — data-oriented design for optimal cache performance
- **Bullet3 physics** — rigid body dynamics, collision detection, and raycasting
- **JSON-driven render pipeline** — declare buffer layouts, passes, and bindings in JSON, not code
- **Python / Cython bindings** — full engine control from Python with near-native speed
- **glTF 2.0 loading** — import scenes, meshes, materials, and node hierarchies
- **Skeletal animation** — skinned mesh playback with bone transforms
- **IBL / PBR lighting** — image-based lighting and physically based materials

---

## Documentation

### Getting Started

| Document | Description |
|----------|-------------|
| [Prerequisites](getting-started/prerequisites.md) | Required tools, SDKs, and build setup |
| [Python Quickstart](getting-started/python-quickstart.md) | Your first Shoonyakasha app in Python |
| [C++ Quickstart](getting-started/cpp-quickstart.md) | Your first Shoonyakasha app in C++ |

### Guides

| Guide | Description |
|-------|-------------|
| [Loading Scenes](guides/loading-scenes.md) | Load glTF models and assemble a scene |
| [JSON Render Pipeline](guides/json-render-pipeline.md) | Declare your rendering pipeline in JSON |
| [Entities and Components](guides/entities-and-components.md) | Work with the ECS — create, query, and modify entities |
| [Cameras and Controllers](guides/cameras-and-controllers.md) | Set up cameras, orbit controls, and fly-through |
| [Lighting and IBL](guides/lighting-and-ibl.md) | Point lights, directional lights, and image-based lighting |
| [Materials](guides/materials.md) | PBR material properties, textures, and overrides |
| [Physics](guides/physics.md) | Add rigid bodies, collision shapes, and raycasting |
| [Animation](guides/animation.md) | Play skeletal animations and control playback |
| [Custom Shader Uniforms](guides/custom-shader-uniforms.md) | Push your own data to shaders via dot-path bindings |
| [Scene Serialization](guides/scene-serialization.md) | Save and load scenes and render graphs as JSON |

### API Reference — Python

| Reference | Description |
|-----------|-------------|
| [Engine](api/python/engine.md) | `Engine` class — initialization, run loop, shutdown |
| [Scene](api/python/scene.md) | `Scene` class — entity creation, glTF loading, queries |
| [Input](api/python/input.md) | Keyboard, mouse, and gamepad input |
| [Physics](api/python/physics.md) | Physics world, rigid bodies, raycasting |
| [Constants](api/python/constants.md) | Enumerations and constant values |
| [GltfResult](api/python/gltf-result.md) | Result object returned by glTF loading |

### API Reference — C++

| Reference | Description |
|-----------|-------------|
| [Engine API](api/cpp/engine-api.md) | `ApplicationBase` and engine lifecycle |
| [Scene API](api/cpp/scene-api.md) | Scene management and entity operations |
| [Input API](api/cpp/input-api.md) | Input system and key codes |
| [Physics API](api/cpp/physics-api.md) | `PhysicsWorld` and rigid body interface |
| [ECS Components](api/cpp/ecs-components.md) | Component types: Transform, Mesh, Material, etc. |
| [ApplicationBase](api/cpp/application-base.md) | Base class lifecycle, hooks, and configuration |
| [Frame Graph](api/cpp/frame-graph.md) | `RenderGraph`, compilation, and `FrameGraphRenderer` |
| [glTF Loader](api/cpp/gltf-loader.md) | `GltfLoader` and scene import |
| [IBL Generator](api/cpp/ibl-generator.md) | Environment map processing and IBL generation |
| [GPU Types](api/cpp/gpu-types.md) | Thin GPU resource types and handles |

### Architecture

| Document | Description |
|----------|-------------|
| [Overview](architecture/overview.md) | High-level architecture and module map |
| [Frame Graph Pipeline](architecture/frame-graph-pipeline.md) | How JSON becomes Vulkan draw calls |
| [ECS Design](architecture/ecs-design.md) | Entity-component-system patterns and data flow |
| [Facade Pattern](architecture/facade-pattern.md) | PIMPL facade layer between core and Python |
| [Cython Bridge](architecture/cython-bridge.md) | How Cython wraps the C++ facade |

### Examples

| Document | Description |
|----------|-------------|
| [Python Examples](examples/python-examples.md) | Annotated Python demo walkthroughs |
| [C++ Examples](examples/cpp-examples.md) | Annotated C++ example walkthroughs |

### FAQ

| Document | Description |
|----------|-------------|
| [FAQ](faq.md) | Frequently asked questions and troubleshooting |

---

## Quick Links

Looking for something specific? Start here:

- **Load a scene** — [Loading Scenes guide](guides/loading-scenes.md)
- **Add physics** — [Physics guide](guides/physics.md)
- **Animate a character** — [Animation guide](guides/animation.md)
- **Create a render pipeline** — [JSON Render Pipeline guide](guides/json-render-pipeline.md)

---

## Philosophy

Shoonyakasha embodies the union of emptiness (*sunya*) and infinite creative potential (*akasa*). Read more about the engine's design philosophy and the meaning behind its name in the [Philosophy](philosophy.md) document.

---

*This engine is dedicated to Vajrayogini, the sky-dancing wisdom dakini, and her fierce retinue of dakinis who cut through illusion with compassion.*
