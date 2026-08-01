# Review 03 — Data binding (dot-path) & GPU resources

## Scope

Read in full:

- `include/FrameGraph/BufferLayoutCompiler.h`, `DotPathResolver.h`, `EntityRenderExecutor.h`, `FrameGraphRenderer.h`, `SharedBufferRegistry.h`, `StagingBufferManager.h`
- `src/FrameGraph/DotPathResolver.cpp`, `EntityRenderExecutor.cpp`, `FrameGraphRenderer.cpp`, `SharedBufferRegistry.cpp`, `StagingBufferManager.cpp`
- `include/GPU/GPUTypes.h`, `include/GPU/GPUResourceFactory.h`, `src/GPU/GPUResourceFactory.cpp`
- `docs/guides/custom-shader-uniforms.md`, `docs/old/declarative_ssbo_data_flow.md`
- `tests/unit/BufferLayoutCompilerTest.cpp`, `tests/unit/DotPathResolverTest.cpp`, `tests/unit/SharedBufferRegistryTest.cpp`, `tests/framegraph/DotPathResolverECSTest.cpp`

Read for tracing only (owned by other reviewers): `src/Vulkan/FrameGraph/RenderGraph.cpp`, `FrameGraphCompiler.cpp`, `src/Vulkan/VulkanBuffer.cpp`.

**Structural finding up front:** there are *two* layout compilers in this repo.
`BufferLayoutCompiler.h` is used **only by tests** — grep across the whole tree finds no
production include outside `tests/unit/BufferLayoutCompilerTest.cpp` and
`tests/framegraph/DotPathResolverECSTest.cpp`. The live path compiles layouts in
`src/Vulkan/FrameGraph/FrameGraphCompiler.cpp:1596-1706`. `EntityRenderExecutor`
(header + cpp, ~800 lines) has **zero** callers anywhere — the live entity draw loop is
`FrameGraphRenderer` + `RenderGraph::bindEntityData`. This matters for every packing
finding below: the unit tests exercise the dead compiler.

## Dot-path inventory

Parsing: `DotPathResolver::getPathRoot` (`src/FrameGraph/DotPathResolver.cpp:103`)
dispatches on prefix `scene.` / `entity.` / `const.`; anything else starting with a letter
or `_` is `Resource` (frame-graph resource name, resolves to *invalid* in this class,
`DotPathResolver.cpp:157-159`). `splitPath` (`:119`) splits on `.` into `string_view`s in a
freshly-allocated `std::vector` on every call.

### `scene.*` — `resolveScenePath`, `DotPathResolver.cpp:188-297`

| Path | Type | Line |
|---|---|---|
| `scene.camera.view` | mat4 | :199 |
| `scene.camera.projection` | mat4 (Y-flipped) | :200 |
| `scene.camera.viewProjection` | mat4 | :201 |
| `scene.camera.invView` | mat4 | :202 |
| `scene.camera.invProj` | mat4 | :203 |
| `scene.camera.position` | vec3 | :204 |
| `scene.camera.fov` | float | :205 |
| `scene.camera.nearPlane` | float | :206 |
| `scene.camera.farPlane` | float | :207 |
| `scene.camera.aspect` | float | :208 |
| `scene.camera.positionVec4` | vec4 (pos,1) | :211 |
| `scene.camera.nearFarFovAspect` | vec4 | :213 |
| `scene.environment.irradianceMap` | GPUTexture | :222 |
| `scene.environment.prefilterMap` | GPUTexture | :223 |
| `scene.environment.brdfLUT` | GPUTexture | :224 |
| `scene.environment.environmentMap` | GPUTexture | :225 |
| `scene.time.elapsed` | float | :232 |
| `scene.time.delta` | float | :233 |
| `scene.time.frame` | uint | :234 |
| `scene.screen.width` | float | :241 |
| `scene.screen.height` | float | :242 |
| `scene.screen.resolution` | vec2 | :243 |
| `scene.lights.count` | uint | :251 |
| `scene.lights[N].positionType` | vec4 | :271 |
| `scene.lights[N].colorIntensity` | vec4 | :272 |
| `scene.lights[N].directionRange` | vec4 | :273 |
| `scene.lights[N].attenuation` | vec4 | :274 |
| `scene.custom.<dotted.key>` | whatever was `setCustom`'d | :282-294 |

