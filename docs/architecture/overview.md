# Architecture Overview

This document describes how the Shoonyakasha engine's subsystems are organized and how they work together to produce a rendered frame. It is aimed at contributors and C++ developers who want to understand the engine's internal structure.

---

## System Diagram

```
Python Script (.py)
    |
    v  (Cython bridge)
C++ Facade Layer  (PIMPL: EngineAPI, SceneAPI, InputAPI, PhysicsAPI)
    |
    v
ApplicationBase  (lifecycle management, main loop)
    |
    +--- Vulkan Backend
    |      VulkanInstance, VulkanDevice, VulkanSwapChain,
    |      VulkanCommandManager, DescriptorManager, ResourceManager
    |
    +--- ECS  (EnTT registry)
    |      Scene, SceneManager, SystemManager
    |      Systems: TransformSystem, CameraSystem, CameraControllerSystem,
    |               PhysicsSystem, SkeletalAnimationSystem, LifetimeSystem
    |
    +--- Frame Graph
    |      RenderGraph  (builder -> compiler -> executor)
    |      DotPathResolver, BufferLayoutResolver, FrameGraphRenderer
    |      SharedBufferRegistry, StagingBufferManager
    |
    +--- Physics  (Bullet3 via PIMPL wrapper)
    |      PhysicsWorld -> rigid bodies, collision shapes
    |
    +--- glTF Loader  (tinygltf -> ECS entities)
    |      GltfSceneLoader -> MeshComponent, MaterialComponentV5,
    |                         TransformComponent, SkeletonComponent
    |
    +--- IBL Generator  (HDR -> cubemaps)
           IBLGenerator -> irradiance map, prefilter map, BRDF LUT
```

The arrows represent ownership and control flow. `ApplicationBase` owns all subsystems and orchestrates the main loop. The Facade layer wraps `ApplicationBase` behind a PIMPL boundary so that Python (via Cython) never sees Vulkan, EnTT, or any internal headers.

---

## Data Flow: How a Frame Renders

Every frame follows the same sequence inside `ApplicationBase::run()`:

### 1. Time Update

```
high_resolution_clock -> deltaTime
```

The frame delta time is computed from the previous frame's timestamp.

### 2. Application Update Hook

```
onUpdate(dt)  -- virtual hook for game logic
```

The application (or Python callbacks via the Facade) runs gameplay code: moving entities, spawning projectiles, updating UI state.

### 3. ECS Systems Update

```
SystemManager::update(registry, dt)
```

Systems execute in priority order:

| Priority | System                      | Responsibility                                          |
|----------|-----------------------------|---------------------------------------------------------|
| 0        | `TransformSystem`           | Rebuild local matrices for dirty transforms, propagate world matrices through parent-child hierarchy |
| 0        | `CameraSystem`              | Update projection and view matrices from camera components |
| 0        | `CameraControllerSystem`    | Process input to move/rotate the active camera          |
| 1        | `PhysicsSystem`             | Step Bullet3 simulation, sync rigid body transforms back to ECS |
| 1        | `SkeletalAnimationSystem`   | Advance animation time, evaluate channels, compute bone matrices |
| 2        | `LifetimeSystem`            | Decrement TTL, destroy expired entities                 |

All systems operate on the shared `entt::registry`. Each system queries only the components it needs via `registry.view<>()`.

### 4. Scene Context Update

```
RenderGraph::updateSceneContext(dt)
    -> SceneContext::updateFromRegistry(registry)
```

The `SceneContext` struct is populated from the ECS registry:

- **Camera data** -- view matrix, projection matrix (Vulkan Y-flipped), inverse matrices, position, FOV, near/far planes, aspect ratio
- **Light data** -- up to 16 packed lights collected from entities with `LightComponent` + `TransformComponent`
- **Time data** -- elapsed time, delta time, frame counter
- **Screen data** -- viewport width and height
- **Custom values** -- application-defined key-value pairs accessible via `scene.custom.<key>` dot-paths

### 5. Dot-Path UBO Update

```
RenderGraph::updateDotPathUBOs(frameIndex)
```

For each `bufferLayout` declared with `updateFrequency: "per_frame"`, the engine:

