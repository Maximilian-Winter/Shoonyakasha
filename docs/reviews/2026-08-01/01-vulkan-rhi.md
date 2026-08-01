# Vulkan RHI Layer Review — Shoonyakasha

## Scope

Read in full (17 headers + 15 translation units, ~6,700 LOC):

`include/Vulkan/{VulkanInstance,VulkanDevice,VulkanSwapChain,VulkanMemoryAllocator,VulkanBuffer,VulkanImage,VulkanTexture,VulkanCubemap,VulkanDescriptorSystem,VulkanPipeline,VulkanComputePipeline,VulkanRenderPass,VulkanCommandBuffer,VulkanWindow,UniformBuffer,VertexTypes}.h` and the matching `src/Vulkan/*.cpp`.

Excluded per instructions: `src/Vulkan/FrameGraph/`, `include/Vulkan/FrameGraph/`, `third_party/`, build dirs. Cross-file greps into `src/App/`, `src/FrameGraph/` and `examples/` were used only to determine whether a suspected bug is reachable; those layers are not reviewed here.

---

## What this layer does (features)

**API version and extensions**

- Instance `apiVersion = VK_API_VERSION_1_0` (`src/Vulkan/VulkanInstance.cpp:57`).
- Instance extensions: whatever GLFW requires, plus `VK_EXT_debug_utils` when validation is requested (`src/Vulkan/VulkanInstance.cpp:105-117`).
- Device extensions: `VK_KHR_swapchain` only (`include/Vulkan/VulkanDevice.h:88-90`).
- VMA configured with `vulkanApiVersion = VK_API_VERSION_1_0` (`src/Vulkan/VulkanMemoryAllocator.cpp:21`).

**Device features requested** (`src/Vulkan/VulkanDevice.cpp:100-113`): `largePoints`, `wideLines`, `samplerAnisotropy`, and `VkPhysicalDeviceVulkan12Features::timelineSemaphore` chained via `pNext`. A `VkPhysicalDeviceTimelineSemaphoreFeatures` struct is built at `:106-108` and then never chained — dead code.

**Queues** (`src/Vulkan/VulkanDevice.cpp:249-312`): graphics, present, dedicated-compute (preferring a COMPUTE-without-GRAPHICS family, falling back to any compute family, falling back to graphics). A `transferFamily` is discovered at `:299-309` but never used anywhere — no transfer queue is created and no queue is retrieved for it. One queue per family, priority 1.0. Separate compute command pool when the compute family differs (`:171-189`).

**Presentation** (`src/Vulkan/VulkanSwapChain.cpp`): prefers `B8G8R8A8_SRGB` + `SRGB_NONLINEAR`, else `availableFormats[0]` (`:298-306`); prefers `MAILBOX`, falls back to `FIFO` (`:308-316`); `CONCURRENT` sharing when graphics != present (`:88-94`). Depth buffer owned by the swapchain, format chosen from `{D32_SFLOAT, D32_SFLOAT_S8_UINT, D24_UNORM_S8_UINT}` via `vkGetPhysicalDeviceFormatProperties` (`:197-203`, `src/Vulkan/VulkanDevice.cpp:215-231`) — this is the one place real format-support querying happens.

**Rendering model**: classic `VkRenderPass` + `VkFramebuffer`. No dynamic rendering, no `VK_KHR_synchronization2`, no timeline semaphores actually used (the feature is enabled but no `vkCreateSemaphore` with a timeline type exists in this layer), no descriptor indexing / bindless, no pipeline cache (`VK_NULL_HANDLE` at `src/Vulkan/VulkanPipeline.cpp:462` and `src/Vulkan/VulkanComputePipeline.cpp:113`), no secondary command buffers in practice (the level is a parameter but nothing records into secondaries), no query pools / timestamps, no push descriptors.

**Builders**: fluent `PipelineStateBuilder`, `RenderPassBuilder`, `DescriptorLayoutBuilder`, `VulkanCommandBuilder`. Named-attachment/named-binding indirection throughout (string → index maps).

**Cubemaps** (`src/Vulkan/VulkanCubemap.cpp`): the only mip-aware code in the layer — `CUBE_COMPATIBLE` images, per-face-per-mip 2D views for compute writes, per-mip cube views, whole-image and per-mip barrier helpers. Factories for environment/irradiance/prefilter maps.

**Debug**: `vkCmdBeginDebugUtilsLabelEXT` / `End` / `Insert` wrappers (`src/Vulkan/VulkanCommandBuffer.cpp:389-428`).

**Error handling**: consistent — every `VkResult` that is checked throws `std::runtime_error`. Nothing uses assert or returns error codes. But several calls are unchecked: `vkQueueSubmit` + `vkQueueWaitIdle` (`src/Vulkan/VulkanDevice.cpp:357-358`, `src/Vulkan/VulkanCommandBuffer.cpp:524-525`), `vkAllocateCommandBuffers`/`vkBeginCommandBuffer` in `beginSingleTimeCommands` (`src/Vulkan/VulkanDevice.cpp:338,344`), all `vkEnumerate*`/`vkGet*PropertiesKHR` two-call-idiom calls, and `vmaFlushAllocation` (`src/Vulkan/VulkanMemoryAllocator.cpp:118`).