`scene.lights[i]` (literal `i`) is only meaningful through the array-expansion path in
`fillSceneBuffer` (`:527`); everywhere else `from_chars("i")` fails and the field silently
becomes zeros.

### `entity.*` — `resolveEntityPath`, `DotPathResolver.cpp:303-409`

| Path | Type | Line |
|---|---|---|
| `entity.transform.worldMatrix` | mat4 | :321 |
| `entity.transform.localMatrix` | mat4 | :322 |
| `entity.transform.position` | vec3 | :323 |
| `entity.transform.rotation` | vec4 (euler.xyz, 0) | :324 |
| `entity.transform.scale` | vec3 | :327 |
| `entity.material.params.<name>` | type of the stored `MaterialParam` | :336-353 |
| `entity.material.textures.<name>` | GPUTexture | :367 |
| `entity.material.textures.<name>.exists` | float 1/0 | :360-363 |
| `entity.material.alphaCutoff` | float | :375 |
| `entity.material.alphaMode` | uint | :378 |
| `entity.material.doubleSided` | float 1/0 | :381 |
| `entity.mesh.vertexCount` | uint | :389 |
| `entity.mesh.indexCount` | uint | :390 |
| `entity.skeleton.hasSkeleton` | float 1/0 | :399 |
| `entity.skeleton.jointCount` | uint | :405 |

### `const.*` — `resolveConstPath`, `DotPathResolver.cpp:415-452`

Dots are component separators, **not decimal points**: `const.42` → float 42;
`const.1.2` → **vec2(1,2)**; `const.1.2.3` → vec3; `const.1.2.3.4` → vec4. This is asserted
as intended behaviour by `tests/unit/DotPathResolverTest.cpp:240-265`.

### Unknown paths

There is **no error path**. Every miss returns a default-constructed `ResolvedValue`
(monostate), and `BufferLayoutResolver::writeField` (`DotPathResolver.cpp:571-575`)
`memset`s `field.size` bytes to zero. No log, no throw, no counter. A typo in a JSON
`source` is invisible: you get a black material or a zero matrix and no diagnostic.
`validatePath` (`:458`) exists but has **no production caller** (grep: only tests), and its
whitelist at `:472` omits `lights` and `custom`, so it would reject the two path families
that real pipeline JSON uses most (`assets/**/*.json` uses `scene.lights[i].*` and
`scene.custom.particles.*`).

### Per-frame cost of resolution

Not cached. Every field of every buffer, every frame (and for push constants, every field
of every *draw*), does: `getPathRoot` (3 `starts_with`), `splitPath` (one `std::vector`
heap allocation), then a linear chain of `string_view` comparisons. Material params and
textures additionally construct a `std::string` for the map key
(`DotPathResolver.cpp:337`, `:357`) — one more allocation each — then hash it.

## How data reaches the GPU (traced)

Two distinct routes, both terminating in `BufferLayoutResolver`:

**1. Per-frame scene UBOs.** `FrameGraphCompiler::compileBufferLayouts`
(`FrameGraphCompiler.cpp:1596`) computes std140 offsets at compile time. `RenderGraph`
pre-converts each layout once into a `Shoonyakasha::CompiledBufferLayout`
(`RenderGraph.cpp:199-229`) and stores it in `DotPathUBO::resolvedLayout` — this is the
one place the conversion is hoisted out of the frame loop. Each frame,
`RenderGraph::updateDotPathUBOs` (`RenderGraph.cpp:261-275`) allocates a
`std::vector<uint8_t>`, calls `fillSceneBuffer`, and `update()`s a host-visible
`VulkanBuffer` (map → memcpy → flush → unmap, `VulkanBuffer.cpp:135-146`).

**2. Per-draw push constants.** `FrameGraphRenderer::executeGeometryPass`
(`FrameGraphRenderer.cpp:124`) → `bindAndDrawEntity` (`:179`) →
`RenderGraph::bindEntityData` (`RenderGraph.cpp:2019-2088`). This rebuilds the entire
`Shoonyakasha::CompiledBufferLayout`, **including a `std::string` copy of every field's
`name` and `source`, on every draw call** (`:2046-2078`), allocates a `pushData` vector
(`:2042`), calls `fillBuffer`, then `vkCmdPushConstants`.

