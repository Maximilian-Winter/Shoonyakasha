# Shoonyakasha Frame Graph Core — Engineering Review

## Scope

Read in full:

- `include/Vulkan/FrameGraph/`: `FrameGraph.h` (1345), `FrameGraphPass.h` (361), `FrameGraphResource.h` (104), `FrameGraphAnalyzer.h` (448), `FrameGraphDebugger.h` (310), `FrameGraphExport.h` (178), `FrameGraphJson.h` (92), `RenderTargetSaver.h` (63), `VertexFormatRegistry.h` (87)
- `src/Vulkan/FrameGraph/`: `FrameGraphCompiler.cpp` (1709), `FrameGraphExecutor.cpp` (694), `FrameGraphBuilder.cpp` (264), `FrameGraphJson.cpp` (1456), `RenderGraph.cpp` (2468), `FrameGraphAnalyzer.cpp` (1173), `VertexFormatRegistry.cpp` (126)

Read substantially (structure + all decision logic; string-formatting bodies skimmed):
`FrameGraphExport.cpp` (1058), `FrameGraphDebugger.cpp` (636, lines 1–330 in full), `RenderTargetSaver.cpp` (509, lines 1–240 in full).

Read as cross-reference (outside scope, needed to verify claims): `src/Vulkan/VulkanCommandBuffer.cpp` (barrier implementation), `src/Vulkan/VulkanImage.cpp` (image create info), `src/Vulkan/VulkanPipeline.cpp` / `VulkanComputePipeline.cpp` (shader loading), `src/Vulkan/VulkanRenderPass.cpp`, `src/App/ApplicationBase.cpp` (resize path).

Reference material: `examples/declarative_sponza_test/pbr_ibl_pipeline_v3.json`, `examples/bloom_test/bloom_pipeline.json`, `docs/guides/json-render-pipeline.md`, `docs/architecture/frame-graph-pipeline.md`.

Read-only review. No files modified except this report.

---

## Architecture (traced)

### Phase boundaries

There are four objects with clean-ish separation and one god object:

| Object | File | Role |
|---|---|---|
| `FrameGraphBuilder` | `FrameGraphBuilder.cpp` | Pure data store: vectors + name→index maps. No logic beyond duplicate-name rejection. |
| `FrameGraphCompiler` | `FrameGraphCompiler.cpp` | Stateless (one `Logger*` member). Declarations → `CompileResult`. |
| `FrameGraphExecutor` | `FrameGraphExecutor.cpp` | Stateless. Records a `CompileResult` into a `VkCommandBuffer`. |
| `RenderGraph` | `RenderGraph.cpp` | Owns everything else + UBO/SSBO/staging/readback/save/ECS-binding/descriptor-cache. God object. |

### Trace: JSON → Vulkan

**1. Parse** — `RenderGraph::loadFromFile` (`RenderGraph.cpp:102`) → `loadGraphFromFile` (`FrameGraphJson.cpp:1197`) → `loadGraphFromJson` (`FrameGraphJson.cpp:381`).

Sections are parsed in a fixed order that encodes forward-reference constraints: `samplers` → `vertexFormats` → `uniformBuffers` → `entityDataBindings` → `bufferLayouts` → `descriptorSetLayouts` → `resources` → `passes`. Passes are parsed last because `parseResourceAccess` (`FrameGraphJson.cpp:342`) resolves resource names to `ResourceHandle` through `builder.getResource()` at parse time (`:349`), so a pass may only reference already-declared resources. `builder.clear()` runs first (`:382`).

**2. Build** — the builder just accumulates. `ResourceHandle` is a bare `uint32_t` index into `m_resources` (`FrameGraphResource.h:22`); `PassDeclaration` holds `inputs`/`outputs` as `ResourceAccess{handle, usage, clearValue}` (`FrameGraphPass.h:151`).

**3. Compile** — `RenderGraph::compile` (`RenderGraph.cpp:1408`) does: `applyCallbacks()` → `createManagedUBOs()` → `buildImportMap()` → `FrameGraphCompiler::compile()` → then post-steps `createDotPathUBOs` / `createDotPathSSBOs` / `registerTargets` / `resolveBufferRefs` / `createStagingBuffers` / `bindUBOsToDescriptorSets` / `setupAutoGeometryRenderers`.

`FrameGraphCompiler::compile` (`FrameGraphCompiler.cpp:830`) runs 11 numbered stages:

1. `topologicalSort` (`:133`) — Kahn's algorithm.
2. `cullDeadPasses` (`:235`) — backward reachability.
3. `createPhysicalResources` (`:351`) — `VulkanImage`/`VulkanBuffer` allocation; usage flags OR-ed from all passes touching the resource (`:362-372`).
   - 3.5 `createSamplers` (`:1352`).
4. Init `CompiledPass[i].declIndex = i` (`:884`), i.e. `compiledPasses` is 1:1 with declarations, and `executionOrder` holds declaration indices.
   - 4.5 Resolve `execution.entityDataBinding` name → `EntityDataBindingConfig` (`:889-913`).
5. `resolveLayoutsAndInsertBarriers` (`:480`).
6. `createRenderPasses` (`:565`) — one `VkRenderPass` per graphics pass, always exactly one subpass (`:720`).
7. `createFramebuffers` (`:761`).
8. `createDescriptorSetLayouts` (`:972`) — one `VulkanDescriptorSet` per named layout, shared across all passes referencing it.
   - 8.5 `performAutoBindings` (`:1416`) — resolves `autoBindResource` + `autoBindSampler` into image writes for every frame slot.
9. `createPipelines` (`:1115`) — graphics and compute.
10. `generateQueueBatches` (`:1292`).
11. `compileBufferLayouts` (`:1596`) — computes field offsets and total size.

`result.valid = true` is set unconditionally at `:961`. Only two things can fail compilation: empty pass list (`:849`) and topological-sort cycle (`:859`). Pipeline creation failure is caught and downgraded to a warning (`:1277-1282`), as is compute-pipeline failure (`:1161-1165`); sampler creation failure just `continue`s (`:1399-1400`). So `compile()` routinely returns `valid == true` for a graph that will draw nothing.

**4. Analyze** — optional and entirely off the execution path. `RenderGraph::analyze` (`RenderGraph.cpp:1798`) lazily builds a `FrameGraphAnalyzer` and caches an `AnalysisResult`. Note the analyzer **re-implements** topological sort and culling independently (`FrameGraphAnalyzer.cpp:459-578`, `:748-821`) rather than reusing the compiler, and the two implementations differ (see Findings F7).