---

## Limitations & missing pieces

- **No MSAA in practice.** `VulkanImage::createImage` hardcodes `VK_SAMPLE_COUNT_1_BIT` (`src/Vulkan/VulkanImage.cpp:82`); `AttachmentDescriptor` factories all hardcode `VK_SAMPLE_COUNT_1_BIT` (`src/Vulkan/VulkanRenderPass.cpp:23,48,69,87`). `PipelineStateBuilder::withMultisampling` exists but there is no multisampled image path to feed it, and `SubpassDescriptor::createMultisampled` is never called.
- **No mipmapping for 2D textures.** `VulkanImage` hardcodes `mipLevels = 1` (`src/Vulkan/VulkanImage.cpp:76`) and `createImageView` hardcodes `levelCount = 1` (`:114`); `VulkanTexture`'s sampler sets `maxLod = 0.0f` (`src/Vulkan/VulkanTexture.cpp:137`). No `vkCmdBlitImage` mip generation anywhere. Only `VulkanCubemap` handles mips.
- **No 3D images, no image arrays, no compressed formats (BCn/ASTC), no sRGB-vs-UNORM view aliasing.**
- **`VulkanImage` cannot represent anything but a single-mip, single-layer 2D color image**, and `transitionImageLayout` supports exactly two transitions (`src/Vulkan/VulkanImage.cpp:146-161`), always with `aspectMask = COLOR_BIT` — it cannot transition the depth image it is also used to create.
- **No staging-buffer reuse / no transfer queue use**: every texture upload does three separate `beginSingleTimeCommands` → `vkQueueSubmit` → `vkQueueWaitIdle` round trips on the graphics queue (`src/Vulkan/VulkanTexture.cpp:105-107`).
- **No descriptor array writes**: `descriptorCount` is hardcoded to 1 (`src/Vulkan/VulkanDescriptorSystem.cpp:467`), so `createStorageArray` / `createTextureArray` layouts can never have elements 1..N written.
- **No device-loss handling** (`VK_ERROR_DEVICE_LOST` is not distinguished anywhere); `DescriptorManager::recreateAll` is described as being for device loss but only recreates descriptor objects.
- **No thread safety.** Every wrapper assumes single-threaded use; `VulkanDevice::beginSingleTimeCommands`/`endSingleTimeCommands` share one `VkCommandPool` with no locking (`src/Vulkan/VulkanDevice.cpp:330-361`). VMA itself is internally synchronized, so allocation is the one safe operation.
- **Hardcoded**: `maxAnisotropy = 16.0f` (`src/Vulkan/VulkanTexture.cpp:129`); `VK_INDEX_TYPE_UINT32` in `DrawCommand` execution (`src/Vulkan/VulkanCommandBuffer.cpp:247`); fullscreen "quad" always 3 vertices (`:264`); color attachment `finalLayout = PRESENT_SRC_KHR` for *every* color attachment (`src/Vulkan/VulkanRenderPass.cpp:29`); `VK_SUBPASS_CONTENTS_INLINE` always.

---

## Findings