**Writing.** `writeField` (`DotPathResolver.cpp:570-579`) does
`memcpy(dest, &value, sizeof(resolved C++ type))`. The declared `field.type` / `field.size`
are used *only* for the zero-fill of missing values. Nothing checks that the resolved type
matches the declared type.

**Staging.** `StagingBufferManager` maintains per-name rings of host-visible
`VulkanBuffer`s, slot = `frameIndex % ringDepth` (`StagingBufferManager.cpp:141, 194, 248,
292`). `RenderGraph::execute` (`RenderGraph.cpp:1507-1549`) calls
`processCompletedReadbacks` → `recordUploadCommands` → passes → `recordReadbackCommands` →
`recordImageReadbackCommands`, in that order.

## Features

- Dot-path binding covering camera, time, screen, lights, IBL, arbitrary app key/values,
  transform, material params/textures, mesh counts, skeleton presence.
- `scene.custom.*` with dotted namespacing, wired to Python (`docs/guides/custom-shader-uniforms.md`).
- `[i]` array expansion for scene buffers with per-element std140 stride
  (`DotPathResolver.cpp:527-541`), driven by `arrayCount`/`arrayStride` computed in
  `FrameGraphCompiler.cpp:1648-1652`.
- Entity filtering by alpha mode / skeleton / sprite / shadow-caster, plus an 8-bit render
  layer mask (`FrameGraphRenderer.cpp:76`) and three sort modes.
- GPU→CPU buffer *and* image readback with per-frequency policies, callbacks, polling, and
  a one-shot trigger (`StagingBufferManager.cpp:128, 157, 389`).
- Cross-graph buffer/image sharing by name with a version counter (`SharedBufferRegistry`).
- Thin GPU resource factory: buffers, 2D/cube/depth textures, mipmap generation, PBR
  fallback textures.

## Limitations

- **Types supported in buffers:** float, vec2, vec3, vec4, mat3, mat4, int, uint only
  (`BufferLayoutCompiler.h:70-81`; `RenderGraph.cpp:2061-2071`). No bool, no `ivecN`/`uvecN`
  in the resolver conversion (they hit `default:` → treated as float, `RenderGraph.cpp:2070`),
  no mat2, no double, no nested structs, no arrays-of-structs at field level.
- **No array support in `BufferLayoutCompiler::compile`** — it never reads `arrayCount` or
  `arrayStride` from JSON (`BufferLayoutCompiler.h:159-189`), so `BufferField::arrayCount`
  stays 1 and the array-expansion branch is unreachable through that compiler. The live
  compiler does read it (`FrameGraphJson.cpp:626`).
- **No array expansion for entity/push-constant buffers** — `fillEntityBuffer`
  (`DotPathResolver.cpp:550-559`) has no `[i]` handling at all.
- **No entity culling of any kind.** No frustum, no distance cutoff, no occlusion. Every
  entity matching the filter is drawn every frame.
- **No instancing, no batching.** One `vkCmdBindVertexBuffers` +
  `vkCmdBindIndexBuffer` + `vkCmdDraw*` per entity, `instanceCount` hardcoded to 1
  (`FrameGraphRenderer.cpp:227-235`).
- `const.` cannot express a non-integer scalar.
- `SharedBufferRegistry` has no thread-safety and no synchronisation primitives.
- `createVertexBuffer`/`createIndexBuffer` accept `data` and a command buffer and ignore
  both.

## Findings

### CRITICAL

**C1. `writeField` writes `sizeof(resolved type)`, not `field.size` — heap/buffer overflow
on any type mismatch.** `src/FrameGraph/DotPathResolver.cpp:570-579`.

```cpp
uint8_t* dest = static_cast<uint8_t*>(buffer) + field.offset;
value.copyTo(dest);          // memcpy of sizeof(T) where T is the *resolved* type
```

`ResolvedValue::copyTo` (`DotPathResolver.h:112-119`) memcpy's the variant's own size.
The declared type is never consulted. Concretely reachable today:

- JSON declares `{"type":"float","source":"const.0.5"}` → `const.0.5` resolves to
  **vec2** (`DotPathResolver.cpp:446-449`) → 8 bytes written into a 4-byte slot.
- JSON declares `{"type":"vec4","source":"entity.material.params.foo"}` and the app stored
  `foo` as a mat4 → 64 bytes written into a 16-byte slot.
- The last field of a push-constant layout overruns `pushData`
  (`RenderGraph.cpp:2042`, a `std::vector<uint8_t>` sized to `layout->totalSize`) → heap
  corruption, not just wrong pixels.