**5. Execute** — `RenderGraph::execute` (`RenderGraph.cpp:1507`): `applyImports(swapchainImageIndex)` (patches per-swapchain-image `VkImage`/`VkImageView` into `physicalResources` and resets `currentLayout` to `UNDEFINED` each frame, `:1683-1703`) → staging readback processing → staging upload recording → `FrameGraphExecutor::execute` → staging readback recording.

`FrameGraphExecutor::execute` (`FrameGraphExecutor.cpp:89`) walks `executionOrder` and per pass: skip if `!enabled` → debug label → **heuristic compute→graphics `VkMemoryBarrier`** (`:141-158`) → acquire barriers → pre-barriers → build `PassExecuteContext` → `vkCmdBeginRenderPass` → bind pipeline + dynamic viewport/scissor → dispatch to `executeFn` (manual, highest priority) or `executeAutoCallback` → `vkCmdEndRenderPass`.

`executeAutoCallback` (`:528`) binds descriptor sets by index, pushes named parameters into push constants, then branches on `execution.type` string: `fullscreen` / `draw` / `compute_dispatch` / `compute_image` / the geometry family (delegated to `sceneRendererFn`).

### Observations on the shape

- **Compilation is total, not incremental.** `recompile` (`RenderGraph.cpp:1462`) tears down and reruns the whole thing. There is no caching of any kind — no `VkPipelineCache` (grep across `src/Vulkan` and `include/Vulkan`: zero hits), no render-pass reuse, no shader-module cache. On every window resize every pipeline is recreated from scratch.
- **`ResourceHandle` is positional.** Because handles are indices into a vector that `builder.clear()` resets, and `parseResourceAccess` resolves at parse time, the graph is fully static after load. Nothing supports adding/removing passes at runtime except `PassDeclaration::enabled` (`FrameGraphPass.h:354`), which is honoured only in `execute()` (`FrameGraphExecutor.cpp:120`) and *not* in `executePasses()` — the multi-queue path.
- **Dependency edges are derived from declaration order, not from the graph.** `findPreviousWriter` (`FrameGraphCompiler.cpp:156-165`) only considers writers with a lower declaration index. See F4.

---

## JSON schema inventory

Derived exclusively from `FrameGraphJson.cpp:381-1194`. This is the complete set of keys the loader reads.

### Top level

| Key | Read at | Notes |
|---|---|---|
| `samplers` | `:385` | object, keyed by name |
| `vertexFormats` | `:423` | object, keyed by name |
| `uniformBuffers` | `:444` | object, keyed by name |
| `entityDataBindings` | `:494` | object, keyed by name |
| `bufferLayouts` | `:560` | object, keyed by name |
| `descriptorSetLayouts` | `:872` | object, keyed by name |
| `resources` | `:906` | **array** |
| `passes` | `:1017` | **array**, order is semantically significant |

`version` and `name` appear in both example files (`pbr_ibl_pipeline_v3.json` has `"version": 3`, `bloom_pipeline.json` has `"version": 2`) and are documented as required, but **the loader never reads either**. Any other unrecognised top-level key is silently ignored.

### `samplers.<name>`
`magFilter`, `minFilter`, `mipmapMode`, `addressMode` (shorthand for U/V/W), `addressModeU`/`V`/`W`, `borderColor`, `anisotropyEnable` (alias `anisotropy`), `maxAnisotropy`, `compareEnable`, `compareOp`, `minLod`, `maxLod`, `mipLodBias`. (`:392-415`)

Accepted enum strings: filter `linear|nearest` (`:275`); mipmap `linear|nearest` (`:281`); address `repeat|mirrored_repeat|clamp_to_edge|clamp_to_border|mirror_clamp_to_edge` (`:288`); border `float_transparent_black|int_transparent_black|float_opaque_black|int_opaque_black|float_opaque_white|int_opaque_white` plus shorthands `transparent_black|opaque_black|opaque_white|white|black` (`:301`); compare op the 8 standard names (`:321`). All of these **throw** on an unknown string.

Note `unnormalizedCoordinates` is hardcoded `VK_FALSE` (`FrameGraphCompiler.cpp:1395`) and there is no `mipmapMode`-independent `maxLod` default — `maxLod` defaults to `0.0`, meaning mip sampling is effectively disabled unless explicitly set.

### `vertexFormats.<name>`
`attributes[]` with `name` (required), `type` (required), `location` (required). (`:429-436`)

Types (`VertexFormatRegistry.cpp:14-30`): `float`, `vec2..4`, `int`, `ivec2..4`, `uint`, `uvec2..4`, `unorm4`. Unknown type → `VK_FORMAT_UNDEFINED` and size 0, **silently**. Offsets are auto-packed tightly in declaration order (`VertexFormatRegistry.cpp:58-65`); explicit offsets, multiple bindings, and `VK_VERTEX_INPUT_RATE_INSTANCE` are not expressible.

### `uniformBuffers.<name>`
`size`, `perFrame` (default true), `frameworkManaged` (default **true**), `fields[]` with `name`/`type`/`offset`/`size`. Field size auto-derived for `float|int|uint`=4, `vec2`=8, `vec3`=12, `vec4`=16, `mat4`=64, everything else 4. (`:451-484`)

### `entityDataBindings.<name>`
- `perDraw`: `method`, `offset`, `size` (default 64), `stages[]`, `set`, `binding`, `layoutRef` (legacy alias `layout`) (`:502-525`)
- `material`: `method`, `set` (default 1), `bindings` (map name→binding index), `layoutRef` (`:529-545`)
- `skeleton`: `layoutRef` (`:549-552`)

