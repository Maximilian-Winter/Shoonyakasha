# Object leaks at `vkDestroyDevice` — investigation

**Status:** all four causes fixed, plus a fifth the fixes exposed. Every example
now reaches `vkDestroyDevice` with no object-tracking report at all.

None of these were regressions from the 2026-08-01 remediation work — they predate
it and only became *observable* when the teardown crash was fixed, because
validation's object tracker runs at `vkDestroyDevice` and no example previously
reached that call.

After the fixes, with the message limit lifted, all six runnable examples report
nothing: `declarative_sponza_test`, `physics_test`, `skinned_mesh_test`,
`particle_test`, `bloom_test` and `pbr_physics_particles` each exit 0 with zero
validation messages of any kind.

---

## How to reproduce

Validation caps repeated messages at ten per VUID, and all object-leak reports
share `VUID-vkDestroyDevice-device-05137`. The first runs therefore showed ten
messages and nothing else, which made the counts look smaller and the pattern
look different than it is.

Drop a `vk_layer_settings.txt` next to the executable's working directory:

```
khronos_validation.duplicate_message_limit = 0
```

then run and count:

```sh
grep -o "Vk[A-Za-z]* 0x[0-9a-f]* has not been destroyed" run.out \
  | awk '{print $1}' | sort | uniq -c
```

Handle values are deterministic across runs on this driver, and their low bits
are a global allocation ordinal — consecutive handles mean consecutively created
objects, which is what allowed each group below to be matched to its creator
without instrumenting the build.

## Measured counts, before the fixes

With the message limit lifted. This is the table the diagnosis below was built
from; everything in it is now zero.

| Example | Buffer | DescriptorPool | DescriptorSet | Image | ImageView | Sampler |
|---|---|---|---|---|---|---|
| `declarative_sponza_test` | 2 | 1 | 2 | 4 | 4 | 4 |
| `physics_test` | 22 | 1 | 22 | 4 | 4 | 4 |
| `skinned_mesh_test` | 2 | 1 | 4 | 5 | 5 | 5 |
| `particle_test` | — | — | — | — | — | — |
| `bloom_test` | — | — | — | — | — | — |

The earlier note in `00-summary.md` guessed that the three leaking examples were
the three that generate IBL. That was wrong. IBL is clean; the correlation is
that those three are the three that **render entities with materials**.
`particle_test` and `bloom_test` never take that path, so the two largest leak
classes never get created at all.

---

## A. `RenderGraph::m_defaultTextures` is never destroyed

The 4 `VkImage` + 4 `VkImageView` + 4 `VkSampler` in every affected example, and
the reason the count is identical across three otherwise unrelated apps.

- Created: `src/Vulkan/FrameGraph/RenderGraph.cpp:2161`, lazily on the first
  material bind, guarded by `m_defaultTexturesCreated`.
- Stored: `include/Vulkan/FrameGraph/FrameGraph.h:1294` — a `DefaultTextures`
  **value member**. `DefaultTextures` is a plain aggregate of four `GPUTexture`
  values with no destructor, so member destruction frees nothing.
- Destroyed: nowhere. `~RenderGraph` (`RenderGraph.cpp:55-97`) destroys
  framebuffers, compiled samplers and sync primitives, and stops there.

`GPUResourceFactory::destroyDefaultTextures` already exists and is correct
(`src/GPU/GPUResourceFactory.cpp:717`). Its only caller is
`EntityRenderExecutor::~EntityRenderExecutor` (`src/FrameGraph/EntityRenderExecutor.cpp:31`)
— the dead-but-compiled class. The live path never had a matching teardown.

**Fixed.** One `destroyDefaultTextures` call in `~RenderGraph`, guarded by
`m_defaultTexturesCreated`. The textures are engine-owned and not handed out
beyond descriptor writes, which are gone by then.

## B. `RenderGraph::m_materialDescriptorPool` is never destroyed

The 1 `VkDescriptorPool` in every affected example, and with it every set
allocated from it (2, 22 and 4 respectively — sets are freed implicitly when the
pool is destroyed, so the pool is the only real leak).

- Created: `RenderGraph::createMaterialDescriptorPool`,
  `src/Vulkan/FrameGraph/RenderGraph.cpp:2073`.
- Destroyed: `RenderGraph.cpp:2057` only — the guard at the *top of the same
  function*, which frees the previous pool before making a new one. That handles
  re-creation and nothing else.
- `~RenderGraph` does not touch it.

**Fixed.** Destroyed in `~RenderGraph`. Same shape and same risk as A.

## C. Mesh vertex/index buffers have no owner

The `VkBuffer` counts, and the one that scales with content rather than being a
fixed cost.

`MeshComponent` holds `GPUBuffer vertexBuffer` / `indexBuffer` by value. Nothing
frees them:

- `GltfSceneLoader` assigns them into the component at
  `src/Resources/GltfSceneLoader.cpp:687-688` (static) and `:1091-1092`
  (skinned). The only `destroyBuffer` in that file is `:263`, an error path that
  frees the pre-skinning vertex buffer after building the skinned one.
- There is no `on_destroy<MeshComponent>` hook. The only ECS destroy hooks in the
  engine are `PhysicsSystem.cpp:167` (rigid bodies) and
  `SkeletalAnimationSystem.cpp:148` (bone SSBOs).
- `~ResourceManager` (`src/Resources/ResourceManager.cpp:313`) only waits on the
  async loader.

The counts line up exactly: `declarative_sponza_test` loads `Box.gltf` — one
primitive, 2 buffers. `skinned_mesh_test` — one primitive, 2 buffers.
`physics_test` builds eleven meshes itself and leaks 22.