| # | Severity | Issue | Location |
|---|---|---|---|
| 1 | CRITICAL | `VulkanPipeline::recreate()` destroys layout + shader modules, recreates neither | `src/Vulkan/VulkanPipeline.cpp:291-295`, `:467-482` |
| 2 | CRITICAL | `reloadShaders()` double-destroys shader modules and loses the layout | `src/Vulkan/VulkanPipeline.cpp:297-306` |
| 3 | CRITICAL | Compute dispatch binds a *graphics* pipeline at the GRAPHICS bind point, then dispatches | `src/Vulkan/VulkanCommandBuffer.cpp:267-280`; `src/Vulkan/VulkanDescriptorSystem.cpp:326` |
| 4 | MAJOR | Validation layers are never actually enabled — `m_validationLayers` is never populated | `include/Vulkan/VulkanInstance.h:29`; `src/Vulkan/VulkanInstance.cpp:69-70,120-143` |
| 5 | MAJOR | `wideLines`/`largePoints` requested unconditionally; `vkCreateDevice` fails where unsupported | `src/Vulkan/VulkanDevice.cpp:100-103,232-247` |
| 6 | MAJOR | Vulkan 1.2 feature struct chained onto a 1.0 instance with no device-version check | `src/Vulkan/VulkanDevice.cpp:110-116,57` |
| 7 | MAJOR | Wireframe pipelines need `fillModeNonSolid`, which is never enabled | `src/Vulkan/VulkanPipeline.cpp:391`; `src/Vulkan/VulkanDevice.cpp:100-103` |
| 8 | MAJOR | RAII wrappers are implicitly copyable → double-destroy of Vulkan handles | `VulkanBuffer.h:16`, `VulkanImage.h:16`, `VulkanTexture.h:18`, `VulkanPipeline.h:153`, `VulkanRenderPass.h:176`, `VulkanDescriptorSystem.h:142` |
| 9 | MAJOR | `VulkanWindow` deletes the `Logger`/`EventDispatcher` its caller passed in | `src/Vulkan/VulkanWindow.cpp:35-36` |
| 10 | MAJOR | Swapchain uses `minImageCount` with no `+1`; the clamp below it is dead code | `src/Vulkan/VulkanSwapChain.cpp:70-73` |
| 11 | MAJOR | Swapchain `recreate()` never refreshes `m_windowExtent`; no zero-extent (minimize) guard | `src/Vulkan/VulkanSwapChain.cpp:213-230,318-327` |
| 12 | MAJOR | `present()` recreates the swapchain, leaving the wait semaphore possibly signaled | `src/Vulkan/VulkanSwapChain.cpp:254-273` |
| 13 | MAJOR | `pResolveAttachments` set without matching `colorAttachmentCount` | `src/Vulkan/VulkanRenderPass.cpp:594-612` |
| 14 | MAJOR | Every color attachment gets `finalLayout = PRESENT_SRC_KHR` | `src/Vulkan/VulkanRenderPass.cpp:29` |
| 15 | MAJOR | `configureDeferred` / `configurePostProcess` presets are incorrectly wired | `src/Vulkan/VulkanRenderPass.cpp:242-281` |
| 16 | MAJOR | `imageBarrier` silently emits `srcAccessMask = 0` for unhandled layouts | `src/Vulkan/VulkanCommandBuffer.cpp:293-325` |
| 17 | MAJOR | `submitCommandBuffers` does not validate `waitStages.size() == waitSemaphores.size()` | `src/Vulkan/VulkanCommandBuffer.cpp:562-584` |
| 18 | MAJOR | `maxAnisotropy = 16.0f` without checking `limits.maxSamplerAnisotropy` | `src/Vulkan/VulkanTexture.cpp:128-129` |
| 19 | MAJOR | `VulkanTexture` staging size uses `m_channels`, not the format's texel size | `src/Vulkan/VulkanTexture.cpp:95` |
| 20 | MAJOR | `VulkanDescriptorSet::recreate()` destroys a live pool/layout with no wait-idle | `src/Vulkan/VulkanDescriptorSystem.cpp:330-335,437-449` |
| 21 | MAJOR | `VulkanComputePipeline::reloadShader()` destroys a possibly in-flight pipeline | `src/Vulkan/VulkanComputePipeline.cpp:72-83` |
| 22 | MAJOR | Constructor-throw leaks: raw `new` members with no cleanup | `VulkanInstance.cpp:19-20`, `VulkanDevice.cpp:23-24`, `VulkanTexture.cpp:22-23,103` |
| 23 | MINOR | First suitable GPU wins — no discrete-GPU preference | `src/Vulkan/VulkanDevice.cpp:63-68` |
| 24 | MINOR | `graphicsFamily`/`presentFamily` take the *last* matching family | `src/Vulkan/VulkanDevice.cpp:259-273` |
| 25 | MINOR | Each wrapper allocates a private `EventDispatcher` nobody subscribes to | many; e.g. `VulkanDevice.cpp:24,155` |
| 26 | MINOR | `transitionMipLayout` is defined outside the namespace block | `src/Vulkan/VulkanCubemap.cpp:298-299` |
| 27 | MINOR | `VulkanRenderPass::begin()` accepts a `subpass` argument it ignores | `src/Vulkan/VulkanRenderPass.cpp:399-417` |
| 28 | MINOR | `glfwInit()` in `VulkanInstance`, `glfwTerminate()` in `~VulkanWindow` | `VulkanInstance.cpp:23`; `VulkanWindow.cpp:33` |
| 29 | MINOR | No SPIR-V size/alignment validation before `vkCreateShaderModule` | `VulkanPipeline.cpp:498-513`; `VulkanComputePipeline.cpp:149-162` |
| 30 | MINOR | Weak XOR-shift hash combiner for `Vertex` | `include/Vulkan/VertexTypes.h:19-35,57-69` |
| 31 | MINOR | `m_ownImage` declaration/initialisation order mismatch (`-Wreorder`) | `include/Vulkan/VulkanImage.h:37-47`; `src/Vulkan/VulkanImage.cpp:14-24` |
| 32 | MINOR | Duplicate `#pragma once`; unused members (`m_nextBinding`, `m_bufferCount`, `transferFamily`) | `VulkanPipeline.h:5-7`, `VulkanRenderPass.h:5-6`; `VulkanDescriptorSystem.h:133`; `UniformBuffer.h:59` |

### Detail

**1 & 2 — CRITICAL — pipeline recreation is broken.**
`cleanup()` (`src/Vulkan/VulkanPipeline.cpp:467-482`) destroys three things: `m_pipeline`, `m_pipelineLayout`, and every entry in `m_shaderModules` (clearing the map). `recreate()` (`:291-295`) calls `cleanup()` and then only `createPipeline()`. At that point `m_pipelineLayout == VK_NULL_HANDLE` and `m_shaderModules` is empty, so `createPipeline()` builds a `VkGraphicsPipelineCreateInfo` with `stageCount = 0` and `layout = VK_NULL_HANDLE` (`:448,458`) — invalid, and `vkCreateGraphicsPipelines` will fail and throw. `reloadShaders()` (`:297-306`) is worse: it destroys the modules, clears the map, calls `loadShaders()` to rebuild them, then calls `recreate()` which destroys them *again* and drops the layout.

