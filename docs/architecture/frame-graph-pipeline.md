# Frame Graph Pipeline

This document is a deep dive into Shoonyakasha's JSON-driven rendering pipeline: how it compiles, how it executes, and how data flows from JSON declarations through ECS components to Vulkan draw calls.

---

## The Problem

Vulkan is explicit by design. To render a single textured mesh, you must create and manage:

- Render passes with attachment descriptions and subpass dependencies
- Framebuffers sized to the swapchain
- Descriptor set layouts specifying every binding slot
- Descriptor pools and allocated descriptor sets
- Pipeline layouts combining descriptor set layouts and push constant ranges
- Graphics pipelines with vertex input state, rasterization, blending, depth/stencil, viewport, scissor
- Image layout transitions between passes (barriers)
- Synchronization primitives (semaphores, fences)

Every change to the pipeline -- adding a G-buffer output, inserting a post-process pass, switching to deferred shading -- requires touching dozens of C++ files, recompiling, and debugging synchronization issues.

---

## The Solution

Declare everything in JSON. The engine compiles it once and executes every frame.

A single JSON file defines the entire rendering pipeline:

```
JSON Pipeline File
    |
    +-- bufferLayouts       (UBO/SSBO/push constant struct definitions + dot-path sources)
    +-- entityDataBindings  (how entity data binds to shaders)
    +-- samplers            (texture filtering and addressing)
    +-- descriptorSetLayouts (binding slot definitions + auto-bind rules)
    +-- resources           (images and buffers: G-buffers, depth, HDR targets)
    +-- passes              (ordered render/compute passes with inputs, outputs, shaders)
```

The engine reads this file, creates all Vulkan objects, and executes the pipeline every frame without any manual binding code.

---

## Compilation Phase

Compilation happens once at startup (or on hot-reload) inside `RenderGraph::compile()`. It transforms declarative JSON into executable Vulkan state.

### Stage 1: JSON Parsing

```
JSON file
    |  (nlohmann-json)
    v
FrameGraphBuilder
    - ResourceDeclarations[]
    - PassDeclarations[]
    - BufferLayoutDescs[]
    - EntityDataBindingConfigs[]
    - SamplerDescs[]
    - DescriptorSetLayoutDescs[]
    - VertexFormatRegistry
```

`FrameGraphJson.h` parses the JSON and populates the `FrameGraphBuilder`. String enums like `"color_write"`, `"depth_read"`, `"combined_image_sampler"` are converted to Vulkan enums. Dot-path source strings (`"scene.camera.view"`, `"entity.transform.worldMatrix"`) are stored as-is for later resolution.

### Stage 2: Topological Sort

The compiler analyzes resource read/write dependencies between passes and produces a topological execution order:

```
Pass A writes gPosition, gNormal  -->  Pass B reads gPosition, gNormal
Pass B writes litColorHDR         -->  Pass C reads litColorHDR
```

If the graph contains cycles, compilation fails with an error message identifying the cycle. Dead passes (passes whose outputs are never read by any downstream pass or the swapchain) are culled unless they declare `hasSideEffects: true`.

### Stage 3: Physical Resource Allocation

For each declared resource:

- **Imported resources** (swapchain, external UBOs): the engine records the external handles for later binding
- **Transient images**: created with VMA as device-local images sized to the reference extent (swapchain dimensions). Format, sample count, and mip levels come from the JSON `ImageDesc`
- **Buffers**: created with VMA according to usage flags

Image dimensions specified as `0` in JSON (the default) inherit the swapchain extent, optionally scaled by `widthScale`/`heightScale`.

### Stage 4: Layout Transitions and Barrier Insertion

The compiler walks the execution order and tracks each resource's current `VkImageLayout`. When a pass requires a different layout from what the previous pass left, a `BarrierInfo` is generated:

```
BarrierInfo {
    resource:    handle to the image
    oldLayout:   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL  (left by writer)
    newLayout:   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  (needed by reader)
    srcStage:    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    dstStage:    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    srcAccess:   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    dstAccess:   VK_ACCESS_SHADER_READ_BIT
}
```