There is no bounds check against `layout.totalSize` anywhere in the write path. Fix is a
type/size guard in `writeField` plus a clamp to `totalSize`.

**C2. Dangling `pImageInfo` passed to `vkUpdateDescriptorSets`.**
`src/FrameGraph/EntityRenderExecutor.cpp:479-489`.

```cpp
imageInfos.push_back(imageInfo);
...
write.pImageInfo = &imageInfos.back();   // pointer into a vector that will reallocate
writes.push_back(write);
```

`imageInfos` starts empty and is never reserved; the second and third `push_back` reallocate
and invalidate the pointers stored in `writes[0]`/`writes[1]`. `vkUpdateDescriptorSets`
(`:493`) then reads freed memory. Mitigating factor: `EntityRenderExecutor` has no callers,
so this is currently latent. Still CRITICAL if the class is ever wired up. Fix:
`imageInfos.reserve(textureSlots.size())` or index-based fixups after the loop.

**C3. mat3 is written tightly packed (36 B) into a std140/std430 slot that expects 48 B.**
`src/FrameGraph/DotPathResolver.cpp:578` (via `copyTo`), against
`FrameGraphCompiler.cpp:1583` (`Mat3: 3*16 = 48`) and
`BufferLayoutCompiler.h:173-175` (`effectiveSize = 48`).

Both compilers correctly reserve 48 bytes and align the *next* field accordingly, but the
resolver writes `sizeof(glm::mat3)` = 36 bytes contiguously. Columns 1 and 2 land at byte
offsets 12 and 24 instead of 16 and 32. Every mat3 uniform is silently garbage on the GPU.
The 12 bytes of intended padding at the end keep the *following* field correct, so this
corrupts only the mat3 itself — which makes it hard to spot.

Related: `BufferLayoutCompiler::getTypeAlignment` special-cases mat3 → 48 bytes for
`Std140` **only** (`BufferLayoutCompiler.h:173`). std430 has the same rule (a `vec3` column
has base alignment 16 in both layouts; std430 only relaxes the round-up-to-vec4 for arrays
and structs), so `packing: "std430"` + mat3 gives 36 bytes and a wrong offset for every
subsequent field. `tests/unit/BufferLayoutCompilerTest.cpp:237` (`Compile_Mat3_Scalar_NativeSize36`)
locks in the 36-byte behaviour for non-std140 packing.

### MAJOR

**M1. Full layout reconstruction, with two string copies per field, on every draw call.**
`src/Vulkan/FrameGraph/RenderGraph.cpp:2042-2078`. Per draw: 1 vector alloc for `pushData`,
1 for the field vector (plus growth reallocs), and `resolvedField.name = field.name` /
`resolvedField.source = field.source` — 2 `std::string` allocations per field. Then
`fillBuffer` adds a `splitPath` vector allocation per field and a `std::string` key for each
material param/texture lookup. A 6-field push-constant layout is ≈25 malloc/free per draw;
1000 entities = ~25 000 allocations per frame, per geometry pass. The scene-UBO path already
demonstrates the fix — `RenderGraph.cpp:199-229` pre-converts once at compile time and caches
in `DotPathUBO::resolvedLayout`. `bindEntityData` should do the same.

**M2. Unknown / mistyped dot-paths fail completely silently.**
`src/FrameGraph/DotPathResolver.cpp:296`, `:408`, `:571-575`. Every unresolved path yields
zeros with no log line and no counter, at any severity. `validatePath` is dead code
(no production caller) and its category whitelist (`:472`) doesn't even include `lights` or
`custom`. For a system whose entire premise is "JSON is the engine", a typo'd `source` should
at minimum warn once per layout at compile time.

**M3. The whole CPU→GPU upload half of `StagingBufferManager` is unreachable, but still
allocates.** `writeUploadData` (`StagingBufferManager.cpp:135`) and `markUploadDirty` (`:150`)
are the only ways to set `uploadDirty`, and neither has a single caller in the repository
(grep across `src/`, `include/`, `examples/`, `python/`). `shouldUpload` returns false
immediately when `!entry.uploadDirty` (`:173`), so `recordUploadCommands` never copies.
Meanwhile `RenderGraph::createStagingBuffers` sets `cfg.hasUpload = true` for any SSBO with
`TransferDirection::CpuToGpu`/`Bidirectional` (`RenderGraph.cpp:651-666`), which allocates
`ringDepth × size` host-visible bytes (`StagingBufferManager.cpp:49-59`) that are never
written and never read. For the 65 536-particle SSBO in the demo that is several MB of dead
host-visible memory.

