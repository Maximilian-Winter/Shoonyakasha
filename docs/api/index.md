# Technical API reference

The C++ symbol reference is generated directly from the engine headers with Doxygen on every site build. Browse classes and structs, overloaded methods, parameters, return types, default arguments, enums, aliases, inheritance diagrams, and linked header source.

## Browse C++ symbols

| Index | What it contains |
|---|---|
| [C++ API home](https://maximilian-winter.github.io/Shoonyakasha/api/native/index.html) | Entry points, scope, and navigation back to the guides |
| [Classes and structs](https://maximilian-winter.github.io/Shoonyakasha/api/native/annotated.html) | A searchable catalog of engine types and their members |
| [Class hierarchy](https://maximilian-winter.github.io/Shoonyakasha/api/native/hierarchy.html) | Inheritance relationships between native types |
| [Namespaces](https://maximilian-winter.github.io/Shoonyakasha/api/native/namespaces.html) | APIs grouped by namespace, including the facade, ECS, UI, and frame graph |
| [Headers and source](https://maximilian-winter.github.io/Shoonyakasha/api/native/files.html) | Header reference pages and line-linked source listings |

Use the **Search** field inside the C++ reference to look up a symbol such as `EngineAPI`, `EngineConfig`, `RenderGraph`, or `VulkanBuffer`. The documentation sidebar's search covers the authored guides and contract pages; the C++ reference has its own symbol index.

## Application entry points

| You want to | Start with |
|---|---|
| Own a window and run an application | `Shoonyakasha::Facade::EngineAPI` and `EngineConfig`; [lifecycle contract](cpp/engine-api.md) |
| Create entities, transforms, cameras, lights, and materials | `Shoonyakasha::Facade::SceneAPI`; [scene contract](cpp/scene-api.md) |
| Handle keyboard and mouse | `Shoonyakasha::Facade::InputAPI`; [input contract](cpp/input-api.md) |
| Configure rigid bodies and simulation | `Shoonyakasha::Facade::PhysicsAPI`; [physics contract](cpp/physics-api.md) |
| Register script systems and components | `Shoonyakasha::Facade::EcsAPI`; [ECS contract](cpp/ecs-api.md) |
| Build screen-space and world-space UI | `Shoonyakasha::Facade::UIAPI`; [UI contract](cpp/ui-api.md) |
| Extend native application hooks | `Shoonyakasha::ApplicationBase`; [lifecycle and extension points](cpp/application-base.md) |
| Work on rendering internals | `Shoonyakasha::FrameGraph::RenderGraph`; [frame graph contract](cpp/frame-graph.md) |

## Scope and accuracy

The generated reference covers the repository's `include/` headers, including public and protected declarations. It excludes private state, vendored libraries, example applications, and `SHOONYAKASHA_TESTING`-only declarations. Conditional declarations are parsed with `NDEBUG`, so defaults reflect a release build. Source listings still show the original header text, including private implementation details.

Signatures stay aligned with the checkout automatically. Detailed prose depends on the comments written beside declarations; undocumented members remain listed, without invented behavior. The curated C++ pages explain lifecycle constraints and known limitations that signatures alone cannot express.

## Python and pipeline reference

Python applications use the same native engine through Cython. Their public method names and conversions are documented in the [Python Engine reference](python/engine.md) and the other Python pages in the sidebar. C++ signatures do not imply Python parity.

Render passes, bindings, layouts, and dot-path data sources are documented in the [pipeline JSON reference](../reference/pipeline-json.md).

To improve or build this reference, see the [API documentation workflow](../maintenance.md#technical-c-api).