These barriers are stored in `CompiledPass::preBarriers` and inserted as `vkCmdPipelineBarrier` calls before each pass executes. The application never writes a barrier manually.

### Stage 5: Sampler Creation

Each entry in the JSON `samplers` section becomes a `VkSampler`:

```json
"linearClamp": {
    "magFilter": "linear",
    "minFilter": "linear",
    "mipmapMode": "linear",
    "addressMode": "clamp_to_edge"
}
```

Samplers support anisotropy, depth comparison (for shadow mapping), LOD control, and border color configuration.

### Stage 6: Render Pass and Framebuffer Creation

For each graphics pass in execution order:

1. Collect output attachments (color writes, depth writes) and input attachments
2. Create a `VkRenderPass` with the correct attachment descriptions, load/store ops (derived from clear values), and subpass dependencies
3. Create `VkFramebuffer`(s) binding the physical image views. Swapchain-dependent passes get one framebuffer per swapchain image; others get one

### Stage 7: Descriptor Set Layout and Allocation

For each `descriptorSetLayout` in JSON:

1. Parse binding descriptions (type, stages, count)
2. Create `VkDescriptorSetLayout`
3. Allocate `VkDescriptorSet`(s) from a pool (one per frame-in-flight for UBOs, shared for static bindings)

### Stage 8: Auto-Binding

Descriptor set bindings with `autoBindResource`, `autoBindSampler`, or `autoBindBuffer` fields are resolved at compile time:

```json
{
    "binding": 0,
    "type": "combined_image_sampler",
    "stages": ["fragment"],
    "autoBindResource": "gPosition",
    "autoBindSampler": "nearestClamp"
}
```

The compiler looks up the physical image view for `"gPosition"` and the `VkSampler` for `"nearestClamp"`, then writes the descriptor immediately. No runtime binding needed for these slots.

### Stage 9: Pipeline Creation

For each pass with a `pipeline` section in JSON:

1. Load SPIR-V shaders from the specified paths
2. Configure vertex input state from the `VertexFormatRegistry` (or `"none"` for fullscreen/compute)
3. Set rasterization state: cull mode, polygon mode (wireframe), topology
4. Set depth/stencil state: depth test, depth write
5. Set color blend state: none, alpha, or additive
6. Build the pipeline layout from the pass's descriptor set layouts + push constant ranges
7. Create `VkPipeline` (graphics) or `VkComputePipeline`

### Stage 10: Buffer Layout Compilation

Each `bufferLayout` in JSON is compiled into a `CompiledBufferLayout`:

- Field offsets are calculated according to the packing rule (`std140`, `std430`, `scalar`, `push_constant`)
- Dot-path sources are classified (`hasSceneSources`, `hasEntitySources`, `hasConstSources`)
- For `updateFrequency: "per_frame"` UBOs, the engine creates per-frame `VulkanBuffer` objects and registers them in the auto-bind system

### Stage 11: SSBO Creation and Initialization

For `storage_buffer` layouts with `elementCount > 0`:

1. Calculate element stride from fields and packing rule
2. Allocate device-local buffer via VMA (`elementCount * elementStride` bytes)
3. If `source.type` is `"initializer"`: generate initial data on CPU using the field init rules (constant values, random ranges, Gaussian distributions, grid placement, spherical distributions)
4. Upload initial data via a staging buffer
5. If `target` is specified: register the buffer in the `SharedBufferRegistry` for cross-graph sharing

### Stage 12: Multi-Queue Batch Generation

If any pass declares `queueType: "compute"`, the compiler generates `QueueSubmitBatch` with:

- Separate batches for graphics and compute queues
- Timeline semaphore sync points between dependent batches
- Queue ownership transfer barriers for shared resources

### Compilation Result

The `CompileResult` struct contains everything needed for execution:

```cpp
struct CompileResult {
    vector<uint32_t>        executionOrder;       // Topologically sorted pass indices
    vector<CompiledPass>    compiledPasses;       // Vulkan objects per pass
    vector<PhysicalResource> physicalResources;   // Images and buffers
    map<string, VkSampler>  samplers;             // Named samplers
    map<string, shared_ptr<VulkanDescriptorSet>> namedDescriptorSets;
    map<string, CompiledBufferLayout> bufferLayouts;
    QueueSubmitBatch        queueBatches;         // Multi-queue schedule
};
```

---

## Execution Phase

Execution happens every frame inside `RenderGraph::execute()`. No Vulkan objects are created during execution (only during compilation).

### Step 1: Scene Context Update

```cpp
renderGraph.updateSceneContext(deltaTime);
```

The `SceneContext` is refreshed from the ECS registry:

- Camera: view, projection (Y-flipped), inverses, position, FOV, near/far, aspect
- Lights: up to 16 packed lights from `LightComponent` entities
- Time: elapsed, delta, frame counter
- Screen: viewport dimensions
- Custom: application-defined values (set via `setCustomFloat`, `setCustomVec3`, etc.)

### Step 2: Dot-Path UBO Update

For each `bufferLayout` with `updateFrequency: "per_frame"`:

```
CompiledBufferLayout.fields[]
    |
    v  (DotPathResolver)
"scene.camera.view"       -> mat4 from SceneContext.cameraView
"scene.lights[0].colorIntensity" -> vec4 from SceneContext.lights[0].colorIntensity
"scene.time.delta"        -> float from SceneContext.timeDelta
"scene.custom.particles.gravity" -> float from SceneContext.customValues["particles.gravity"]
    |
    v  (BufferLayoutResolver.fillSceneBuffer)
Write resolved values at computed offsets into mapped buffer memory
    |
    v
VkBuffer updated for current frame index
```

This is how uniform data reaches the GPU. The JSON declares the buffer structure and data sources; the engine resolves and uploads automatically.

### Step 3: Pass Execution Loop

For each pass index in `executionOrder`:

#### 3a. Pre-Barriers

```cpp
vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
    0, nullptr,                    // memory barriers
    0, nullptr,                    // buffer barriers
    imageBarrierCount, imageBarriers);  // image layout transitions
```

Layout transitions computed during compilation are executed. This ensures each resource is in the correct `VkImageLayout` before the pass reads or writes it.

#### 3b. Begin Render Pass (Graphics Only)

```cpp
vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
vkCmdSetViewport(cmd, 0, 1, &viewport);
vkCmdSetScissor(cmd, 0, 1, &scissor);
```

#### 3c. Execute Pass Content

The pass's `execution.type` determines what happens:

**`opaque_geometry` / `transparent_geometry` / `skinned_geometry` / `shadow_casters`**

The `FrameGraphRenderer` handles these automatically:

1. **Query**: iterate the ECS registry for entities with `MeshComponent` + `MaterialComponentV5` + `TransformComponent` + `RenderableTagComponent`
2. **Filter**: apply the entity filter (opaque excludes skinned and transparent; transparent excludes opaque and skinned; shadow_casters requires `castShadows = true`)
3. **Sort**: front-to-back for opaque (minimizes overdraw), back-to-front for transparent (correct alpha blending), none for shadow casters
4. **Draw**: for each filtered entity, bind and draw (see Entity Data Binding Flow below)