**Fixed by giving GPU buffers an owner**, not by adding a destroy hook. Two
things ruled the hook out:

- `MeshComponent` is copyable. A hook is safe only as long as no two components
  name the same allocation — true today because the loader bakes each node's
  transform into its own vertex buffer, but any copy of the component or the
  entity turns it into a double free.
- Freeing on component destruction frees mid-frame, while a command buffer
  recorded one or two frames ago may still name the buffer.

`include/GPU/GpuDeleteQueue.h` addresses both:

- `GpuBufferRef` is `std::shared_ptr<const GPUBuffer>`. That is the reference
  count. `MeshComponent`, `GltfPrimitive` and `SkeletonComponent::boneSSBO` all
  hold one, so copying a component shares the allocation instead of aliasing a
  raw handle, and two entities drawing identical geometry cost one buffer.
- Dropping the last reference does not free: it *retires* the allocation with the
  current frame index. `GpuDeleteQueue::beginFrame()`, called from
  `ApplicationBase::render()`, releases anything that has survived
  `maxFramesInFlight` frames. Nothing is freed while a frame that named it could
  still be in flight.
- `VulkanDevice` owns the queue and declares it after the allocator, so it is
  destroyed first and can still free what it holds.

Three deliberate escape hatches, since automatic is not always what you want:

- `MeshComponent::release()` drops one component's claim. If it was the last, the
  buffers retire normally.
- `GpuDeleteQueue::flush()` frees everything pending immediately. The caller owns
  the synchronisation — normally `vkDeviceWaitIdle`. For level transitions where
  reclaiming the memory now matters more than not stalling.
- `borrowBuffer()` wraps a buffer *without* taking ownership, for allocations
  whose lifetime is managed elsewhere, and for tests with no device.

`pendingCount()` and `liveCount()` report retired-not-yet-freed and
adopted-and-still-referenced, which is the counting mechanism made visible.

`examples/instancing_test` demonstrates all of it and checks it: run it with
`--selftest` and it drives spawn, destroy, release and flush on a timer,
asserting the buffer counts after each.

### A fifth leak this exposed

With C fixed, `skinned_mesh_test` still leaked exactly one buffer: the bone SSBO.
`SkeletalAnimationSystem` had an `on_destroy<SkeletonComponent>` hook to free it —
added earlier in this remediation — and the hook was never firing, which is a neat
demonstration of why the hook approach was rejected for C. The hook only works
while the system is alive, and `SkinnedMeshApp` owns the system as its own member,
so `~SkinnedMeshApp` released the connection before `~ApplicationBase` destroyed
the scene. `SkeletonComponent::boneSSBO` is a `GpuBufferRef` now and the hook is
gone; `attachTo()` is kept as a documented no-op.

## D. `GltfSceneLoader::m_textureCache` is cleared without destroying

The extra image/view/sampler in `skinned_mesh_test` (5 rather than 4 — its glTF
has one texture; `Box.gltf` has none, which is why the other two examples show
exactly the four default textures).

- `include/Resources/GltfSceneLoader.h:155` —
  `std::unordered_map<std::string, GPUTexture> m_textureCache`, by value.
- `src/Resources/GltfSceneLoader.cpp:91` — `m_textureCache.clear()` as the first
  statement of `load()`. That drops the handles without destroying anything, so
  **loading a second scene leaks every texture of the first**.
- `~GltfSceneLoader() = default` (`:79`) — nothing at shutdown either.

Ownership here is unambiguous: the cache is the sole owner and deliberately
shares textures across primitives by cache key.

**Fixed — but not by destroying at the `clear()` site**, which is the obvious move
and is wrong. Entities from an earlier load still hold those views and samplers in
their materials, so freeing them when a *second* scene loads trades a leak for a
dangling handle. The cache now lives for the loader's lifetime and is destroyed
only in `~GltfSceneLoader`, which in `ApplicationBase` runs after the scene that
holds the materials is already gone.

Two consequences that had to be handled:

- The embedded-texture cache key was `"embedded_bv_" + (uintptr_t)buffer_view` —
  a pointer into the `cgltf_data` that `load()` frees before returning. That was
  survivable only because the cache was wiped each load; with a cache that spans
  loads, a later allocation landing on the same address returns an unrelated
  texture. Re-keyed on the source file path plus the view's offset and size.
- `GltfLoadResult::totalTextures` was `m_textureCache.size()`, which meant
  "this load's textures" only because of the wipe. Now counted from a per-load
  key set, so it keeps its meaning and still counts a cache hit.

Also deleted while there: `GltfSceneLoader` declared a
`std::unique_ptr<DefaultTextures> m_defaultTextures` that no line of the
implementation referenced.

---

## Not leaked

Worth recording, since some of it was suspected: IBL resources
(`IBLResources::destroy()` is called from `~ApplicationBase`, and
`~VulkanCubemap` frees image, cube view, face views, mip views and sampler),
`IBLGenerator`'s descriptor pool, frame graph physical images, render passes,
pipelines, and `Sprite2DManager`'s quad buffers and texture cache — that class is
the one place in the codebase that already does this correctly, and is a
reasonable model for the fix to D.

## A note on the diagnostic logs

`Logger` opens with `std::ios::app` (`src/Core/Logger.cpp:11`) and several classes
construct one per instance against the same filename — `VulkanImage` does this in
both constructors (`src/Vulkan/VulkanImage.cpp:27,48`). The result is that
`vulkan_image.log` and friends accumulate across runs and interleave writes from
independent streams. They were misleading enough during this investigation to
mention: creation and destruction counts in them do not balance, and timestamps
from different runs sit next to each other. Delete the logs before a diagnostic
run, or read the validation output directly.