### `bufferLayouts.<name>`
- `usage`: `push_constant|uniform_buffer|storage_buffer|descriptor_set` (`:568`). **Unknown value silently keeps the `uniform_buffer` default** — no `else` branch.
- `packing`: `std140|std430|scalar|push_constant` (`:580`). Unknown silently keeps `std140`.
- `updateFrequency`: `per_frame|every_n_frames|on_change|once|manual` (`:592`), plus `updateFrequencyN`. Unknown → `manual`.
- `binding`: `set`, `binding`, `offset`, `stages[]` (`:607-618`)
- `fields[]`: `name` (required), `type`, `arrayCount`, `offset`, `source` (dot-path) (`:622-654`). Types: `float|double|int|uint|bool|vec2..4|ivec2..4|uvec2..4|mat2|mat3|mat4`. **Unknown type silently defaults to `float`** (`:634-651` has no `else`).
- `textures[]`: `name` (required), `binding`, `stages[]` (`:658-672`)
- `elementCount` (`:676`)
- `source`: `type` (`none|initializer|buffer_ref|file`), `seed`, `ref` + `frequency` (buffer_ref), `path` (file), and `fields.<fieldName>` with one of `randomRange{min,max}` / `constant[]` / `gaussian{mean,stddev}` / `grid{dimensions,origin,spacing,w}` / `sphere{center,radius,mode,w}` (`:681-768`)
- `target`: string form, or object `{name, readback{frequency,n,callback,ringDepth}, save{path,trigger,n,autoCreateDirectories}}` (`:772-828`)
- `memory`: `location` (`device_local|host_visible|host_coherent`), `staging` (`auto|persistent|none`), `transferDirection` (`gpu_only|cpu_to_gpu|gpu_to_cpu|bidirectional`) (`:831-865`)

### `descriptorSetLayouts.<name>`
`bindings[]` with `binding` (required), `type` (required), `count`, `name`, `stages[]`, `autoBindResource`, `autoBindSampler`, `autoBindBuffer`. (`:880-897`)

Descriptor types (`:222`): `uniform_buffer`, `uniform_buffer_dynamic`, `storage_buffer`, `storage_buffer_dynamic`, `combined_image_sampler`, `sampled_image`, `storage_image`, `input_attachment`, `sampler`. Throws on unknown.
Shader stages (`:195`): `vertex`, `fragment`, `compute`, `geometry`, `tess_control`, `tess_eval`, `all`. Throws on unknown.

### `resources[]`
`name` (required), `kind` (required, `image|buffer`), `imported`.
- `image`: `width`, `height`, `widthScale`, `heightScale`, `mipLevels`, `arrayLayers`, `transient`, `format`, `samples` (`:923-936`)
- `buffer`: `size`, `persistentlyMapped` (`:948-949`)
- `target` (string or `{name}`), `readback{frequency,n,callback,ringDepth}`, `save{path,trigger,n,autoCreateDirectories}` — accepted at resource level or nested inside `target` (`:960-1011`)

**`samples`, `mipLevels`, `arrayLayers`, `transient`, `persistentlyMapped` are parsed and round-tripped but have zero effect.** See F9.

Formats (`:33-84`): 33 entries including R8/R16/R32 UNORM/SNORM/SFLOAT/UINT/SINT families, sRGB, BGRA, and 5 depth formats. Throws on unknown. Notably absent: all block-compressed (BC/ETC/ASTC), `A2B10G10R10`, `B10G11R11_UFLOAT` (the standard HDR-light format), `E5B9G9R9`.

### `passes[]`
`name` (required), `type` (required, `graphics|compute|transfer`), `queue` (`graphics|compute` — note the **key is `queue`, not `queueType`**, `:1024`), `inputs[]`, `outputs[]`, `pipeline{}`, `descriptorSets[]`, `pushConstants` (array or single object), `execution{}`, `hasSideEffects`, `enabled`.

- `inputs`/`outputs` entries: `resource` (required), `usage` (required), `clear` (4-array for color, or `{depth,stencil}` object) (`:342-375`). Integer/unsigned clear values are not expressible.
- `usage` strings (`:110-126`): `color_write`/`color_attachment_write`, `color_blend`/`color_attachment_blend`, `depth_write`/`depth_stencil_write`, `depth_read`, `shader_read`, `shader_read_write`, `storage_image_write`, `input_attachment`, `transfer_src`, `transfer_dst`, `present`. Throws on unknown.
- `pipeline`: `vertexShader`, `fragmentShader`, `computeShader`, `depthTest`, `depthWrite`, `cullMode`, `blending`, `topology`, `vertexInput`, `wireframe`, and when `blending == "custom"`: `srcColorFactor`, `dstColorFactor`, `colorBlendOp`, `srcAlphaFactor`, `dstAlphaFactor`, `alphaBlendOp` (`:1045-1063`)
- `execution`: `type`, `bindDescriptorSets`, `bindPipeline`, `instanceCount`, `firstVertex`, `firstInstance`, `vertexCount` (number or `{parameter,resource,dimension,divisor}`), `workgroupSize[3]`, `dispatch{x,y,z}` (each number or `{value,resource,dimension,divisor,parameter}`), `sortMode`, `entityDataBinding`, `renderLayerMask`, `lightIndex` (`:1123-1183`)

`execution.bindPipeline` is parsed (`:1127`) and **never read anywhere** — pipeline binding is unconditional in the executor (`FrameGraphExecutor.cpp:278-286`).

### Error handling