The fix is for `recreate()` to rebuild the layout and reload shaders, or for `cleanup()` to be split into "pipeline only" and "everything". Note the sibling class gets this right: `VulkanComputePipeline::reloadShader()` (`src/Vulkan/VulkanComputePipeline.cpp:72-83`) deliberately preserves the layout.

Reachability: `grep` finds no caller of either method in the repository, so this is latent — it is a public API that cannot work, not a live crash.

**3 — CRITICAL — the compute path in the command builder cannot work.**
`ComputeDispatchCommand::computePipeline` is typed `std::shared_ptr<VulkanPipeline>` (`include/Vulkan/VulkanCommandBuffer.h:122`) — the *graphics* pipeline class. `VulkanCommandBuilder::dispatch` (`src/Vulkan/VulkanCommandBuffer.cpp:272`) calls `bindPipeline(command.computePipeline)`, which reaches `VulkanPipeline::bind` → `vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ...)` (`src/Vulkan/VulkanPipeline.cpp:288`), and then issues `vkCmdDispatch` at `:278`. Dispatching with no pipeline bound to `VK_PIPELINE_BIND_POINT_COMPUTE` is undefined behaviour.

The same mistake is baked into descriptor binding: `VulkanDescriptorSet::bind` hardcodes `VK_PIPELINE_BIND_POINT_GRAPHICS` (`src/Vulkan/VulkanDescriptorSystem.cpp:326`), so the `createCompute()` descriptor preset can never be bound correctly either. `VulkanComputePipeline` — the class that *does* bind to the compute point (`src/Vulkan/VulkanComputePipeline.cpp:65`) — is not reachable through `VulkanCommandBuilder` at all.

**4 — MAJOR — validation layers are silently never enabled.**
`m_validationLayers` is declared (`include/Vulkan/VulkanInstance.h:29`) and never assigned — a repo-wide grep confirms only reads. Consequences: `checkValidationLayerSupport()` (`src/Vulkan/VulkanInstance.cpp:120-143`) iterates an empty vector and returns `true` unconditionally; `createInfo.enabledLayerCount` is set from `.size()` == 0 (`:69`). The instance is created with **zero** layers. `VK_EXT_debug_utils` is still requested and the messenger still installs successfully (`:87-103`), so the code path looks healthy and the log file is created — but `VK_LAYER_KHRONOS_validation` is never loaded, so no validation message can ever be produced. The same empty vector is forwarded to `vkCreateDevice` (`src/Vulkan/VulkanDevice.cpp:123-125`).

This is the finding I would fix first: it is a one-line change, and it is why most of the other findings in this list have gone unnoticed.

**5 — MAJOR — unconditional feature requests.**
`src/Vulkan/VulkanDevice.cpp:100-103` sets `largePoints`, `wideLines` and `samplerAnisotropy` to `VK_TRUE`. `isDeviceSuitable` (`:232-247`) checks only `samplerAnisotropy`. `vkCreateDevice` returns `VK_ERROR_FEATURE_NOT_PRESENT` if any requested feature is unsupported, so on a device without `wideLines` the engine throws at startup with "failed to create logical device". `wideLines` is not supported on MoltenVK/Metal, and is absent on a good deal of mobile hardware. The comment at `:102` calls it "nice-to-have", which is exactly the case that should be conditional.

