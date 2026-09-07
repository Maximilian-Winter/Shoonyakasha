# Architecture overview

Shoonyakasha has a native application/renderer and a facade shared by C++ callers and Cython bindings. JSON defines rendering configuration; ECS state supplies runtime values.

```text
Python application -> Cython -> Facade (Engine, Scene, Input, Physics, Ecs)
C++ application ------------> Facade or ApplicationBase subclass
                                      |
                           ApplicationBase lifecycle
                  +-------------------+------------------+
                  |                   |                  |
               ECS Scene         RenderGraph       Asset managers
             SystemManager    builder/compiler/    glTF, sprites,
             native/scripts       executor           fonts, IBL
                  |                   |
                  +-> scene context / dot-path bindings
                                      |
                                Vulkan resources
```

## Ownership and layers

ApplicationBase owns window/device/swapchain and application services. The facade's private CallbackApp forwards callbacks and adds physics/skeletal-animation integration. Scene owns its registry and system manager. RenderGraph coordinates declarations, compiled resources, runtime bindings, and execution. Mesh geometry uses shared GPU ownership so node instances can reuse allocations.

`include/Vulkan/FrameGraph` contains graph declarations and low-level compilation/execution; `include/FrameGraph` contains layout, data-binding, and entity-rendering helpers. The facade hides both from ordinary Python callers.

## Frame lifecycle

Initialization creates Vulkan/ECS/assets and a graph object, calls init, then loads/compiles the graph and calls post-init. Frame updates drive input, application/ECS state, scene context and standard buffers, pre-render, graph execution, and presentation. Post-render follows presentation. For precise scheduling, see [ApplicationBase](../api/cpp/application-base.md) and its linked implementation.

The default application uses single-command-buffer graph execution; native multi-queue support requires submission integration. Graphics configuration and available execution APIs should not be mistaken for a performance guarantee.

Continue with [frame graph](frame-graph-pipeline.md), [ECS](ecs-design.md), [facade](facade-pattern.md), and [Cython](cython-bridge.md).