**M4. `m_pendingResults` grows without bound unless the app polls.**
`src/FrameGraph/StagingBufferManager.cpp:321` and `:500` push a `ReadbackResult` every time
a readback completes. The only drain is `pollReadbacks()` (`:328`). An app that registers a
callback and never polls accumulates one `ReadbackResult` (with a `std::string bufferName`)
per readback per frame, forever. With `PerFrame` readback that is 60 entries/second/buffer.

**M5. Stale `ReadbackResult::data` pointers.** `StagingBufferManager.cpp:309` stores
`entry.readbackCpuBuffers[slot].data()` into the result. That slot is overwritten
`ringDepth` frames later. Any result held past that window silently reads a different
frame's data. Combined with M4 this means the polling API returns a mix of live and
overwritten pointers. Not a dangling pointer (the vectors are stable), but a correctness
trap the API does nothing to signal.

**M6. `ringDepthOverride` can silently break write-after-read safety.**
`StagingBufferManager.cpp:38` accepts `cfg.ringDepthOverride` verbatim with no validation
against `maxFramesInFlight`. The safety argument for slot reuse rests on
`slot == frameIndex % ringDepth` being 1:1 with the per-frame fence the app already waits on.
Set `ringDepth: 1` in JSON (`RenderGraph.cpp:673`, from `readbackPolicy.ringDepth`) with
3 frames in flight and two in-flight frames share slot 0 — the CPU reads a staging buffer the
GPU is concurrently writing. Should be clamped to `>= maxFramesInFlight` with a warning.

**M7. Uploads always copy the full buffer regardless of how much was written.**
`StagingBufferManager.cpp:145` clamps the *host* write to `min(size, entry.size)`, but
`copyRegion.size = entry.size` at `:219` copies the whole staging buffer to the GPU. A
partial `writeUploadData` publishes uninitialised staging memory into the tail of the GPU
buffer. (Latent given M3.)

**M8. UBO readback always reads frame 0's buffer.** `RenderGraph.cpp:690`:
`cfg.gpuBuffer = ubo.perFrameBuffers[0]->getBuffer()`. UBOs are per-frame-in-flight, so a
readback taken on frame index 1 or 2 reports the contents of frame 0's buffer, which the
CPU last wrote `maxFramesInFlight` frames ago.

**M9. `getEntityTextureDescriptorSet` leaks descriptor sets and never invalidates.**
`src/FrameGraph/EntityRenderExecutor.cpp:409-500`. On the `!m_registry` (`:441`) and
`!material` (`:446`) paths it returns `VK_NULL_HANDLE` *after* having allocated a set at
`:433`, without freeing it and without caching it — a straight leak from a pool with
`maxSets = 1000` (`:393`). The cache (`:498`) is never invalidated when a material's textures
change, so a texture swap is never reflected, and destroyed entities leave entries behind
(EnTT recycles ids with a bumped version, so `entt::to_integral` produces a *new* key and the
old set is orphaned). Also `:418` shifts the entity id by 16 while the comment at
`EntityRenderExecutor.h:234` says 32. Latent — no callers.

**M10. `createVertexBuffer` / `createIndexBuffer` silently ignore their `data` and
`cmdBuffer` arguments.** `src/GPU/GPUResourceFactory.cpp:42-70`. They set
`TRANSFER_DST_BIT` when `data != nullptr` — signalling intent to upload — then return a
buffer full of garbage. The header (`GPUResourceFactory.h:42-58`) advertises "and optionally
upload data". No caller uses them today (all call sites use `createBuffer` + `uploadBuffer`),
but the API is a trap. Either implement or delete.

### MINOR

**m1. `bindEntityData` pushes at `binding.offset` but fills from index 0.**
`RenderGraph.cpp:2085-2087` uses `layout->binding.offset` as the push-constant destination
offset while `pushData[0]` corresponds to field offset 0. If any layout ever declares a
non-zero binding offset the data is shifted. Currently always 0 in shipped JSON.