**6 — MAJOR — Vulkan 1.2 features on a 1.0 instance.**
`appInfo.apiVersion = VK_API_VERSION_1_0` (`src/Vulkan/VulkanInstance.cpp:57`) while `createInfo.pNext = &vulkan12Features` (`src/Vulkan/VulkanDevice.cpp:116`). `VkPhysicalDeviceVulkan12Features` requires the physical device to support Vulkan 1.2; nothing queries `VkPhysicalDeviceProperties::apiVersion` or calls `vkGetPhysicalDeviceFeatures2` to confirm `timelineSemaphore` is available. On a Vulkan 1.1 device this fails device creation. Declaring `apiVersion = VK_API_VERSION_1_2` (and gating on the device's reported version) is the consistent choice — and would also let VMA use its 1.1+/1.2+ paths instead of the 1.0 path it is pinned to at `src/Vulkan/VulkanMemoryAllocator.cpp:21`.

Uncertain sub-point: because the instance is 1.0, some loaders will also warn about `vkGetPhysicalDeviceFeatures2` vs the KHR alias. Enabling validation (finding 4) would settle exactly which of these the driver objects to.

**7 — MAJOR — wireframe without `fillModeNonSolid`.**
`rasterizer.polygonMode = m_state.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL` (`src/Vulkan/VulkanPipeline.cpp:391`). `fillModeNonSolid` is never requested in `VulkanDevice.cpp:100-103`, so this violates VUID-VkPipelineRasterizationStateCreateInfo-polygonMode-01507. Unlike findings 1-3 this one *is* reachable outside the RHI layer: `src/Vulkan/FrameGraph/FrameGraphCompiler.cpp:1224` calls `builder.withWireframe()` when a pass declares it, and `VulkanPipeline::createWireframe` (`src/Vulkan/VulkanPipeline.cpp:256`) exists as a public factory.

**8 — MAJOR — copyable RAII wrappers.**
`VulkanCubemap` (`include/Vulkan/VulkanCubemap.h:39-42`) and `VulkanComputePipeline` (`include/Vulkan/VulkanComputePipeline.h:30-33`) correctly delete/define copy and move. `VulkanMemoryAllocator` deletes copy (`include/Vulkan/VulkanMemoryAllocator.h:41-42`). Every other wrapper — `VulkanBuffer`, `VulkanImage`, `VulkanTexture`, `VulkanPipeline`, `VulkanRenderPass`, `VulkanDescriptorSet`, `VulkanCommandManager`, `VulkanInstance`, `VulkanDevice`, `VulkanSwapChain`, `VulkanWindow` — has a user-declared destructor and no copy control, so the implicit copy constructor is still generated (deprecated but legal). Copying any of them yields two objects owning the same `VkBuffer`/`VkImage`/`VmaAllocation`/`VkPipeline` and a double-destroy at scope exit. Several of these hold reference members (`VulkanDevice& m_device`), which suppresses copy *assignment* but not copy *construction*.

No copy currently happens in-tree (everything is held via `unique_ptr`/`shared_ptr`), so this is latent. It is cheap insurance to delete them explicitly.

**9 — MAJOR — `VulkanWindow` deletes pointers it does not own.**
The constructor takes `EventDispatcher* eventDispatcher, Logger* logger` from the caller (`include/Vulkan/VulkanWindow.h:19`) and stores them (`src/Vulkan/VulkanWindow.cpp:20-21`); the destructor then `delete`s both (`:35-36`). If the caller owns those objects — or shares them with another subsystem, which is the obvious reason to pass them in rather than construct them — this is a double-free or a use-after-free. Every other class in this layer constructs its own logger with `new`, so `VulkanWindow` is the odd one out in both directions: it is the only class that receives them, and it deletes them anyway.

**10 — MAJOR — swapchain image count.**
`uint32_t imageCount = swapChainSupport.capabilities.minImageCount;` (`src/Vulkan/VulkanSwapChain.cpp:70`). The conventional `+ 1` is missing. With `minImageCount == 2`, double buffering means `vkAcquireNextImageKHR` will block on the presentation engine, and `MAILBOX` (which the code prefers, `:310`) degrades toward FIFO behaviour. The guard immediately below (`:71-73`) tests `imageCount > maxImageCount`, which can never be true when `imageCount == minImageCount` — dead code that reads as though clamping were happening.

**11 — MAJOR — resize correctness.**
`recreate()` (`:213-230`) calls `cleanup()` then `init()`, but never updates `m_windowExtent`. This is masked on Windows because `chooseSwapExtent` returns `capabilities.currentExtent` whenever it is not `UINT32_MAX` (`:318-321`). On platforms where the surface reports `0xFFFFFFFF` — Wayland notably — the `else` branch clamps the *stale* constructor-time extent, so the swapchain never actually changes size. Separately, there is no guard for a zero extent: minimizing a window yields `currentExtent == {0,0}`, and `vkCreateSwapchainKHR` with a zero extent is invalid. The usual `while (width == 0 || height == 0) glfwWaitEvents();` loop is absent from both this class and `VulkanWindow`.

**12 — MAJOR — semaphore state across recreation.**
`present()` calls `recreate()` on `VK_ERROR_OUT_OF_DATE_KHR` *or* `VK_SUBOPTIMAL_KHR` (`:267-268`). When `vkQueuePresentKHR` fails, whether `renderFinishedSemaphore` was consumed is not well-defined across implementations, and `recreate()`'s `vkDeviceWaitIdle` does not reset semaphore state. The next frame may then wait on an already-signaled or never-to-be-signaled semaphore. `acquireNextImage` has the mirror-image problem: on `OUT_OF_DATE` it recreates and returns `UINT32_MAX` as a sentinel (`:243-245`), which callers must remember to test — a sentinel return rather than a status enum.

I have marked this MAJOR rather than CRITICAL because the actual frame-pacing and semaphore ownership live in `src/App/ApplicationBase.cpp`, outside this review's scope; whether the hazard is hit depends on that code. Enabling validation would show it immediately as a "semaphore is being waited on with no way to be signaled" error.

**13 — MAJOR — resolve attachment count.**
`desc.pResolveAttachments = resolveRefs[i].empty() ? nullptr : resolveRefs[i].data();` (`src/Vulkan/VulkanRenderPass.cpp:612`) with no relation enforced to `colorAttachmentCount` (`:610`). The spec requires `pResolveAttachments`, when non-NULL, to point to exactly `colorAttachmentCount` elements. `SubpassDescriptor::createMultisampled` (`:116-130`) accepts independent `colorTargets` and `resolveTargets` vectors and `validateConfiguration` (`:289-323`) only checks that names resolve — never that the counts match. A caller passing one resolve target for two color targets causes the driver to read past the end of the vector.

**14 & 15 — MAJOR — render pass presets.**
`createColorAttachment` unconditionally sets `finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` (`:29`). That is right for the one swapchain attachment and wrong for every G-buffer, offscreen or intermediate target — all of which are created through the same factory. `configureDeferred` (`:242-266`) consequently declares G-buffer attachments that transition to `PRESENT_SRC_KHR` at the end of the pass, then reads them as input attachments in the lighting subpass whose refs use `SHADER_READ_ONLY_OPTIMAL` (`:589`); its `createColorDependency` (`:152-164`) synchronizes `COLOR_ATTACHMENT_OUTPUT → COLOR_ATTACHMENT_OUTPUT` with `COLOR_ATTACHMENT_READ`, whereas input-attachment reads occur in `FRAGMENT_SHADER` with `INPUT_ATTACHMENT_READ` and require `VK_DEPENDENCY_BY_REGION_BIT`. `configurePostProcess` (`:275-281`) declares an "input" attachment that no subpass references in any capacity — not as input, not as preserve — while still requiring a framebuffer view for it.

Neither preset is called anywhere in the repository, so these are unused-and-untested rather than actively broken paths. They should either be fixed or removed; as written they will mislead whoever reaches for them first.

**16 — MAJOR — under-specified barriers.**
`VulkanCommandBuilder::imageBarrier` (`:293-325`) derives access masks from a three-case `if` chain for `oldLayout` and a two-case chain for `newLayout`. Any layout outside that set — `COLOR_ATTACHMENT_OPTIMAL`, `GENERAL`, `PRESENT_SRC_KHR`, `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`, `TRANSFER_SRC_OPTIMAL` — leaves the corresponding mask at `0`, producing a barrier that performs the layout transition but no memory dependency. That is a silent correctness hole rather than an error. The same function hardcodes `aspectMask = COLOR_BIT`, `levelCount = 1`, `layerCount = 1` (`:302-306`), so it cannot barrier a depth image, a mip chain, or an array. `VulkanImage::transitionImageLayout` (`src/Vulkan/VulkanImage.cpp:127-176`) and `VulkanTexture::transitionImageLayout` (`src/Vulkan/VulkanTexture.cpp:148-196`) are near-identical copies of each other with the same two-transition limit, and both at least throw on an unhandled pair rather than emitting a wrong barrier — the opposite (and better) choice from `imageBarrier`.

**17 — MAJOR — submit parameter validation.**
`submitCommandBuffers` (`:562-584`) sets `pWaitDstStageMask = waitStages.data()` unconditionally. Both `waitSemaphores` and `waitStages` default to `{}` independently (`include/Vulkan/VulkanCommandBuffer.h:296-298`), so a caller passing wait semaphores but omitting stages yields `waitSemaphoreCount > 0` with `pWaitDstStageMask` pointing at an empty vector's data — `nullptr` in practice. The array must have exactly `waitSemaphoreCount` entries.

**18 — MAJOR — anisotropy limit.**
`samplerInfo.maxAnisotropy = 16.0f` (`src/Vulkan/VulkanTexture.cpp:129`) with `anisotropyEnable = VK_TRUE`. The spec requires `maxAnisotropy <= VkPhysicalDeviceLimits::maxSamplerAnisotropy`. Desktop drivers report 16, so this passes on the development machine, but it is not guaranteed and nothing queries the limit. `VulkanCubemap` sidesteps this by disabling anisotropy entirely (`src/Vulkan/VulkanCubemap.cpp:236-237`).

**19 — MAJOR — staging buffer sizing.**
`VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * m_channels;` (`src/Vulkan/VulkanTexture.cpp:95`) — sized from the caller-declared channel count, while the image is created with the caller-supplied `format` (`:103`), defaulted to `VK_FORMAT_R8G8B8A8_SRGB` (`include/Vulkan/VulkanTexture.h:22`). The two are independent parameters. A caller passing `channels = 3` with the default RGBA format allocates a staging buffer 3/4 of the required size; `copyBufferToImage` (`:198-230`) then instructs the GPU to read `width*height*4` bytes from it. That is an out-of-bounds device read. Whether it is hit depends on the texture-loading layer (outside this review) always passing 4 — I did not verify the callers, so I would call this **uncertain but structurally unsound**: the constructor's contract permits a combination that is guaranteed to overrun. Checking every `VulkanTexture` construction site would confirm or clear it. The HDR constructor computes size from `sizeof(float)` and has the identical exposure (`:53`).

**20 & 21 — MAJOR — recreation without idle.**
`VulkanDescriptorSet::recreate()` (`src/Vulkan/VulkanDescriptorSystem.cpp:330-335`) calls `cleanup()`, which destroys the descriptor pool (`:437-441`) and the layout (`:443-446`). Destroying a pool frees every set allocated from it; if any command buffer still in flight references those sets, that is a use-after-free on the GPU. There is no `vkDeviceWaitIdle`, and callers are given no signal that previously-returned `VkDescriptorSet` handles and any `VkPipelineLayout` built from `m_layout` are now dangling. `DescriptorManager::recreateAll` (`:561-565`) fans this out across every registered set at once. `VulkanComputePipeline::reloadShader()` (`src/Vulkan/VulkanComputePipeline.cpp:72-83`) has the same missing-idle problem for `vkDestroyPipeline`.

**22 — MAJOR — leaks on constructor failure.**
The pattern `m_logger = new Logger(...); m_eventDispatcher = new EventDispatcher();` followed by work that can throw appears in `VulkanInstance` (`:19-24`), `VulkanDevice` (`:23-29`), `VulkanBuffer` (`:22-26`), `VulkanImage` (`:26-30`), `VulkanTexture` (`:22-28`), `VulkanSwapChain` (`:21-25`). If the subsequent Vulkan call throws, the destructor does not run and both allocations leak. `VulkanTexture` additionally leaks its `new VulkanImage` (`src/Vulkan/VulkanTexture.cpp:103`) if `createTextureImageView` or `createTextureSampler` throws. These are startup-path leaks that end in process exit, so the practical impact is small — but `unique_ptr` members would remove the class of problem for free.

**23 & 24 — MINOR — device and queue selection.**
`pickPhysicalDevice` takes the first device satisfying `isDeviceSuitable` (`src/Vulkan/VulkanDevice.cpp:63-68`) with no scoring — on a laptop enumerating the integrated GPU first, the engine silently runs on it. `findQueueFamilies`' first pass (`:259-273`) has no `break`, so `graphicsFamily` and `presentFamily` end up holding the *last* qualifying family rather than the first; on hardware exposing several graphics-capable families this can gratuitously produce `graphics != present` and push the swapchain onto `VK_SHARING_MODE_CONCURRENT` (`src/Vulkan/VulkanSwapChain.cpp:88-91`), which is slower. The compute and transfer passes below it (`:276-309`) do break correctly.

**25 — MINOR — private event dispatchers.**
`VulkanDevice`, `VulkanInstance`, `VulkanBuffer`, `VulkanImage`, `VulkanTexture` and `VulkanSwapChain` each `new` their own `EventDispatcher` and publish to it (e.g. `DeviceCreatedEvent` at `src/Vulkan/VulkanDevice.cpp:155`, `SwapChainRecreatedEvent` at `src/Vulkan/VulkanSwapChain.cpp:229`). Since the dispatcher is private and never handed out, nothing can subscribe — every publish is a no-op. Likewise each class opens its own log file (`vulkan_device.log`, `vulkan_buffer.log`, …), and `VulkanBuffer`/`VulkanImage` do so *per instance*, meaning every buffer and image allocation opens and closes a log file handle. That is a real per-allocation cost, not just clutter.

**26 — MINOR — misplaced namespace brace.**
`src/Vulkan/VulkanCubemap.cpp:298` closes `namespace Shoonyakasha`, and `VulkanCubemap::transitionMipLayout` is then defined at global scope (`:299-324`). This compiles — the global namespace encloses `Shoonyakasha`, and the class name resolves through the `using Shoonyakasha::VulkanCubemap;` at `include/Vulkan/VulkanCubemap.h:102` — but it is plainly unintentional and will break the moment that using-declaration is removed. Moving the brace to the end of the file is the fix.

**27 — MINOR — ignored parameter.** Both `VulkanRenderPass::begin` overloads take `uint32_t subpass` and neither uses it (`src/Vulkan/VulkanRenderPass.cpp:399-417`); a render pass always begins at subpass 0. The parameter suggests a capability that does not exist.

**28 — MINOR — GLFW lifecycle split across classes.** `glfwInit()` is called in the `VulkanInstance` constructor (`src/Vulkan/VulkanInstance.cpp:23`) and `glfwTerminate()` in the `VulkanWindow` destructor (`src/Vulkan/VulkanWindow.cpp:33`). With more than one window, the first window destroyed terminates GLFW for all of them; `glfwInit`'s return value is also unchecked.

**29 — MINOR — shader module input.** Neither `readShaderFile` validates that the file size is non-zero and a multiple of 4 before `reinterpret_cast<const uint32_t*>` (`src/Vulkan/VulkanPipeline.cpp:488`, `src/Vulkan/VulkanComputePipeline.cpp:140`). Alignment happens to be safe because `std::vector<char>`'s allocation is suitably aligned, but a truncated or non-SPIR-V file reaches the driver as garbage.

**30 — MINOR — hash quality.** `Vec3Hash`/`Vec2Hash` and `hash<Vertex>` combine with `h1 ^ (h2 << 1) ^ (h3 << 2)` (`include/Vulkan/VertexTypes.h:24,33,66`). Shifting by 1-3 bits discards very little, so coordinate permutations and mirrored values collide readily. For vertex deduplication during model import this degrades the hash map toward linear scans. `boost::hash_combine`'s mixing constant is the usual remedy.

---

## Design notes

**Ownership model** is inconsistent across the layer, and that is its main structural weakness:

- `unique_ptr` members: `VulkanDevice::m_vmaAllocator`, `VulkanSwapChain::m_swapChainImages`/`m_depthImage`.
- Raw owning pointers with manual `delete`: `VulkanTexture::m_textureImage`, and the `Logger`/`EventDispatcher` in six classes.
- Raw pointers returned from factories that the caller must `delete`: `VulkanCubemap::createEnvironmentMap`/`createIrradianceMap`/`createPrefilterMap` (`include/Vulkan/VulkanCubemap.h:30-32`) — the only factories in the layer that do not return a smart pointer.
- Raw `VkFramebuffer` handles returned with no ownership tracking: `VulkanRenderPass::createFramebuffer` (`src/Vulkan/VulkanRenderPass.cpp:427-459`) — the caller must remember to destroy them, and `VulkanSwapChain` keeps its own parallel framebuffer vector.
- Borrowed references: nearly every class holds `VulkanDevice&`, which makes them non-move-assignable and quietly outlives-dependent.

**Two command pools exist** for the same graphics family: `VulkanDevice::m_commandPool` (`src/Vulkan/VulkanDevice.cpp:158-169`) and `VulkanCommandManager::m_commandPool` (`src/Vulkan/VulkanCommandBuffer.cpp:467-478`). Buffers allocated from one must be freed to the same one; the two `beginSingleTimeCommands`-style paths (`VulkanDevice::beginSingleTimeCommands` and `VulkanCommandManager::executeImmediate`) are duplicate implementations of the same idea, each with its own `vkQueueWaitIdle`.

**`VulkanCommandManager::createSingleTimeBuilder()`** (`:599-602`) begins recording and returns a builder, but there is no matching "end and submit" — a caller using it leaks a command buffer unless they manually call `endRecording` and `freeCommandBuffer`. `executeImmediate` is the complete version of the same operation.

**Descriptor pooling is per-object**: each `VulkanDescriptorSet` creates its own `VkDescriptorPool` sized exactly to its own bindings (`src/Vulkan/VulkanDescriptorSystem.cpp:390-420`). With one descriptor set object per material this becomes one pool per material. There is no shared/growable pool and no free-list, so pool exhaustion is structurally impossible — at the cost of many small pools. `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` is set (`:415`) but no code ever calls `vkFreeDescriptorSets`, so it only costs the driver its faster bump allocator.

The pending-write mechanism deserves a specific note. `addPendingWrite` stores `&m_bufferInfos[set].back()` into `write.pBufferInfo` (`:479`), which is invalidated by the next `push_back`. `updateSet` repairs this before submitting by walking the writes and re-deriving indices (`:298-307`). The repair is correct as written — each write contributes exactly one info of exactly one kind, so the separate `bufferIdx`/`imageIdx` counters stay in step — but it is fragile, and the comment above it ("the bufferInfos and imageInfos vectors are now stable") states something that is not true. Reserving up front, or storing indices instead of pointers, would remove the hazard rather than repair it.

**Builders are value-semantic and cheap**, which is good, but `DescriptorLayoutBuilder::build()` is non-const and mutates `m_config` (`:172-182`), forcing `createFromBuilder` (`:254-261`) and `DescriptorManager::createDescriptorSet` (`:519-527`) to copy the builder first. `RenderPassBuilder::build()` has the same shape (`src/Vulkan/VulkanRenderPass.cpp:283-287`). `PipelineStateBuilder::build()` is properly `const` (`src/Vulkan/VulkanPipeline.cpp:196-198`) — that is the version to standardize on.

**Thread safety**: none, and none is claimed. The single shared command pool, the mutable pending-write vectors in `VulkanDescriptorSet`, and the per-object `Logger` file handles all assume one thread. This matters because `hasDedicatedComputeQueue()` and the separate compute command pool advertise async compute, which is the feature most likely to tempt someone into a second thread.

**Positives, briefly**: `VulkanCubemap` and `VulkanComputePipeline` are the two best-built classes here — correct copy/move control, correct cleanup ordering, and in `VulkanCubemap`'s case genuinely careful subresource handling. VMA integration is clean and the destruction ordering in `~VulkanDevice` (allocator before pools before device, `src/Vulkan/VulkanDevice.cpp:35-49`) is right. Error handling is uniform: everything throws, nothing swallows. `VulkanCommandBuilder`'s `validateRenderPassState`/`validatePipelineState` (`src/Vulkan/VulkanCommandBuffer.cpp:441-451`) catch real ordering mistakes at record time.

---

## Open questions

1. **Is `VulkanTexture` ever constructed with `channels != 4`?** This determines whether finding 19 is a live out-of-bounds device read or only a latent contract hole. The answer is in the texture/model loading layer, outside this review.
2. **Was `m_validationLayers` meant to be `{"VK_LAYER_KHRONOS_validation"}`?** If validation was intentionally disabled, the `enableValidationLayers` flag, the support check, and the debug messenger are all elaborate no-ops that should be removed instead.
3. **Are `VulkanPipeline::recreate`/`reloadShaders` intended to survive?** They have no callers and cannot work as written. If hot-reload is wanted, `VulkanComputePipeline::reloadShader` is the model to copy.
4. **Who owns the `Logger`/`EventDispatcher` passed to `VulkanWindow`?** If the caller owns them, the destructor's `delete` is a double-free waiting for the second run through teardown.
5. **Is `VulkanCommandBuilder::dispatch` used anywhere, or is compute driven exclusively through `VulkanComputePipeline` + the frame graph?** If the latter, `ComputeDispatchCommand` should be deleted rather than fixed — it cannot be made correct without changing its member type.
6. **Are the `configureDeferred` / `configurePostProcess` / `createMultisampled` presets aspirational?** None is called. If the frame graph has superseded them, removing them is better than leaving three incorrect templates in the public header.
7. **What is the intended minimum target?** Vulkan 1.0 instance + 1.2 feature structs + `wideLines` is not a coherent floor. Deciding this resolves findings 5, 6 and 7 together, and unlocks the VMA and dynamic-rendering paths currently left on the table.