1. Reads the `CompiledBufferLayout` fields
2. Uses `DotPathResolver` to resolve each field's `source` path against the `SceneContext`
3. Writes resolved values into a staging buffer
4. Uploads to the GPU UBO for the current frame index

This is how camera matrices, light arrays, time values, and custom uniforms reach the shaders -- all driven by JSON declarations, no C++ code changes needed.

### 6. Pre-Render Hook

```
onPreRender(dt)  -- virtual hook for last-minute adjustments
```

### 7. Command Buffer Recording and Execution

```
RenderGraph::execute(frameIndex, swapchainImageIndex, commandBuffer)
    -> FrameGraphExecutor::execute(...)
        For each compiled pass in topological order:
            1. Insert pre-barriers (layout transitions)
            2. Begin render pass (graphics) or skip (compute/transfer)
            3. Execute pass:
               - If geometry pass (opaque_geometry, transparent_geometry, etc.):
                   FrameGraphRenderer::executeGeometryPass(...)
                       -> queryEntities(filter, sortMode)
                       -> for each entity:
                           DotPathResolver resolves entity.* paths -> push constants
                           Bind material textures -> descriptor set
                           Bind skeleton SSBO (if skinned)
                           Bind mesh vertex/index buffers
                           vkCmdDrawIndexed
               - If fullscreen pass:
                   Bind pipeline + descriptor sets
                   vkCmdDraw(3, 1, 0, 0)  -- fullscreen triangle
               - If compute pass:
                   Bind compute pipeline + descriptor sets
                   vkCmdDispatch(groupCountX, groupCountY, groupCountZ)
               - If manual callback:
                   Invoke registered PassExecuteFn
            4. End render pass (graphics)
```

### 8. Present

```
vkQueuePresentKHR -> swapchain image displayed
```

### 9. Post-Render Hook

```
onPostRender()
```

Frame synchronization uses double-buffered semaphores and fences (`maxFramesInFlight = 2` by default).

---

## Directory Map

| Directory | Headers | Responsibility |
|-----------|---------|----------------|
| `include/App/` | `ApplicationBase.h` | Main application lifecycle, configuration, virtual hooks, convenience helpers (camera, lights, glTF loading) |
| `include/Facade/` | `EngineAPI.h`, `SceneAPI.h`, `InputAPI.h`, `PhysicsAPI.h`, `FacadeTypes.h` | Python-friendly C++ API. PIMPL hides all internal types. No Vulkan/EnTT in headers. |
| `include/ECS/` | `Core.h`, `Scene.h`, `Systems.h`, `RenderComponents.h`, `SkeletonComponents.h`, `CameraController.h`, `CameraControllerBuilders.h`, `PhysicsSystem.h`, `SkeletalAnimationSystem.h`, `InputSystem.h` | Entity components, systems, scene management. `Core.h` has TransformComponent, CameraComponent, LightComponent, HierarchyComponent. |
| `include/Vulkan/FrameGraph/` | `FrameGraph.h`, `FrameGraphPass.h`, `FrameGraphResource.h`, `FrameGraphJson.h`, `VertexFormatRegistry.h`, `FrameGraphAnalyzer.h`, `FrameGraphDebugger.h`, `FrameGraphExport.h`, `RenderTargetSaver.h` | RenderGraph (builder, compiler, executor), pass/resource declarations, JSON parsing, analysis/debug/export tools |
| `include/FrameGraph/` | `DotPathResolver.h`, `BufferLayoutCompiler.h`, `FrameGraphRenderer.h`, `EntityRenderExecutor.h`, `SharedBufferRegistry.h`, `StagingBufferManager.h` | Runtime data binding: dot-path resolution, buffer layout compilation, automatic entity rendering, cross-graph SSBO sharing, CPU-GPU staging |
| `include/Resources/` | `GltfSceneLoader.h`, `ResourceManager.h`, `AnimationData.h` | glTF scene loading (tinygltf), GPU resource caching, animation clip/channel data |
| `include/IBL/` | `IBLGenerator.h` | HDR environment map processing: irradiance convolution, prefilter mip chain, BRDF LUT generation |
| `include/GPU/` | `GPUTypes.h`, `GPUResourceFactory.h` | Thin GPU type wrappers (`GPUTexture`, `GPUBuffer`), default texture factory |
| `src/Facade/` | (implementations) | PIMPL internals for the Facade layer |
| `python/shoonyakasha/` | `.pyx`, `.pxd`, bridge headers | Cython bindings wrapping the Facade |
| `python/examples/` | `.py` scripts | Python demo applications |
| `examples/` | C++ example apps | Standalone applications demonstrating various features |
| `tests/` | GTest suites | 582 tests (518 core + 64 facade) |