**m2. Compiler-computed offset 0 is indistinguishable from "unset".**
`RenderGraph.cpp:2074` / `:2131`: `resolvedField.offset = (field.offset != 0) ? field.offset : currentOffset`.
Works only because the first field is at 0 and `currentOffset` starts at 0. Fragile.

**m3. Scalar packing rule is really std430.** `BufferLayoutCompiler.h:98-101` returns
alignment 4/8/16 by size — that is std430, not `VK_EXT_scalar_block_layout` (where
everything aligns to its component type, i.e. 4). `PackingRule::Scalar` is also the *default*
(`:135`, `:149`), so pipelines that don't state `packing` get std430-ish alignment under a
misleading name.

**m4. `std::isalpha(path[0])` with a plain `char`.** `DotPathResolver.cpp:112` — UB for
bytes ≥ 0x80 on signed-char platforms. Cast to `unsigned char`.

**m5. `pushConstants` const-casts away constness of its own member.**
`EntityRenderExecutor.cpp:303-308` `const_cast`s `m_pushConstantBuffer` to resize and write
it from a `const` method. Should be `mutable`. It also always pushes
`VERTEX_BIT | FRAGMENT_BIT` at offset 0 (`:311-318`) regardless of the pipeline layout's
declared range, which the validation layer will flag. Latent.

**m6. `computeDistanceToCamera` uses `transform.position` while `FrameGraphRenderer` uses
`worldMatrix[3]`.** `EntityRenderExecutor.cpp:192` vs `FrameGraphRenderer.h:248`. The former
ignores parent transforms, so sorting is wrong for any child entity. Latent, but the two
query implementations differ in several ways — `EntityRenderExecutor::queryEntities`
(`:98-101`) treats `RenderableTagComponent` as optional while `FrameGraphRenderer` requires
it (`FrameGraphRenderer.cpp:46-51`), so the same scene yields different entity sets.

**m7. `calculateDistance` is computed even for `EntitySortMode::None`.**
`FrameGraphRenderer.cpp:84` — an unconditional matrix column read + dot product per entity.
`EntityRenderExecutor.cpp:134` correctly guards it.

**m8. Image readback divides by `width * height` without a zero check.**
`StagingBufferManager.cpp:488-489`. A zero-extent tracked render target divides by zero.

**m9. `uploadBuffer` does a full `vkQueueWaitIdle` per call.**
`GPUResourceFactory.cpp:154-155`, likewise `createTexture2DWithData` at `:322-323`. Loading
a glTF scene with N buffers is N full GPU stalls. Acceptable for load-time, worth noting.

**m10. `generateMipmaps` hardcodes `layerCount = 1`** (`GPUResourceFactory.cpp:579`) so it
cannot be used on the cubemaps `createTextureCube` produces, and it never checks
`VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` before `vkCmdBlitImage` (`:616`).

**m11. `createSampler` sets `anisotropyEnable` from the argument without consulting
`VkPhysicalDeviceFeatures::samplerAnisotropy` or `maxSamplerAnisotropy`**
(`GPUResourceFactory.cpp:466-467`).

**m12. `createTexture2D` returns a `GPUTexture` with a null sampler**, so `isValid()`
(`GPUTypes.h:97`) is false for everything it produces; only `createSolidColorTexture` attaches
one (`:683`). Callers must know to fill it in (`FontLoader.cpp:103` does).

**m13. VMA usage flags are the deprecated set** (`VMA_MEMORY_USAGE_GPU_ONLY`,
`CPU_TO_GPU`, `CPU_ONLY` — `GPUResourceFactory.cpp:54, 80, 93, 116`). VMA 3.x wants
`VMA_MEMORY_USAGE_AUTO` + `VMA_ALLOCATION_CREATE_HOST_ACCESS_*` flags.

**m14. Default metallic-roughness texture disagrees with its own documentation.**
`GPUResourceFactory.h:205` says `(0, 128, 0, 255)`; `GPUResourceFactory.cpp:715-716` creates
`(255, 128, 0, 255)`. The code is the sensible one (R=occlusion=1); the header comment is wrong.

**m15. Header claims persistently-mapped staging; implementation maps and unmaps per write.**
`StagingBufferManager.h:7-8` says "persistently mapped for zero-overhead CPU access", but
`writeUploadData` (`:146`) calls `VulkanBuffer::update`, which is
map → memcpy → flush → unmap (`VulkanBuffer.cpp:135-146`). Same for the readback path
(`StagingBufferManager.cpp:299-304`).