**`fullscreen`**

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
// Bind descriptor sets (auto-bound at compile time)
vkCmdBindDescriptorSets(cmd, ...);
vkCmdDraw(cmd, 3, 1, 0, 0);  // fullscreen triangle
```

Used for deferred lighting, tone mapping, post-processing, and any pass that reads textures and writes to a render target without geometry input.

**`compute_dispatch`**

```cpp
vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, ...);
vkCmdDispatch(cmd, groupCountX, groupCountY, groupCountZ);
```

Dispatch dimensions can be:
- Fixed values from JSON
- Derived from a resource's dimensions (e.g., image width / workgroup size)
- Derived from a named parameter (e.g., `particleCount / 256`)

**`draw`**

A parametric draw call. Vertex count and instance count can come from fixed values or named parameters:

```json
"execution": {
    "type": "draw",
    "vertexCount": { "parameter": "particleCount" },
    "instanceCount": 1
}
```

**Manual callback**

If a pass has a registered `PassExecuteFn`, it receives a `PassExecuteContext` with the command builder, pipeline, descriptor sets, and physical resource views. The callback records whatever Vulkan commands it needs.

#### 3d. End Render Pass

```cpp
vkCmdEndRenderPass(cmd);
```

---

## Entity Data Binding Flow

This is the most intricate part of the pipeline. It describes how per-entity data (model matrices, material properties, textures, bone matrices) reaches the GPU without any manual binding code.

### JSON Declaration

```json
"entityDataBindings": {
    "pbrOpaque": {
        "perDraw":   { "layoutRef": "MaterialPushConstants" },
        "material":  { "layoutRef": "materialSet" },
        "skeleton":  { "layoutRef": "skeletonSet" }
    }
}
```

This tells the engine: for each entity drawn in a pass that references `"pbrOpaque"`, use the `MaterialPushConstants` buffer layout for push constants, the `materialSet` descriptor set layout for textures, and the `skeletonSet` layout for bone matrices.

### Per-Draw Push Constants

The `MaterialPushConstants` buffer layout declares fields with entity-scoped dot-path sources:

```json
"MaterialPushConstants": {
    "usage": "push_constant",
    "fields": [
        { "name": "model",            "type": "mat4",  "source": "entity.transform.worldMatrix" },
        { "name": "baseColorFactor",   "type": "vec4",  "source": "entity.material.params.baseColorFactor" },
        { "name": "metallicFactor",    "type": "float", "source": "entity.material.params.metallicFactor" },
        { "name": "roughnessFactor",   "type": "float", "source": "entity.material.params.roughnessFactor" },
        { "name": "hasNormalMap",      "type": "float", "source": "entity.material.textures.normalMap.exists" },
        { "name": "alphaCutoff",       "type": "float", "source": "entity.material.alphaCutoff" }
    ]
}
```

For each entity drawn, the `BufferLayoutResolver` resolves every field:

```
DotPathResolver::resolveEntity("entity.transform.worldMatrix", entity, registry)
    -> reads TransformComponent.worldMatrix -> glm::mat4

DotPathResolver::resolveEntity("entity.material.params.baseColorFactor", entity, registry)
    -> reads MaterialComponentV5.params["baseColorFactor"] -> glm::vec4

DotPathResolver::resolveEntity("entity.material.textures.normalMap.exists", entity, registry)
    -> checks if MaterialComponentV5.textures["normalMap"] is valid -> float (1.0 or 0.0)
```

The resolved values are written at the correct byte offsets into a push constant buffer, then pushed:

```cpp
vkCmdPushConstants(cmd, pipelineLayout, stages, offset, size, buffer);
```

### Per-Material Textures

The `materialSet` descriptor set layout declares texture bindings:

```json
"materialSet": {
    "bindings": [
        { "binding": 0, "type": "combined_image_sampler", "stages": ["fragment"], "name": "albedoMap" },
        { "binding": 1, "type": "combined_image_sampler", "stages": ["fragment"], "name": "normalMap" },
        { "binding": 2, "type": "combined_image_sampler", "stages": ["fragment"], "name": "metallicRoughnessMap" }
    ]
}
```

For each entity, the engine reads `MaterialComponentV5.textures` and binds each named texture to the corresponding descriptor set slot. Missing textures fall back to default textures (1x1 white for albedo, flat normal for normal maps, etc.) created by `GPUResourceFactory`.

Descriptor sets are cached per entity per frame to avoid redundant allocations:

```
Cache key: (entity_id, layout_hash, frame_index) -> VkDescriptorSet
```

### Per-Entity Skeleton SSBO

For skinned entities (those with a `SkeletonComponent`), bone matrices are uploaded to a storage buffer and bound via the `skeletonSet` descriptor set:

```
SkeletonComponent.boneMatrices[] (CPU)
    |  (memcpy to mapped SSBO)
    v