Malformed JSON: `nlohmann::json` `operator>>` throws `json::parse_error` (`FrameGraphJson.cpp:1245`). Missing required keys: `.at()` throws `json::out_of_range`. Bad enum strings: `std::runtime_error` with the offending value. **None of these are caught** — `loadGraphFromFile` propagates, `RenderGraph::loadFromFile` propagates (`RenderGraph.cpp:102-108`), so the application sees an exception. That is defensible, but the messages carry no file/line/JSON-pointer context (nlohmann's parse errors do; the hand-rolled enum errors do not).

There is no schema validation: unknown keys anywhere are ignored, and the three "silently default" cases above (`bufferLayouts.usage`, `.packing`, `fields[].type`) turn typos into wrong-sized buffers rather than errors.

`loadGraphFromJson` calls `builder.clear()` (`:382`), but `FrameGraphBuilder::clear` (`FrameGraphBuilder.cpp:246-261`) does **not** clear `m_vertexFormats`. Loading a second graph into the same builder leaks the previous graph's vertex formats.

### Round-tripping

`saveGraphToJson` (`FrameGraphJson.cpp:1289`) writes `version: 1` and serialises **only** `descriptorSetLayouts`, `resources`, and `passes`. It drops `samplers`, `vertexFormats`, `uniformBuffers`, `entityDataBindings`, `bufferLayouts`, all `autoBind*` fields, `execution`, `hasSideEffects`, `enabled`, `target`/`readback`/`save`, and push-constant `bindings`. Save→load is heavily lossy; the API name suggests otherwise.

---

## Features

Verified present and working:

- **Topological ordering with cycle detection.** Kahn's algorithm; on failure the error names every pass with residual in-degree (`FrameGraphCompiler.cpp:217-226`). The message names all cycle *participants*, not the cycle itself, but it is actionable.
- **Dead-pass culling** from `Present` outputs, imported-resource writes, and `hasSideEffects` roots (`:284-334`).
- **Automatic image layout tracking** across the execution order with barrier emission on change (`:480-559`).
- **Automatic render pass + framebuffer construction**, including MRT (`colorAttachmentCount` counted at `:1244-1254`), per-swapchain-image framebuffer arrays (`:787`), and read-only depth attachments (`:686-716`).
- **Automatic descriptor set layout + set creation**, shared by name across passes, with compile-time auto-binding of graph images + samplers (`:1416-1504`).
- **Automatic graphics and compute pipeline creation** from `PipelineDesc`, with per-pass manual override (`:1176-1184`).
- **Declarative vertex formats** (`VertexFormatRegistry`).
- **Declarative buffer layouts** with std140 offset computation and dot-path sources bound to ECS/scene data.
- **Declarative SSBO initialisation**: constant, uniform random, gaussian, 3D grid, sphere surface/volume, binary file load, and cross-graph `buffer_ref` (`RenderGraph.cpp:299-511`).
- **GPU→CPU readback and GPU→disk save** for SSBOs, UBOs, and render-target images, with ring-buffered staging and frequency policies.
- **Render target screenshot** to PNG/JPG/BMP/TGA/HDR with format conversion including half-float decode (`RenderTargetSaver.cpp:28-67`).
- **Static analysis + export**: DOT/Graphviz, JSON, markdown reports, culling reasons, barrier tables, resource lifetimes, alias-opportunity detection (reporting only).
- **Runtime debugger** with pass CPU timing, execution assertions, event callbacks, 120-frame history.
- **Shader loading**: SPIR-V binary only, read from a file path at compile time (`VulkanPipeline.cpp:498-513`, `VulkanComputePipeline.cpp:150`). No runtime GLSL compilation, no `shaderc`/`glslang` dependency, no shader reflection, no shader module cache.

---

## Limitations

Each verified against code, not inferred from names.

**Cannot be expressed at all:**

- **Ray tracing** — no `VK_KHR_ray_tracing` anywhere; `PassType` is `{Graphics, Compute, Transfer}` (`FrameGraphPass.h:33`).
- **Multi-view / VR** — no `VkRenderPassMultiviewCreateInfo`, no view mask in any struct.
- **Mesh/task shaders, tessellation, geometry shaders.** `tess_control`/`tess_eval`/`geometry` are accepted as *descriptor stage flags* (`FrameGraphJson.cpp:201-202`) but `PipelineDesc` (`FrameGraphPass.h:211-235`) has only `vertexShader`/`fragmentShader`/`computeShader`, so those stages can never be populated.
- **Multiple subpasses.** `createRenderPasses` always calls `addBasicSubpass("main", ...)` once (`FrameGraphCompiler.cpp:720`). The `input_attachment` usage parses and maps to `SHADER_READ_ONLY_OPTIMAL` (`:46`), but `createRenderPasses` only iterates `passDecl.outputs` — inputs are never turned into attachments, so `input_attachment` cannot actually work.
- **MSAA and resolve attachments.** `ImageDesc::samples` is parsed and serialised but `VulkanImage.cpp:82` hardcodes `VK_SAMPLE_COUNT_1_BIT`. `PipelineStateBuilder` is never given a sample count. No `pResolveAttachments` anywhere.
- **Mip chains and array layers / cubemaps.** `VulkanImage.cpp:76-77` hardcodes `mipLevels = 1`, `arrayLayers = 1`. `createImageView` is called with a single aspect and no range. Cubemaps exist as a separate `VulkanCubemap` class outside the graph.
- **3D images.** `ImageDesc` has no depth field; `VkExtent2D` throughout.
- **Transient / aliased memory.** `ImageDesc::transient` is parsed and documented as "Phase 5" (`FrameGraphResource.h:48-49`) and `createPhysicalResources` never reads it. The analyzer *reports* alias candidates (`FrameGraphAnalyzer.cpp:987-1024`) but nothing acts on them. Every declared image gets its own dedicated allocation for the whole frame.
- **Conditional / dynamic passes.** `enabled` is a static bool set at load; there is no per-frame predicate, no `VK_EXT_conditional_rendering`, no indirect draw/dispatch (`vkCmdDrawIndirect`/`vkCmdDispatchIndirect` appear nowhere in the executor).
- **Bindless / descriptor indexing.** `DescriptorBindingDesc::count` exists but `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` / `VARIABLE_DESCRIPTOR_COUNT` / `UPDATE_AFTER_BIND` flags appear nowhere.
- **Dynamic rendering** (`VK_KHR_dynamic_rendering`) — everything goes through `VkRenderPass`+`VkFramebuffer`.
- **Per-pass viewport/scissor.** Always set to the full pass extent (`FrameGraphExecutor.cpp:280-281`).
- **Depth bias, depth compare op, depth bounds, stencil state.** `PipelineDesc` exposes only `depthTest`/`depthWrite`. Shadow-map slope-scaled bias is not expressible.
- **Per-attachment blend state.** `blending` is one string for the whole pass; with MRT all attachments share it.
- **Buffer resources as real graph resources.** `declareBuffer` works, but no barrier is ever emitted for a buffer (`FrameGraphCompiler.cpp:506-507` early-returns for non-images) and `BufferDesc::usage` is never populated by the JSON parser (`FrameGraphJson.cpp:948-949` sets only `size` and `persistentlyMapped`), so a JSON-declared buffer resource is created with `usage == 0` — an invalid `VkBufferCreateInfo`.
- **Async compute with real overlap.** See F5 — the multi-queue path is unreachable dead code.
- **`transfer` pass type.** Parses (`FrameGraphJson.cpp:157`) but `createRenderPasses`, `createPipelines`, and `executeAutoCallback` all skip or ignore it; a transfer pass does nothing unless it has a manual callback.

**Expressible but only via manual C++ callback:** indexed/indirect draws, instancing beyond a fixed `instanceCount`, mipmap generation, blits, anything needing per-draw state.

---

## Findings

### CRITICAL

**F1 — Barrier access masks computed by the compiler are discarded by the executor; the actual barrier uses a 5-case layout heuristic that yields `srcAccess = 0` for the most common transition.**
`FrameGraphCompiler.cpp:520-521` computes correct `srcAccess`/`dstAccess` into `BarrierInfo`. `FrameGraphExecutor.cpp:227-233` then calls `cmd.imageBarrier(image, oldLayout, newLayout, srcStage, dstStage)` — **the access masks are not passed**. `VulkanCommandBuffer.cpp:308-321` re-derives them from layouts with three `old` cases (`UNDEFINED`→0, `TRANSFER_DST`, `SHADER_READ_ONLY`) and two `new` cases (`TRANSFER_DST`, `SHADER_READ_ONLY`). Everything else gets **0**.

Consequence for the canonical render-to-texture chain: `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` gets `srcAccessMask = 0`, so the previous pass's colour writes are never made *available*. `→ COLOR_ATTACHMENT_OPTIMAL` and `→ PRESENT_SRC_KHR` and `→ GENERAL` all get `dstAccessMask = 0`, so nothing is made *visible*. This is a genuine memory-hazard bug, not just a validation-layer complaint. It happens to work on desktop IHVs with coherent L2 much of the time, which is exactly why it survives.

**F2 — Every barrier uses `VK_IMAGE_ASPECT_COLOR_BIT` unconditionally, including depth images.**
`VulkanCommandBuffer.cpp:302`, and the queue-transfer paths in the executor at `FrameGraphExecutor.cpp:173`, `:214`, `:390`, `:428`. Any depth resource that changes layout (`depth_write` → `depth_read`, or shadow map → `shader_read`) gets an image barrier whose `subresourceRange.aspectMask` does not match the image's aspect. This is a spec violation (VUID-VkImageMemoryBarrier-image-01207 family) and the transition is not performed. Every shadow-mapping and depth-sampling pipeline in the repo hits this.
`levelCount`/`layerCount` are likewise hardcoded to 1 in all five sites — currently harmless only because F9 forces all images to 1 mip / 1 layer.

**F3 — Barriers are emitted only on layout change, so same-layout read-after-write hazards get no synchronization at all.**
`FrameGraphCompiler.cpp:513`: `if (currentLayouts[ri] != requiredLayout)`. `lastStages`/`lastAccess` are updated regardless (`:540-541`) but only consumed inside that branch.

Missed cases:
- compute writes storage image (`GENERAL`) → next compute reads it (`GENERAL`): no barrier.
- two consecutive `color_write` passes to the same attachment: no barrier (relies entirely on the render pass's `EXTERNAL` dependency).
- **all buffer hazards**: `:506-507` returns early for `PhysicalBuffer`. The compute→graphics SSBO case is patched by a blanket `VkMemoryBarrier` in the executor (`FrameGraphExecutor.cpp:141-158`) that fires whenever *any* compute pass is followed by *any* non-compute pass, with a fixed `COMPUTE_SHADER → VERTEX_INPUT|VERTEX_SHADER` scope. A compute→fragment SSBO read is not covered. A graphics→compute buffer read is not covered. Compute→compute is not covered.

Write-after-read and write-after-write are not modelled anywhere: `resolveLayoutsAndInsertBarriers` tracks a single "last access" per resource with no read-set, and the analyzer's `checkWriteHazards` (`FrameGraphAnalyzer.cpp:703`) only *warns* when a resource has more than one writer.

**F4 — Dependency edges are derived from JSON declaration order, so a pass that reads a resource written by a later-declared pass gets no edge and no barrier.**
`FrameGraphCompiler.cpp:156-165`, `findPreviousWriter`, breaks at `w >= currentPass` where both are *declaration* indices. `topologicalSort` at `:175` therefore only creates an edge to a writer declared earlier in the JSON array.

This means the "topological sort" cannot reorder anything relative to declaration order in a way that discovers a dependency the author did not already encode positionally — and worse, a graph where the producer is declared after the consumer compiles cleanly, culls nothing, produces zero barriers for that resource, and reads undefined memory. The docs describe passes as being scheduled by dependency analysis; in practice JSON array order *is* the schedule. This is arguably the single most important design constraint in the subsystem and it is undocumented.

### MAJOR

**F5 — The entire multi-queue / async-compute path is unreachable dead code, and would be incorrect if reached.**
`RenderGraph::createSyncPrimitives` (`RenderGraph.cpp:1750`) creates a timeline semaphore. `m_timelineSemaphore` and `m_timelineValue` are then referenced **only** in the constructor and destructor — grep across all of `src/`, `include/`, and `examples/` finds no other use. `executeMultiQueue` (`:1726`) and `needsMultiQueueSubmit` (`:1722`) are never called by any code in the repository. `SyncPoint` (`FrameGraph.h:595`) carries `signalQueue`/`waitQueue`/`timelineValue` but **no batch indices**, so even a correct submitter could not tell which batch signals which value. `executeMultiQueue` records into two command buffers and performs no submission and no semaphore operation.
`executePasses` (`FrameGraphExecutor.cpp:324`) — the multi-queue variant — also omits the `passDecl.enabled` check that `execute` has (`:120`).

**F6 — Queue ownership transfers are emitted twice on the acquiring queue and never released on the source queue.**
`FrameGraphCompiler.cpp:526-536`: when a cross-queue transition is detected, the same `BarrierInfo` is pushed into **both** `compiled.acquireBarriers` and `compiled.preBarriers`. The executor then issues both loops (`FrameGraphExecutor.cpp:161-186` then `:189-236`, and the pre-barrier path takes the `srcQueueFamilyIndex != IGNORED` branch at `:205`). The image is transitioned `old→new`, then transitioned `old→new` again from a layout it is no longer in.
Separately, a QFOT requires a *release* barrier submitted on the source queue with the same handles. None is ever recorded. The transfer is therefore malformed on both halves.

**F7 — The compiler and the analyzer implement culling differently, so `simulateCulling()` and `getCullingReport()` can disagree with what actually compiled.**
Compiler (`FrameGraphCompiler.cpp:235-345`): builds `resourceWriter[res] = writers.back()` (the *last* writer overall, `:274-278`) for backward liveness, plus a separate `ColorAttachmentBlend` previous-writer chain (`:322-333`).
Analyzer (`FrameGraphAnalyzer.cpp:459-578`): `resourceWriter[idx] = i` (last writer, `:485`), no blend handling at all, and propagates liveness through the forward adjacency list rather than input edges.

Beyond the divergence, the compiler's own use of "last writer overall" for backward liveness is wrong: given `A writes R`, `B reads R`, `C writes R` (all in declaration order), `resourceWriter[R] == C`, so marking `B` live marks `C` live but **not** `A`. If `A` has no other consumer it is culled and `B` reads undefined memory. `topologicalSort` meanwhile correctly edges `A→B` via `findPreviousWriter`. The two passes disagree within the same compile.

**F8 — Render pass `initialLayout` is set to the barrier's *old* layout, after the barrier has already moved the image to the *new* layout.**
`FrameGraphCompiler.cpp:614-620`: `initialLayout = barrier.oldLayout; finalLayout = barrier.newLayout;`. The executor issues `preBarriers` at `:189-236` and only then calls `vkCmdBeginRenderPass` at `:273`. At `vkCmdBeginRenderPass` the image is in `barrier.newLayout`, but the render pass declares it as being in `barrier.oldLayout` and will perform a second transition from there.
Masked in two common cases: when `hasClearValue` is set, `initialLayout` is forced to `UNDEFINED` (`:623-625`), which is "don't care"; and for `ColorAttachmentBlend` it is forced to `COLOR_ATTACHMENT_OPTIMAL` (`:628-631`). It bites for a plain `color_write` with no clear on a resource that was previously read — i.e. a ping-pong post-process chain.

**F9 — `samples`, `mipLevels`, `arrayLayers` are parsed, documented, and serialised, but hardcoded to 1 at image creation.**
`FrameGraphCompiler.cpp:428-431` passes only `width/height/format/tiling/usage/memProps` to `VulkanImage`; `VulkanImage.cpp:76,77,82` sets `mipLevels = 1`, `arrayLayers = 1`, `samples = VK_SAMPLE_COUNT_1_BIT`. A JSON author who writes `"samples": 4` gets a silently single-sampled image. `FrameGraphAnalyzer::estimateResourceSize` (`:232-240`) *does* multiply by samples/layers/mips, so the memory report is wrong in the opposite direction.

**F10 — `std430` and `scalar` packing produce wrong offsets.**
`FrameGraphCompiler.cpp:1629` sets `useStd140 = (packing == Std140)`. Everything else falls into the `else` at `:1656-1664`, which applies **no alignment whatsoever** — offsets are `sizeof` sums via `getFieldTypeSize` (`:1515`), which returns 12 for `Vec3` and 36 for `Mat3`. std430 requires `vec3` aligned to 16 and `mat3` laid out as 3 vec4-aligned columns (48 bytes). Every SSBO declared with `"packing": "std430"` and containing a `vec3` or `mat3` will mismatch its GLSL counterpart. The docs explicitly recommend `std430` for SSBOs.
Also at `:1658`: `if (field.offset != 0) currentOffset = field.offset;` — an explicitly declared offset of 0 is indistinguishable from "unset".

**F11 — `mat3` size disagrees between the layout compiler and the dot-path resolver.**
`compileBufferLayouts` std140 path uses `getStd140PaddedSize(Mat3) == 48` (`:1583`). But `RenderGraph.cpp:217`, `:2066`, and `:2124` all set `resolvedField.size = 36` for `Mat3` when converting to `Shoonyakasha::BufferField`. A std140 `mat3` field is 48 bytes on the GPU and 36 bytes to the writer. Three separate copies of this conversion switch exist (`createDotPathUBOs`, `bindEntityData`, `fillBuffer`) — see F18.

**F12 — `"execution": {"type": "compute_image"}` always dispatches zero workgroups.**
`FrameGraphExecutor.cpp:639-643` computes group counts from `ctx.renderExtent`, which is `compiledPass.extent`. `compiled.extent` is only ever assigned inside `createRenderPasses`, which `continue`s for non-graphics passes (`FrameGraphCompiler.cpp:576`). A compute pass therefore has `extent == {0,0}`, giving `(0 + 15)/16 == 0` and `vkCmdDispatch(cmd, 0, 0, 1)`.

**F13 — `sprite_geometry` passes never draw.**
`RenderGraph::setupAutoGeometryRenderers` accepts `"sprite_geometry"` and installs a `sceneRendererFn` (`RenderGraph.cpp:1658`), and `FrameGraphRenderer.h:208` maps it to `EntityFilter::Sprite2D`. But `FrameGraphExecutor::executeAutoCallback`'s dispatch chain (`FrameGraphExecutor.cpp:675-680`) lists `scene_geometry`, `opaque_geometry`, `transparent_geometry`, `shadow_casters`, `skinned_geometry`, `skinned_transparent` — and **not** `sprite_geometry`. The `else if` chain falls through with no branch and no warning. Sprite and text rendering (`TextRenderSystem.h:7` says text reuses this path) silently produce nothing.

**F14 — Samplers leak on every recompile.**
`RenderGraph::recompile` (`:1462-1501`) destroys framebuffers explicitly but not `m_compiled.samplers`. It then calls `compile()`, which overwrites `m_compiled` wholesale (`:1429`). The old `VkSampler` handles are lost. The destructor destroys only the *final* set (`:87-92`). One leaked sampler per declared sampler per window resize.

**F15 — Per-entity descriptor set cache survives recompilation with stale layouts.**
`m_materialDescriptorCache` (`FrameGraph.h:1252`) keys on `(entityId, hash(layoutName), frameIndex)` and is never cleared — not in `recompile`, not in `compile`. After a resize, `createDescriptorSetLayouts` creates fresh `VkDescriptorSetLayout` objects (`FrameGraphCompiler.cpp:1036`) and the old ones are destroyed with the old `VulkanDescriptorSet`s. `bindMaterialTextures` (`RenderGraph.cpp:2205`) then returns a cached `VkDescriptorSet` allocated from a destroyed layout and binds it against the new pipeline layout. Use-after-free plus layout incompatibility.
`m_materialDescriptorPool` is also never reset, so the cache's entries accumulate across the lifetime of the app up to the 4096 hard cap (`:2192`), after which `vkAllocateDescriptorSets` fails and materials silently fall back to unbound.

**F16 — `saveGraphToJson` is lossy enough to be misleading.**
See the JSON inventory. It writes `version: 1` (the loader ignores it, and the examples use 2 and 3) and drops eight of the eleven top-level sections. `saveGraphToFile` is public API (`FrameGraphJson.h:86`).

**F17 — GPU timing attributes durations to the wrong passes and leaks the query pool.**
`FrameGraphDebugger::collectGpuTimings` (`FrameGraphDebugger.cpp:250-262`) walks `m_currentPassTimings` with a counter `timingIdx` and then looks up `m_passQueryIndices.find(timingIdx)` — treating a *position in the timings vector* as a *pass index*. These coincide only when the execution order happens to be `0,1,2,...`. Otherwise durations land on arbitrary passes.
`disableGpuTiming` (`:81-88`) sets `m_timestampQueryPool = VK_NULL_HANDLE` without calling `vkDestroyQueryPool`; the destructor (`:28-31`) does not either. The comment acknowledges it. Also, `collectGpuTimings` is never called from `RenderGraph` — `enableGpuTiming` is exposed on `RenderGraph` (`FrameGraph.h:1080`) but there is no corresponding collect, so `getPassTime`-style GPU queries return 0 unless the app reaches into `getDebugger()` itself.

### MINOR

**F18 — The `BufferFieldType` → `MaterialParam::Type` conversion switch is copy-pasted three times.**
`RenderGraph.cpp:212-222`, `:2061-2071`, `:2119-2129`. Identical 9-case switches with identical hardcoded sizes. F11's `mat3` bug is present in all three. The offset-recompute logic that follows (`:2074-2075`, `:2131-2132`) is also duplicated and differs from what `compileBufferLayouts` already computed — `bindEntityData` and `fillBuffer` recompute offsets tightly-packed, ignoring the std140 offsets the compiler already stored in `field.offset`, using `(field.offset != 0) ? field.offset : currentOffset`.

**F19 — `recompile` from `ApplicationBase` silently resets frames-in-flight to 2.**
`ApplicationBase.cpp:439` calls `recompile(newExtent, imageCount)`, omitting the third argument, which defaults to 2 (`FrameGraph.h:1013-1014`). All current apps happen to use `MAX_FRAMES_IN_FLIGHT = 2`, so this is latent. `vkDeviceWaitIdle` *is* correctly called first (`ApplicationBase.cpp:418`).

**F20 — `AnalysisResult::queueBatches` is declared, consumed by the exporter, and never populated.**
`FrameGraphAnalyzer.h:251` declares it; `FrameGraphExport.cpp:187-189` reads it under `options.clusterByBatch`; `analyzeCompiled` (`FrameGraphAnalyzer.cpp:1083-1142`) never fills it. Same for `AnalysisStatistics::queueBatchCount` and `syncPointCount` (`FrameGraphAnalyzer.h:226-227`) — declared, never assigned. `computeStatistics` (`:1026`) skips them.

**F21 — Two `ExportOptions` fields are dead.**
`colorByPassType` and `verboseMode` (`FrameGraphExport.h:45`, `:66`) have zero reads in `FrameGraphExport.cpp`.

**F22 — `endRenderPass` is guarded on `framebuffers[0]` while `beginRenderPass` is guarded on `framebuffers[fbIdx]`.**
`FrameGraphExecutor.cpp:302-305` (and `:506-509`). If `framebuffers[0] == VK_NULL_HANDLE` but a later index is valid, the render pass is begun and never ended. Reachable only if framebuffer creation partially fails (`FrameGraphCompiler.cpp:817` skips creation when the view list is empty or the extent is zero) — which is exactly the failure mode where it would matter.

**F23 — Five separate raw-`new` `Logger*` members with manual `delete`.**
`FrameGraphCompiler.cpp:840` (lazily, inside `compile`), `FrameGraphExecutor.cpp:82`, `FrameGraphAnalyzer.cpp:24`, `FrameGraphDebugger.cpp:24`, `RenderGraph.cpp:48`. Each writes its own log file. None of the owning classes suppress copy/move, so a copy would double-free. `FrameGraphCompiler::m_logger` is dereferenced unconditionally in `createDescriptorSetLayouts`, `createSamplers`, `performAutoBindings`, and `compileBufferLayouts` — safe today only because `compile()` always runs first, but these are `private` methods with no guard.

**F24 — Buffer resources declared in JSON get `usage == 0`.**
`FrameGraphJson.cpp:948-949` populates only `size` and `persistentlyMapped`; `BufferDesc::usage` (`FrameGraphResource.h:58`) stays 0 and `createPhysicalResources` (`FrameGraphCompiler.cpp:451-453`) passes it straight to `VulkanBuffer`. `persistentlyMapped` is ignored; memory is always `DEVICE_LOCAL` (`:449`).

**F25 — `FrameGraphBuilder::clear()` does not clear the vertex format registry** (`FrameGraphBuilder.cpp:246-261` vs `FrameGraph.h:568`).

**F26 — Push constant pushes ignore the declared range size.**
`FrameGraphExecutor.cpp:576-579` uses `sizeof(v)` from the variant alternative. A `float` parameter bound at an offset inside a 64-byte range pushes 4 bytes; a `std::array<float,16>` pushes 64. There is no check that `binding.offset + sizeof(v) <= pc.size`, and `PushConstantBindingDesc::type` (`FrameGraphPass.h:244`) is parsed but never used to validate against the actual variant alternative.

**F27 — `bindMaterialTextures` re-scans every compiled pass twice per call to find a set index.**
`RenderGraph.cpp:2213-2225` and again at `:2329-2338`. The second loop has no outer `break`, so it returns the *last* matching pass's index rather than the first. Called per entity per draw.

### File-size / structure observations

- `RenderGraph.cpp` (2468 lines) is a genuine god object: JSON loading, UBO/SSBO lifecycle, staging, readback, disk save, screenshot, ECS scene binding, dot-path resolution, per-entity descriptor caching, material texture binding, skeleton SSBO binding, analysis facade, export facade, debug facade, and multi-queue sync all live here. `FrameGraph.h` correspondingly declares 5 nested private structs and ~40 members. Natural splits: `RenderGraphResources` (dot-path UBO/SSBO + staging + readback + save), `RenderGraphEntityBinding` (the ECS/dot-path/descriptor-cache half, ~600 lines), leaving `RenderGraph` as the orchestrator it is named for.
- `FrameGraphCompiler.cpp` (1709) is well-staged and readable; the only structural complaint is that `compile()`'s stage list is a hardcoded sequence with no way to inspect or skip a stage.
- `FrameGraphJson.cpp` (1456) is ~700 lines of hand-written `if (str == "...")` chains and `.value()` calls. The three "silent default" bugs (F-inventory) are a direct consequence of that style; a table-driven `stringToEnum` returning `std::optional` would eliminate the class.
- Duplicated logic worth noting beyond F18: `findPreviousWriter` is implemented twice with slightly different loop conditions (`FrameGraphCompiler.cpp:156` vs `:260`); topological sort + culling is implemented twice (compiler vs `FrameGraphAnalyzer.cpp:459`); the ~100-line barrier + context + render-pass + dispatch block in `FrameGraphExecutor::execute` (`:115-316`) is copy-pasted almost verbatim into `executePasses` (`:345-520`), with the `enabled` check and frame-begin/end hooks dropped in the copy.

---

## Docs vs code discrepancies

| Doc claim | Location | Reality |
|---|---|---|
| `version` is a **required** top-level key; "currently 2 or 3" | `json-render-pipeline.md:36` | Never read. `loadGraphFromJson` (`FrameGraphJson.cpp:381`) has no version handling of any kind. There is no schema versioning. |
| `name` is a **required** top-level key | `json-render-pipeline.md:37` | Never read. |
| "Format, sample count, and mip levels come from the JSON `ImageDesc`" | `frame-graph-pipeline.md:83` | Only format. `VulkanImage.cpp:76,77,82` hardcodes mips/layers/samples to 1. (F9) |
| "The compiler inserts the appropriate buffer memory barrier between the compute write and the vertex read" | `frame-graph-pipeline.md:559` | The compiler emits no buffer barriers at all (`FrameGraphCompiler.cpp:506-507`). The executor emits one blanket `VkMemoryBarrier` on any compute→non-compute transition (`FrameGraphExecutor.cpp:141-158`), resource-agnostic. (F3) |
| "The engine tracks the current layout of every resource and inserts the minimal set of barriers needed" | `frame-graph-pipeline.md:464` | Barriers are emitted only on layout *change*; same-layout RAW gets nothing. (F3) |
| "The compiler generates timeline semaphore sync points so that compute passes can overlap with graphics passes" | `frame-graph-pipeline.md:584-588` | `SyncPoint`s are generated into a struct nothing reads. The timeline semaphore is created and destroyed and never signalled. `executeMultiQueue` is never called. (F5) |
| Passes declare `queueType: "compute"` | `frame-graph-pipeline.md:185`, `:584` | The JSON key is `queue` (`FrameGraphJson.cpp:1024`). `queueType` is silently ignored. |
| `std430` = "tighter packing, no vec3 rounding" | `json-render-pipeline.md:260` | std430 falls into a no-alignment path. vec3 is *not* rounded — which is the bug, since std430 requires 16-byte alignment for vec3. (F10) |
| `mat3` is 36 bytes | `json-render-pipeline.md:241` | 48 in the compiler's std140 path (`FrameGraphCompiler.cpp:1583`), 36 in the resolver (`RenderGraph.cpp:217`). Neither matches the other. (F11) |
| `per_frame` is "default for UBOs" | `json-render-pipeline.md:269` | The parser defaults to `manual` (`FrameGraphJson.cpp:602-604`). A UBO without an explicit `updateFrequency` is never auto-filled. |
| Execution type table lists 9 types | `json-render-pipeline.md:719-731` | Code also accepts `compute_image` (`FrameGraphExecutor.cpp:635`), `scene_geometry` (`:675`), and `sprite_geometry` (`RenderGraph.cpp:1658` — which does not work, F13). None documented. |
| Resource usage table lists 4 outputs + 2 inputs | `json-render-pipeline.md:741-757` | Code accepts 14 strings (`FrameGraphJson.cpp:110-126`). Undocumented: `shader_read_write`, `input_attachment`, `transfer_src`, `transfer_dst`, `present`, and the `color_attachment_*` / `depth_stencil_write` aliases. |
| Not documented at all | — | Pass-level `queue`, `hasSideEffects`, `enabled`; resource-level `target`/`readback`/`save`; bufferLayout `target`/`memory`/`source`/`elementCount`; `uniformBuffers` section entirely. `hasSideEffects` in particular is load-bearing — without it any compute pass writing only through descriptors is culled. |
| Doc says pass array is "passes in execution order" | `json-render-pipeline.md:45` | Accurate in effect, but the docs elsewhere describe dependency-driven scheduling. Declaration order is load-bearing for correctness, not just a convention (F4). This is the most important undocumented constraint. |

Accurate in the docs, for the record: the compilation stage list in `frame-graph-pipeline.md:45-207` matches the code's actual stage sequence closely; the dispatch-sizing section (`json-render-pipeline.md:860-914`) matches `FrameGraphExecutor.cpp:646-666`; the dot-path prefix classification matches `FrameGraphCompiler.cpp:1683-1689`.

---

## Open questions

1. **Is the declaration-order dependency constraint (F4) intentional?** If it is a deliberate simplification ("JSON order *is* the schedule"), it should be documented and `topologicalSort` should be renamed/reduced to a validation pass. If not, `findPreviousWriter` needs to work on the resolved order rather than declaration indices, which is a larger change.

2. **Was the multi-queue path (F5) ever exercised?** No example uses it, `SyncPoint` cannot express batch pairing, and the timeline semaphore is inert. Was there a working prototype that got reverted, or was it built speculatively?

3. **`FrameGraphExecutor` calls `cmd.imageBarrier` which drops the access masks (F1).** Was this a deliberate simplification in `VulkanCommandBuilder`, or did the two sides drift? The compiler clearly intends the full masks to be used — the fix is a wider `imageBarrier` overload, which touches the RHI layer outside my scope.

4. **`FrameGraphDebugger::disableGpuTiming` explicitly comments that the caller must destroy the query pool, but no caller does.** Was an owner-side `destroyGpuTiming(VulkanDevice&)` intended?

5. **`m_sceneRenderers` (`FrameGraph.h:1114`) is marked DEPRECATED and has no public setter** — `registerGeometryRenderer` writes to `m_geometryTypeRenderers` instead. It is read once (`RenderGraph.cpp:1598`) and can never be non-empty. Safe to delete, or is there an external writer I did not find?

6. **Coverage caveat:** I read `FrameGraphExport.cpp` (1058 lines) and the back half of `FrameGraphDebugger.cpp` / `RenderTargetSaver.cpp` structurally rather than line-by-line. These are presentation/IO code with no synchronization or graph-algorithm content; findings F17, F20, F21 came from targeted verification within them. A line-by-line pass over the export string builders could surface additional formatting bugs but is unlikely to change any conclusion above.