---

## Key Design Decisions

### Declarative over Imperative

The rendering pipeline is defined entirely in JSON. Buffer layouts, descriptor set layouts, samplers, render passes, resource dependencies, entity data bindings, compute dispatch dimensions -- all declared, not coded. This means:

- Changing the pipeline does not require recompilation
- Artists and technical directors can iterate on rendering without touching C++
- The same JSON can be analyzed, validated, and visualized before running
- New pass types can be added by writing JSON + shaders, not new C++ classes

The `DotPathResolver` is the bridge that makes this work. JSON fields like `"source": "scene.camera.view"` or `"source": "entity.material.params.baseColorFactor"` are resolved at runtime against the ECS registry and SceneContext, filling GPU buffers automatically.

### PIMPL Everywhere

The Facade layer (`EngineAPI`, `SceneAPI`, `InputAPI`, `PhysicsAPI`) uses PIMPL (pointer to implementation) to hide all internal types:

```cpp
// EngineAPI.h -- what Python sees
class EngineAPI {
public:
    explicit EngineAPI(const EngineConfig& config);
    ~EngineAPI();
    void run();
    SceneAPI& getScene();
    // ... no Vulkan, no EnTT, no ApplicationBase in this header
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
```

This means:
- Cython `.pxd` files only need to parse simple types (`glm::vec3`, `std::string`, `float`)
- Changing engine internals does not break the Python binding ABI
- Compile times for Python bindings are fast (no transitive Vulkan/EnTT includes)

### ECS over Inheritance

The engine uses EnTT's entity-component-system architecture instead of a class hierarchy:

- **Entities** are lightweight integer handles (`entt::entity`)
- **Components** are plain structs: `TransformComponent`, `CameraComponent`, `MeshComponent`, `MaterialComponentV5`, `LightComponent`, `SkeletonComponent`, `RigidBodyComponent`, etc.
- **Systems** operate on views of components: `registry.view<TransformComponent, HierarchyComponent>()`

Benefits:
- Cache-friendly iteration (components stored contiguously per type)
- Composition over inheritance (an entity's behavior is the sum of its components)
- Easy serialization (components are simple data)
- Hot-adding/removing components at runtime

### RAII for GPU Resources

All Vulkan objects are wrapped in RAII classes:

- `VulkanBuffer` -- VkBuffer + VMA allocation, destroyed on scope exit
- `VulkanImage` -- VkImage + VkImageView + VMA allocation
- `VulkanPipeline` -- VkPipeline + VkPipelineLayout
- `VulkanDescriptorSet` -- VkDescriptorSet (pool-allocated)
- `VulkanRenderPass` -- VkRenderPass

The RenderGraph owns all compiled resources via `std::unique_ptr` and `std::shared_ptr`. When the graph is recompiled or destroyed, all GPU resources are automatically freed. No manual `vkDestroy*` calls leak into application code.

### JSON is the Engine

The core philosophy can be summarized as:

| Layer | Role |
|-------|------|
| **JSON** | Declares buffer layouts, passes, bindings, and data sources |
| **ECS** | Holds runtime data (meshes, materials, transforms, lights) |
| **RenderGraph** | Compiles JSON to Vulkan resources (once at startup) |
| **FrameGraphRenderer** | Executes automatically every frame (no manual binding code) |

This separation means the C++ engine code rarely changes. New rendering techniques are expressed as new JSON pipelines and SPIR-V shaders. The engine's job is to compile and execute whatever the JSON describes.

---

## See Also

- [Frame Graph Pipeline](frame-graph-pipeline.md) -- deep dive into compilation and execution
- [ECS Design](ecs-design.md) -- deep dive into the component system
- [Facade Pattern](facade-pattern.md) -- PIMPL facade layer between core and Python
- [Cython Bridge](cython-bridge.md) -- how Cython wraps the C++ facade
