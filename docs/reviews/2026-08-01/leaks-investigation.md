# Object leaks at `vkDestroyDevice` — investigation

**Status:** diagnosed, not yet fixed. Four independent causes, all confirmed
against a running build. Nothing here is a regression from the 2026-08-01
remediation work — these leaks predate it and only became *observable* when the
teardown crash was fixed, because validation's object tracker runs at
`vkDestroyDevice` and no example previously reached that call.

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

## Measured counts

With the message limit lifted:

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

**Fix:** one call in `~RenderGraph`, guarded by `m_defaultTexturesCreated`.
Low risk; the textures are engine-owned and not handed out beyond descriptor
writes, which are gone by then.

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

**Fix:** destroy it in `~RenderGraph`. Same shape and same risk as A.

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

**This one is not a one-line fix**, and that is why it is worth writing down
rather than patching in passing:

- An `on_destroy<MeshComponent>` hook is the obvious answer and is *currently*
  safe — the loader bakes each node's transform into its own vertex buffer, so no
  two entities share a primitive's buffers today. But `MeshComponent` is
  copyable, so a user copying the component or the entity gets an aliasing
  double-free. Making the buffers a shared, ref-counted handle is the version
  that stays correct.
- It also needs a decision about *when*: freeing on component destruction means
  freeing possibly mid-frame, while the buffer may still be referenced by an
  in-flight command buffer. A deferred-delete queue keyed on frame index is the
  usual answer, and the engine does not have one.

Suggested order: fix A and B first (contained, no design question), then treat C
as its own piece of work.

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

Unlike C, the ownership here is unambiguous: the cache is the sole owner and
deliberately shares textures across primitives by cache key. A `destroyTexture`
loop before the `clear()` and in the destructor is the whole fix.

Related, and worth deleting while there: `include/Resources/GltfSceneLoader.h:255`
declares a `std::unique_ptr<DefaultTextures> m_defaultTextures` that is never
referenced anywhere in the implementation.

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