**m16. `SharedBufferRegistry` version semantics are odd and unenforced.**
`SharedBufferRegistry.cpp:15-17` discards the caller's `entry.version` and sets
`prevVersion + 1`, but on first insert (`:19`) it keeps whatever the caller passed. Nothing
in the codebase reads `getVersion()` to detect a stale handle. `getBuffer()` returns a raw
pointer into the map, which `unregisterBuffer`/`clear` invalidate with no notification, and
the stored `VkBuffer`s are non-owning by design — so a producer graph recompile leaves
consumers holding destroyed handles. No thread-safety (documented nowhere).

**m17. `SceneContext::updateFromRegistry` warns once, statically.**
`DotPathResolver.cpp:51-55` uses a function-local `static bool` — not thread-safe for the
write, and it suppresses the warning for the entire process lifetime, including after a scene
reload. Camera matrices retain their previous values when no main camera is found.

## Performance notes

Concrete per-frame hot paths, worst first:

1. **`RenderGraph.cpp:2042-2078` — layout rebuild per draw.** ~2 string allocations per field
   per draw plus vector allocations. See M1. This is the dominant CPU cost of the entity
   path and it is entirely avoidable.
2. **`DotPathResolver.cpp:119-133` — `splitPath` allocates a `std::vector<string_view>` on
   every resolve.** One allocation per field per resolve; for push constants that is per
   field per draw. Could be a fixed `std::array<string_view, 6>` + count with zero allocations.
3. **`DotPathResolver.cpp:337, 357` — `std::string paramName(parts[2])`** constructs a
   string purely to index `material->params` / `material->textures`, then hashes it. A
   transparent hash (`std::unordered_map<..., ..., string_hash, std::equal_to<>>`) removes
   the allocation.
4. **`DotPathResolver.cpp:531-539` — array expansion allocates ~4 strings per element per
   frame**: `std::string expanded = field.source`, `std::to_string(i)`, the `"[" + ... + "]"`
   temporary, and `BufferField elemField = field` (which copies `name` and `source`). For 16
   lights × 4 fields that is ~256 allocations per frame for data that could be resolved once
   into a `PackedLight` blit.
5. **`RenderGraph.cpp:269` — `std::vector<uint8_t> data(ubo.size, 0)` per UBO per frame.**
   Small, but it is a fresh allocation + zero-fill of a buffer that is fully overwritten
   (except for padding) immediately after.
6. **`FrameGraphRenderer.cpp:38-117` / `EntityRenderExecutor.cpp:83-156` — a fresh
   `std::vector<RenderableEntity>` per pass per frame.** `FrameGraphRenderer` at least
   reserves (`:54`); `EntityRenderExecutor` does not. With N geometry passes the whole ECS
   view is re-walked and re-sorted N times per frame; nothing is shared between passes.
7. **`std::sort` per pass** (`FrameGraphRenderer.cpp:92-109`) — comparison sort on 40-byte
   structs, no radix key, no persistence across frames. Fine at hundreds of entities,
   noticeable at tens of thousands.
8. **String-keyed map lookups per draw**: `getBufferLayout(pushConstantLayout)`
   (`RenderGraph.cpp:2030`) hashes the layout name string on every draw; likewise
   `bindMaterialTextures` and `bindSkeletonSSBO`. Layouts should be resolved to indices at
   compile time.
9. **`vkCmdBindVertexBuffers` + `vkCmdBindIndexBuffer` per entity** even when consecutive
   entities share a mesh (`FrameGraphRenderer.cpp:227-232`). No state-change dedup.

No O(n²) behaviour found in the per-frame path. The O(n·m) `std::find_if` over
`builderLayouts` at `RenderGraph.cpp:183` and `:308` is compile-time only.

## Test coverage

**Covered:**
- `DotPathResolverTest.cpp` — path-root classification (7 cases), `ResolvedValue`
  construction/`byteSize`/`tryAs`/`copyTo` for all variants, `const.*` scalar and vecN
  parsing, `validatePath` happy/sad paths. Pure, no GPU.
- `DotPathResolverECSTest.cpp` — `SceneContext::updateFromRegistry` (camera extraction,
  light collection, MAX_SCENE_LIGHTS cap), scene camera/time/screen/lights/custom resolution,
  entity transform/material-param/alphaCutoff/alphaMode/skeleton resolution, null-entity and
  cross-root rejection, and three `BufferLayoutResolver` fill tests.