VkBuffer bound at descriptor set binding
    |
    v
Vertex shader reads bone matrices for skinning
```

### Complete Per-Entity Binding Sequence

For each entity in a geometry pass:

```
1. Fill push constant buffer from entity components via DotPathResolver
2. vkCmdPushConstants(...)
3. Get or create material texture descriptor set
4. vkCmdBindDescriptorSets(..., materialSet, ...)
5. If skinned: update and bind skeleton SSBO descriptor set
6. vkCmdBindVertexBuffers(..., mesh.vertexBuffer, ...)
7. vkCmdBindIndexBuffer(..., mesh.indexBuffer, ...)
8. vkCmdDrawIndexed(mesh.indexCount, 1, mesh.firstIndex, mesh.vertexOffset, 0)
```

---

## Resource Lifecycle

Resources flow through the graph in a producer-consumer pattern:

```
Resource created (compilation)
    |
    v
Written by Pass A  (VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    |
    v  [automatic barrier: color_write -> shader_read]
    |
Read by Pass B     (VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    |
    v  [automatic barrier: shader_read -> color_write]
    |
Written by Pass C  (VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    |
    v  [automatic barrier: color_write -> present]
    |
Presented           (VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
```

The engine tracks the current layout of every resource and inserts the minimal set of barriers needed. Resources can be:

- **Written once, read many times**: a G-buffer texture written in the geometry pass, read by the lighting pass and the transparency pass
- **Written and read in sequence**: an HDR color target written by the lighting pass, blended by the transparency pass, read by the tone map pass
- **Ping-ponged between passes**: an SSBO written by a compute pass, read by the next compute pass (e.g., particle simulation double-buffering)

### Buffer Data Flow

UBOs and SSBOs follow a similar lifecycle:

| Buffer Type | Created | Written | Read | Updated |
|-------------|---------|---------|------|---------|
| Dot-path UBO (`per_frame`) | Compilation | CPU every frame via DotPathResolver | GPU shader reads | Automatic |
| Dot-path UBO (`once`) | Compilation | CPU once at creation | GPU shader reads | Never |
| Dot-path SSBO | Compilation | CPU once (initialization), then GPU compute writes | GPU shader reads | GPU only (or readback) |
| External UBO | Application code | Application code | GPU shader reads | Manual |
| External SSBO | Application code | Application code or GPU | GPU shader reads/writes | Manual or GPU |

### Cross-Graph Sharing

The `SharedBufferRegistry` enables data flow between multiple `RenderGraph` instances:

```
Graph A (compute simulation)
    |
    +-- bufferLayout "particleSSBO" with target: "particles.currentState"
    |       |
    |       v  (after compile)
    |   SharedBufferRegistry.registerBuffer("particles.currentState", ...)
    |
Graph B (rendering)
    |
    +-- bufferLayout "particleData" with source: { "type": "buffer_ref", "ref": "particles.currentState" }
            |
            v  (at compile time)
        SharedBufferRegistry.getBuffer("particles.currentState") -> VkBuffer
```

This allows a compute-only graph to simulate particles while a separate rendering graph draws them, all declared in JSON.

---

## Compute Integration

Compute passes integrate seamlessly into the frame graph:

### Declaration

```json
{
    "name": "ParticleSimulate",
    "type": "compute",
    "execution": {
        "type": "compute_dispatch",
        "workgroupSize": [256, 1, 1],
        "dispatch": {
            "x": { "parameter": "particleCount", "divisor": 256 },
            "y": 1,
            "z": 1
        }
    },
    "pipeline": {
        "computeShader": "shaders/particle_sim.comp.spv"
    },
    "descriptorSets": ["particleComputeSet"],
    "hasSideEffects": true
}
```

### SSBO as Storage Buffer

An SSBO bound as `"storage_buffer"` in one pass can be written by a compute shader:

```json
"particleComputeSet": {
    "bindings": [
        { "binding": 0, "type": "storage_buffer", "stages": ["compute"],
          "autoBindBuffer": "particleSSBO" }
    ]
}
```

### SSBO as Read-Only in Subsequent Pass

The same buffer bound as `"storage_buffer"` (read-only by shader convention) in a rendering pass:

```json
"particleRenderSet": {
    "bindings": [
        { "binding": 0, "type": "storage_buffer", "stages": ["vertex"],
          "autoBindBuffer": "particleSSBO" }
    ]
}
```

The compiler inserts the appropriate buffer memory barrier between the compute write and the vertex read:

```
srcStage:  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
dstStage:  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
srcAccess: VK_ACCESS_SHADER_WRITE_BIT
dstAccess: VK_ACCESS_SHADER_READ_BIT
```

### Parameter-Driven Dispatch

Dispatch dimensions can reference named parameters set at runtime:

```cpp
// C++
renderGraph.setParameter("particleCount", uint32_t(50000));

// Python
engine.scene.set_render_parameter_uint("particleCount", 50000)
```

The executor reads the parameter value and divides by the workgroup size to compute the dispatch group count: `ceil(50000 / 256) = 196 groups`.

### Multi-Queue Compute

Passes with `queueType: "compute"` are scheduled on the dedicated compute queue (if available). The compiler generates timeline semaphore sync points so that:

1. Compute passes on the compute queue can overlap with graphics passes on the graphics queue
2. A graphics pass that reads a compute pass's output waits on the correct timeline value
3. Queue ownership transfers are inserted for shared resources

---

## Readback and Persistence

### GPU to CPU Readback

SSBOs and render targets can be read back to the CPU:

```json
"target": {
    "name": "particles.currentState",
    "readback": {
        "frequency": "every_n_frames",
        "n": 120,
        "callback": true
    }
}
```

The engine uses a ring of staging buffers (one per frame-in-flight) to avoid stalling the GPU. When a readback completes, the registered callback receives a `ReadbackResult` with a pointer to the mapped data, element count, and stride.

### GPU to Disk Persistence

Buffers can be saved to disk:

```json
"save": {
    "path": "output/particles.bin",
    "trigger": "every_n_frames",
    "n": 600
}
```

Render targets can be saved as images (PNG, JPG, HDR) via `renderGraph.saveRenderTarget("litColorHDR", "screenshot.png")`.

---

## Summary: The Complete Picture

```
JSON Pipeline File
    |
    v  [parse]
FrameGraphBuilder (declarations)
    |
    v  [compile]
FrameGraphCompiler
    |-- topological sort
    |-- physical resource allocation (VMA)
    |-- barrier computation
    |-- sampler creation
    |-- render pass + framebuffer creation
    |-- descriptor set layout + allocation
    |-- auto-binding (resources -> descriptor sets)
    |-- pipeline creation (SPIR-V -> VkPipeline)
    |-- buffer layout compilation (dot-path source classification)
    |-- SSBO creation + initialization
    |-- multi-queue batch generation
    |
    v
CompileResult (all Vulkan objects ready)
    |
    v  [every frame]
FrameGraphExecutor
    |-- update SceneContext from ECS
    |-- update dot-path UBOs (DotPathResolver -> BufferLayoutResolver -> VkBuffer)
    |-- for each pass:
    |       insert barriers
    |       begin render pass (graphics)
    |       execute: geometry / fullscreen / compute / callback
    |       end render pass
    |-- process readbacks + saves
    |
    v
Vulkan present
```

---

## See Also

- [Architecture Overview](overview.md) -- high-level system diagram and design decisions
- [ECS Design](ecs-design.md) -- entity-component-system patterns and data flow
- [Facade Pattern](facade-pattern.md) -- PIMPL facade layer between core and Python
- [Cython Bridge](cython-bridge.md) -- how Cython wraps the C++ facade
