# Shoonyakasha Engine Documentation — Design Document

**Date:** 2026-03-04
**Author:** Maximilian Winter + 九天玄碼女
**Status:** Approved

## Goal

Create comprehensive documentation for the Shoonyakasha engine covering both Python game developers and C++ engine developers. Documentation lives in-repo as Markdown files under `docs/`.

## Audience

- **Python developers:** Using the Engine/Scene/Input/Physics Python API to build games and simulations
- **C++ developers:** Extending the engine core, writing custom systems, or using ApplicationBase directly

## Documentation Structure

```
docs/
├── index.md                         # Engine overview, navigation
├── philosophy.md                    # Śūnya/Ākāśa design principles
│
├── getting-started/
│   ├── prerequisites.md             # Build requirements, setup
│   ├── python-quickstart.md         # "Hello Sponza" in Python
│   └── cpp-quickstart.md            # ApplicationBase hello-world
│
├── guides/
│   ├── loading-scenes.md            # glTF workflow
│   ├── json-render-pipeline.md      # Pipeline JSON, dot-paths, resources
│   ├── entities-and-components.md   # Entity creation, component management
│   ├── cameras-and-controllers.md   # Camera types, controller modes
│   ├── lighting-and-ibl.md          # Lights, HDR, PBR environment
│   ├── materials.md                 # Material params, textures, alpha
│   ├── physics.md                   # Bullet3 integration guide
│   ├── animation.md                 # Skeletal animation workflow
│   ├── custom-shader-uniforms.md    # Custom values, dot-path binding
│   └── scene-serialization.md       # Save/load scenes
│
├── api/
│   ├── python/
│   │   ├── engine.md               # Engine class
│   │   ├── scene.md                # Scene class
│   │   ├── input.md                # Input class
│   │   ├── physics.md              # Physics class
│   │   ├── gltf-result.md          # GltfResult
│   │   └── constants.md            # Enums & constants
│   └── cpp/
│       ├── engine-api.md           # EngineAPI facade
│       ├── scene-api.md            # SceneAPI facade
│       ├── input-api.md            # InputAPI facade
│       ├── physics-api.md          # PhysicsAPI facade
│       ├── ecs-components.md       # All ECS components
│       ├── application-base.md     # ApplicationBase
│       ├── frame-graph.md          # RenderGraph, DotPath, Renderer
│       ├── gltf-loader.md          # GltfSceneLoader
│       ├── ibl-generator.md        # IBL pipeline
│       └── gpu-types.md            # GPUBuffer, GPUTexture, MaterialParam
│
├── architecture/
│   ├── overview.md                  # System diagram, data flow
│   ├── frame-graph-pipeline.md      # JSON → Compile → Execute
│   ├── ecs-design.md               # EnTT patterns
│   ├── facade-pattern.md           # PIMPL bridge design
│   └── cython-bridge.md            # Python ↔ C++ binding architecture
│
├── examples/
│   ├── python-examples.md          # demo.py, skinned_fox_demo.py walkthrough
│   └── cpp-examples.md             # C++ example apps walkthrough
│
└── faq.md                          # Troubleshooting & common issues
```

**Total: ~35 documents**

## Content Strategy

### Getting Started
- **Tone:** Welcoming, practical, zero assumptions
- **Structure:** Goal → Prerequisites → Step-by-step → Result → Next steps
- Python quickstart: Engine → callbacks → glTF → camera + light → run
- C++ quickstart: Subclass ApplicationBase → override hooks → JSON pipeline → run

### Guides
- **Tone:** Tutorial, teaching concepts through examples
- **Structure:** Concept → Code (Python first, C++ below) → Explanation → Patterns → Pitfalls
- Self-contained, with cross-links to API reference

### API Reference
- **Tone:** Precise, exhaustive
- **Per class:** Description → Constructor → Properties table → Methods with signatures, params, returns, examples
- Python uses Python types; C++ uses glm:: signatures

### Architecture
- **Tone:** Technical, explanatory, aimed at contributors
- **Structure:** Problem → Decision → How it works → Diagram → Trade-offs
- Mermaid/ASCII diagrams for data flow

### Examples
- Annotated walkthroughs of existing example applications

### FAQ
- Organized by category: Setup, Rendering, Physics, Python
- Question → Short answer → Details

## Key API Surface to Document

### Python API (4 classes + types)
- `Engine` — lifecycle, callbacks, entity helpers, custom uniforms
- `Scene` — entity CRUD, transforms, cameras, lights, materials, animation, hierarchy
- `Input` — key/mouse polling, event callbacks
- `Physics` — gravity, forces, velocities, body management
- `GltfResult` — load result data
- Constants: NULL_ENTITY, camera/light/rigidbody/collider type enums

### C++ Facade API (4 classes + types)
- `EngineAPI` — mirrors Python Engine
- `SceneAPI` — mirrors Python Scene
- `InputAPI` — mirrors Python Input
- `PhysicsAPI` — mirrors Python Physics
- `EngineConfig`, `GltfOptions`, `GltfLoadResult`

### C++ Core API
- **ECS Components:** Transform, Camera, Light, RigidBody, Collider, Mesh, MaterialV5, RenderableTag, Skeleton, AnimationPlayback, Hierarchy, Name, Tag, Active, Lifetime, CameraController
- **EntityBuilder** — fluent entity factory
- **ComponentRegistry** — string-based component access
- **ApplicationBase** — convenience C++ app base class
- **RenderGraph** — JSON pipeline compilation
- **DotPathResolver** — runtime ECS data resolution for shaders
- **FrameGraphRenderer** — geometry pass execution, entity filtering/sorting
- **GltfSceneLoader** — glTF 2.0 scene import
- **IBLGenerator** — PBR environment map generation
- **GPU types** — GPUBuffer, GPUTexture, MaterialParam, AlphaMode, IndexType

### JSON Pipeline Schema
- Pass declarations (type, execution, targets, bindings, shaders)
- Resource declarations (images, buffers, formats)
- Dot-path syntax reference
- Buffer layout fields and packing rules

## Implementation Order

1. **Foundation:** index.md, philosophy.md, prerequisites.md
2. **Quick starts:** python-quickstart.md, cpp-quickstart.md
3. **API Reference (Python):** Engine, Scene, Input, Physics, constants
4. **API Reference (C++):** Facade APIs, ECS components, core systems
5. **Guides:** Loading scenes, JSON pipeline, entities, cameras, lighting, materials, physics, animation, custom shaders, serialization
6. **Architecture:** Overview, frame-graph, ECS, facade, cython bridge
7. **Examples:** Python and C++ walkthroughs
8. **FAQ:** Troubleshooting

## Notes

- Existing docs (`CameraSystemIntegration.md`, `declarative_ssbo_data_flow.md`) can be referenced or incorporated into the new structure
- Documentation should reference source file paths for deeper exploration
- The engine's philosophical theme (Śūnya/Ākāśa) adds unique character — preserve it