- `BufferLayoutCompilerTest.cpp` — ~30 tests on `parseType`, `getTypeSize`,
  `getTypeAlignment`, `parsePackingRule`, and vec3/float/mat3/mat4 std140 padding.
- `SharedBufferRegistryTest.cpp` — register/get/unregister/version/clear for buffers and
  images, separate namespaces.

**Not covered — the gaps that matter:**
- **The compiler that actually ships.** All 30+ packing tests target
  `BufferLayoutCompiler`, which no production code includes.
  `FrameGraphCompiler::compileBufferLayouts` (`FrameGraphCompiler.cpp:1596`) — the one that
  computes the offsets the GPU sees — has no unit test here.
- **C1 (type mismatch overflow).** No test writes a value whose resolved type differs from
  the declared type. All three fill tests use matching types.
- **C3 (mat3 padding).** No test ever fills a mat3 field and inspects the resulting bytes.
  `Compile_Mat3_Std140_EffectiveSize48` checks only the *offsets*, never the payload.
- **Array expansion.** `fillSceneBuffer`'s `[i]` branch (`DotPathResolver.cpp:527-541`) —
  the path that feeds every light in every shipped pipeline — is untested. So is the fact
  that `fillEntityBuffer` lacks it.
- **`scene.lights[N].*`, `scene.environment.*`, `entity.material.textures.*`,
  `entity.mesh.*`, `entity.transform.rotation`, `scene.camera.positionVec4`,
  `nearFarFovAspect`, `invView`, `invProj`** — none resolved in any test.
- **`StagingBufferManager` — zero tests.** No coverage of ring slot arithmetic, the
  `shouldUpload`/`shouldReadback` frequency matrix, pending-frame bookkeeping, or
  `pollReadbacks`. Several of these are pure logic and testable without a device if the
  `VulkanBuffer` dependency were injected.
- **`EntityRenderExecutor`, `FrameGraphRenderer` — zero tests.** `passesFilter` and
  `executionTypeToFilter`/`sortModeStringToEnum` are pure functions on plain structs and
  need no Vulkan; the filter-matrix divergence between the two `queryEntities`
  implementations (m6) would be caught immediately by a shared table test.
- **`GPUResourceFactory` — zero tests.** `calculateMipLevels` and `getFormatSize` are pure
  and untested; `getFormatSize` returns 4 for every unlisted format including BC/ASTC
  compressed ones (`GPUResourceFactory.cpp:773-774`), which silently mis-sizes staging
  buffers.
- **`SharedBufferRegistry` version test is vacuous.** `GetVersion_IncrementOnReRegister`
  (`SharedBufferRegistryTest.cpp:62-73`) sets `entry.version = 1` before re-registering, so
  it passes whether or not the implementation increments.

## Open questions

1. Are `EntityRenderExecutor` and `BufferLayoutCompiler` intended as a deprecated first
   iteration to be deleted, or as a public API for embedders? If the former, removing them
   drops ~1000 lines and eliminates C2/M9/m5/m6 outright. If the latter, they need the fixes
   and the tests need to move to the live compiler as well.
2. Is `writeUploadData` meant to be exposed through the Python/C++ engine API? The
   declarative-SSBO design doc describes `"source": {"type": "custom"}` per-frequency uploads
   (`docs/old/declarative_ssbo_data_flow.md`), which is presumably what would call it. Until
   then `cfg.hasUpload` should probably not allocate the ring.
3. Was `const.` intended to support decimals? `const.0.5` is a natural thing to write and
   currently yields `vec2(0,5)` and (with C1) an 8-byte write. A `const.` grammar change is
   breaking; an alternative is a compile-time diagnostic when the declared type is scalar and
   the const has dots.
4. Is `mat3` used by any shipped shader today? I found no `"type": "mat3"` in
   `assets/**/*.json`, which would mean C3 is latent rather than active — worth confirming
   against any user pipelines outside the repo.
5. `getExpectedType` (`DotPathResolver.cpp:494-515`) infers types from substring matches on
   the path name ("Matrix" → mat4, "Color" → vec4). It has no callers. Is it a stub for a
   planned compile-time type check against the declared field type? That check is exactly
   what would prevent C1.
