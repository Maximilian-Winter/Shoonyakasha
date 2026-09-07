# Shoonyakasha documentation

Build C++ or Python applications with JSON-defined Vulkan rendering. Begin with a working example, then use the guides to change one subsystem at a time. Current behavior is documented here; APIs are still evolving.

## Start here

- [Prerequisites](getting-started/prerequisites.md) and [build/install guide](../BUILDING.md).
- [Python quickstart](getting-started/python-quickstart.md): generate and run a starter.
- [C++ quickstart](getting-started/cpp-quickstart.md): build and run the facade example.
- [JSON pipeline walkthrough](guides/json-render-pipeline.md) and [detailed JSON reference](reference/pipeline-json.md).
- [FAQ](faq.md): missing files, validation, bindings, and known limits.

## Build an application

| Task | Guide |
|---|---|
| Load models and preserve node transforms | [Loading scenes](guides/loading-scenes.md), [geometry sharing/instancing](guides/instancing.md) |
| Create, parent, query, and update entities | [Entities/components](guides/entities-and-components.md) |
| Write custom components and per-frame systems | [Script ECS](guides/script-ecs.md) |
| Set up a camera and input | [Cameras/controllers](guides/cameras-and-controllers.md) |
| Work with surfaces and light | [Materials](guides/materials.md), [lighting/IBL](guides/lighting-and-ibl.md) |
| Animate a character | [Skeletal animation](guides/animation.md) |
| Draw 2D content | [Sprites, UI, and text](guides/sprites-ui-text.md) |
| Add simulation | [Physics](guides/physics.md), [compute/GPU data flow](guides/compute-and-data-flow.md) |
| Send application values to shaders | [Custom uniforms](guides/custom-shader-uniforms.md) |
| Capture output or persist state | [Frame capture](guides/frame-capture.md), [scene snapshot limitations](guides/scene-serialization.md) |

## Public API references

| Interface | Python | C++ |
|---|---|---|
| Engine and lifecycle | [Engine](api/python/engine.md) | [EngineAPI](api/cpp/engine-api.md) |
| Entities and built-in components | [Scene](api/python/scene.md) | [SceneAPI](api/cpp/scene-api.md) |
| Keyboard and mouse | [Input](api/python/input.md) | [InputAPI](api/cpp/input-api.md) |
| Physics control | [Physics](api/python/physics.md) | [PhysicsAPI](api/cpp/physics-api.md) |
| Script payloads and systems | [Ecs](api/python/ecs.md) | [EcsAPI](api/cpp/ecs-api.md) |

Also see [loading options/results](api/python/gltf-result.md), [constants](api/python/constants.md), and [Python utilities](api/python/utilities.md). Member inventories in the facade references are derived from source; surrounding prose documents runtime contracts and limitations.

Native extension references: [ApplicationBase](api/cpp/application-base.md), [frame graph](api/cpp/frame-graph.md), [ECS component map](api/cpp/ecs-components.md), [glTF loader](api/cpp/gltf-loader.md), [IBL generator](api/cpp/ibl-generator.md), [GPU types/ownership](api/cpp/gpu-types.md).

## Examples and architecture

- [Example catalog](../examples/README.md), [C++ targets](examples/cpp-examples.md), [Python commands](examples/python-examples.md), [assets](../assets/README.md).
- [Architecture overview](architecture/overview.md), [frame graph](architecture/frame-graph-pipeline.md), [ECS](architecture/ecs-design.md), [facade](architecture/facade-pattern.md), [Cython bridge](architecture/cython-bridge.md).
- [Documentation maintenance](maintenance.md), [refresh audit](audit.md), [historical archive](archive.md), [philosophy](philosophy.md).
