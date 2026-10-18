# Choose your path

Shoonyakasha is a C++20 Vulkan engine with a C++ facade, Python bindings, and JSON-defined render pipelines. Choose a starting point based on what you want to build. Both language paths use the same native renderer.

The engine is in early development. Follow the documentation from the same revision as your checkout; signatures and pipeline settings can change.

## Build a C++ application

1. Check the [prerequisites](prerequisites.md), then [build the engine](../../BUILDING.md) with `BUILD_EXAMPLES=ON`.
2. Run `FacadeTest` using the [C++ quickstart](cpp-quickstart.md). The bundled box is enough to get started.
3. Learn [entities and components](../guides/entities-and-components.md), [scene loading](../guides/loading-scenes.md), and [camera controls](../guides/cameras-and-controllers.md).
4. Use the [default render pipeline](../guides/default-pipeline.md) for a scene without authoring shaders, or follow the [JSON pipeline walkthrough](../guides/json-render-pipeline.md) to control rendering yourself.
5. Look up exact calls in the [EngineAPI reference](../api/cpp/engine-api.md) and choose a focused program from the [C++ examples](../examples/cpp-examples.md).

**First checkpoint:** the example opens a window, renders the bundled scene, and responds to its documented camera controls. Run it from its source directory so its pipeline and shaders can be found.

## Build a Python application

1. Install the same native dependencies from the [build guide](../../BUILDING.md), then build the Python extension.
2. Follow the [Python quickstart](python-quickstart.md) to generate and run a starter project.
3. Learn [scene loading](../guides/loading-scenes.md) and [Python script systems](../guides/script-ecs.md).
4. Add controls or live data displays with [Canvas UI](../guides/canvas-ui.md) and the [Python toolkit](../guides/python-toolkit.md).
5. Use the [Python API reference](../api/python/engine.md) for signatures and the [Python examples](../examples/python-examples.md) for complete applications.

**First checkpoint:** the starter compiles its shaders, loads the box, and renders frames. An import of a pure-Python utility alone does not verify the native extension.

## Extend the engine or renderer

Start by building and running an application so you can observe your changes.

1. Read the [architecture overview](../architecture/overview.md) to locate application, ECS, asset, and Vulkan responsibilities.
2. Study the [facade boundary](../architecture/facade-pattern.md) and [ApplicationBase lifecycle](../api/cpp/application-base.md) before adding application hooks.
3. For rendering work, read the [frame graph architecture](../architecture/frame-graph-pipeline.md), then the [frame graph API](../api/cpp/frame-graph.md) and [GPU ownership reference](../api/cpp/gpu-types.md).
4. For gameplay work, start with the [ECS design](../architecture/ecs-design.md) and [component map](../api/cpp/ecs-components.md).
5. When changing public interfaces, follow the [documentation workflow](../maintenance.md) to keep C++ and Python references aligned.

## How to use this documentation

| Section | Use it when you need |
|---|---|
| Get started | A sequence that takes you from dependencies to a running application |
| Guides | Steps to complete one task, such as loading a scene or adding a light |
| Examples | Complete programs, launch commands, assets, and runtime previews |
| Architecture | Explanations of ownership, data flow, and engine design |
| Reference | Exact API contracts, JSON fields, types, and known limitations |
| Contributing | Documentation checks, publishing instructions, and source-of-truth rules |

If a program does not start, check the [FAQ](../faq.md) and the build guide's [troubleshooting section](../../BUILDING.md#troubleshooting).
