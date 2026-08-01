# Shoonyakasha — Full Code Review

**Date:** 2026-08-01
**Scope:** entire repository at commit `dc9fa3d` (branch `master`)
**Method:** nine independent read-only reviews, one per subsystem, each required to cite
`file:line` for every claim and to mark uncertainty explicitly. No code was modified.

| Report | Subsystem |
|---|---|
| [01-vulkan-rhi.md](01-vulkan-rhi.md) | Vulkan wrapper layer (`include/Vulkan/`, `src/Vulkan/*.cpp`) |
| [02-framegraph-core.md](02-framegraph-core.md) | Frame graph builder / compiler / analyzer / executor |
| [03-databinding-gpu.md](03-databinding-gpu.md) | Dot-path resolution, buffer packing, staging, GPU factory |
| [04-ecs-scene.md](04-ecs-scene.md) | ECS, transforms, animation, physics, cameras, events |
| [05-resources-assets.md](05-resources-assets.md) | glTF loader, ResourceManager, IBL, fonts, sprites |
| [06-facade-app.md](06-facade-app.md) | Public C++ API and `ApplicationBase` lifecycle |
| [07-python-bindings.md](07-python-bindings.md) | Cython bridge and Python packaging |
| [08-shaders-examples.md](08-shaders-examples.md) | GLSL, SPIR-V, example apps, pipeline JSON |
| [09-build-tests-docs.md](09-build-tests-docs.md) | CMake, vcpkg, tests, CI, hygiene, doc accuracy |

---

## 1. Overall assessment

Shoonyakasha is a real engine, not a sketch. ~36k LOC of C++ implements a working declarative
render-graph architecture whose central idea — describing buffer layouts, passes and data
bindings in JSON and resolving them against the ECS at runtime through dot-paths — is
coherently carried through from parser to GPU write. The parts that were designed most
deliberately (dot-path resolution, buffer layout compilation, the ECS component model, the
SceneAPI facade) are also the best tested and hold up under scrutiny.

The weaknesses are concentrated and consistent, and they share one root cause: **the layers
that cannot be unit-tested headlessly were never verified at all.** `src/Vulkan/` is ~14.7k
LOC — roughly 60% of the implementation — and the test suite does not include a single one of
its headers. There is no CI. And validation layers, which would have caught much of what
follows, are never actually enabled: `m_validationLayers` is declared at
`VulkanInstance.h:29` and never populated (`VulkanInstance.cpp:69-70, 120-143`), so the
instance is created with an empty layer list despite all the surrounding code reading as if
validation were on.

Most of the CRITICAL findings below are the kind of defect a validation layer reports on the
first frame. They have survived because nothing was listening.

A second, independent root cause explains a different cluster: **duplication without
propagation.** 142 tracked GLSL files contain 39 distinct byte sequences (72.5% exact
duplicates); nine example `CMakeLists.txt` each carry a private copy of the same shader-compile
function; the same mat3 type-size switch is written three times in `RenderGraph.cpp`. In at
least one confirmed case a bug was correctly fixed in one copy and left in seven others.

---

## 2. What the engine actually does

Verified present and functional unless noted.

### Rendering
- Vulkan 1.0 (`VulkanInstance.cpp:57`), `VK_KHR_swapchain` only, classic `VkRenderPass` +
  `VkFramebuffer`, VMA for allocation.
- Declarative JSON render pipelines compiled at runtime into render passes, framebuffers,
  descriptor set layouts, descriptor sets, and graphics/compute pipelines.
- Topological pass ordering with cycle detection (Kahn's algorithm,
  `FrameGraphCompiler.cpp:217-226`), dead-pass culling from `Present`/imported/side-effect
  roots (`:284-334`), automatic image layout tracking and barrier emission on layout change.
- MRT, read-only depth attachments, per-swapchain-image framebuffer arrays.
- Declarative vertex formats and buffer layouts with std140 offset computation.
- Declarative SSBO initialisation: constant, uniform random, gaussian, 3D grid, sphere
  surface/volume, binary file load, cross-graph `buffer_ref` (`RenderGraph.cpp:299-511`).
- GPU→CPU readback and GPU→disk save for SSBOs, UBOs and render targets, ring-buffered.
- Render-target screenshots to PNG/JPG/BMP/TGA/HDR with half-float decode.
- Static analysis and export: DOT/Graphviz, JSON, markdown reports, culling reasons, barrier
  tables, resource lifetimes, alias-opportunity detection (reporting only).
- Runtime debugger: per-pass CPU timing, execution assertions, event callbacks, 120-frame
  history.
- PBR + IBL (split-sum), bloom, tonemapping, GPU particle simulation via compute, skeletal
  animation with GPU skinning.
- Shaders: SPIR-V binaries only, loaded from a path at compile time. No runtime GLSL
  compilation, no reflection, no shader module cache.

### Data binding
- Dot-path sources (`scene.*`, `entity.*`, `const.*`) resolved against the ECS at render time
  and written into std140 buffers and push constants.
- Scene UBO paths are pre-resolved once at compile time and cached
  (`RenderGraph.cpp:199-229`); per-entity paths are not (see §4).

### ECS
- EnTT registry with **23 component types**, of which **11** are registered for name-based /
  Python access (`Core.h:488-498` registers 9, `CameraController.h:596-599` two more).
- 11 systems including hierarchical transforms, camera, camera controllers (Free / Orbit /
  FirstPerson / ThirdPerson), text baking, UI anchoring, lifetime, physics, and a
  `CallbackSystem` bridge for scripting.
- Entity builder pattern, component registry, JSON scene serialization (Transform, Name, Tag,
  Camera, Light, Hierarchy only).

### Physics
- Bullet3 dynamics world, ECS↔Bullet sync via EnTT signals, box/sphere/capsule/mesh/plane
  colliders, forces and impulses, raycasting, gravity control.

### Assets
- glTF 2.0 via cgltf (**not** tinygltf, contrary to `docs/architecture/overview.md:35,176`):
  meshes, materials, node hierarchies, skins, animations, `.gltf` and `.glb`.
- HDR environment maps via stb, IBL generation on GPU compute (equirect→cubemap, irradiance,
  prefilter) plus a CPU BRDF LUT.
- Font loading and sprite/text rendering.
- Resource cache with handle hashing and a memory budget.

### API
- C++ facade: `EngineAPI` (27 methods), `SceneAPI` (102), `PhysicsAPI` (17), `EcsAPI` (14),
  `InputAPI` (10), plus `ApplicationBase` for subclass-style apps.
- Python bindings via Cython covering most of the above, callback-driven lifecycle.

### Build & test
- Modern CMake 3.21+, vcpkg manifest with a pinned baseline
  (`1f5e0348089e8a9b187f57d42866ebc871e815da`), correct install/export rules with
  `find_dependency()` in the package config, `SHOONYAKASHA_INSTALL` option for wheel builds.
- **522 test macros** (394 `TEST` + 122 `TEST_F` + 6 `TEST_P`) across 22 files; **618** runtime
  ctest cases after parameterized expansion. Deterministic, no disabled tests, only 4 of 522
  assert nothing (all deliberate crash-regression tests).
- 9 C++ examples, all wired into CMake; 2 Python demos.

---

## 3. Limitations

### Architectural constraints (by design or by omission)

- **JSON declaration order is the execution schedule.** `findPreviousWriter`
  (`FrameGraphCompiler.cpp:156-165`) breaks at `w >= currentPass` on *declaration* indices, so
  the topological sort can only ever encode dependencies the author already ordered
  positionally. This is defensible as a design, but it is undocumented and unenforced — see
  CRIT-4.
- **No async compute in practice.** The timeline semaphore is created and referenced only in
  the constructor and destructor; `executeMultiQueue` and `needsMultiQueueSubmit` have no
  callers anywhere in the repo. `SyncPoint` carries no batch indices, so even a correct
  submitter could not use it.
- **No memory aliasing.** `ImageDesc::transient` is parsed and marked "Phase 5"; every image
  gets a dedicated allocation for the whole frame. The analyzer reports alias candidates but
  nothing consumes them.
- **Single-threaded throughout.** No wrapper is thread-safe; `beginSingleTimeCommands` shares
  one command pool with no lock. `ResourceManager`'s 4-thread async path would race on the
  single `VkQueue`/`VkCommandPool` every GPU-touching loader uses.

### Cannot be expressed at all

Ray tracing; multi-view/VR; mesh, task, tessellation and geometry shaders; multiple subpasses
(and therefore input attachments, which parse but are never turned into attachments); MSAA and
resolve attachments; mip chains and array layers inside the graph; 3D images; conditional or
indirect draws/dispatches; bindless / descriptor indexing; dynamic rendering; per-pass
viewport/scissor; depth bias, depth compare op, depth bounds, stencil; per-attachment blend
state under MRT; buffers as real graph resources (no buffer barrier is ever emitted, and
JSON-declared buffers get `usage == 0`); `transfer` pass type (parses, does nothing).

### Missing engine features

No audio, no immediate-mode UI/debug overlay, no multiple windows, no multiple simultaneous
scenes, no hot reload, no programmatic exit (neither C++ nor Python can close a running
engine), no device-loss handling, no 2D texture mipmaps, no compressed texture formats
(BCn/ASTC), no descriptor array writes (`descriptorCount` hardcoded to 1).

### glTF gaps

No Draco or meshopt decoder wired in — and worse, compressed primitives load as
`success = true` with every vertex at the origin (§4, R-9). See
[05-resources-assets.md](05-resources-assets.md) for the full support matrix.

### Platform

Windows/MSVC in practice. Nothing in the source prevents a Linux build and BUILDING.md has a
detailed Linux section, but no CI has ever verified one, and
`python/CMakeLists.txt:12` requests `COMPONENTS Development` rather than `Development.Module`,
which breaks manylinux and is wrong for macOS. macOS is not supported (no MoltenVK mention
anywhere). `docs/getting-started/prerequisites.md:40-42` tells users not to build from a CLI
at all, contradicting BUILDING.md.

---

## 4. Consolidated defect list

Severity-ordered. Two findings were reported independently by two agents and are marked ⊕;
three were additionally verified by hand during synthesis and are marked ✓.

### CRITICAL

| # | Defect | Location |
|---|---|---|
| CRIT-1 ✓ | **IBL generation writes only one cube face.** One `VkDescriptorSet` is allocated, then updated inside a 6-iteration loop with all dispatches recorded into a single command buffer submitted only at the end. Descriptor sets are read at execution, not bind, time — so all six dispatches write through face 5's view. Faces 0–4 are undefined memory, transitioned to `SHADER_READ_ONLY` regardless. Same structure in irradiance (6) and prefilter (60 dispatches). | `IBLGenerator.cpp:296-379`, `:409-465`, `:507-569` |
| CRIT-2 | **Prefilter samples environment mips that were never generated.** `createEnvironmentMap` allocates 11 mips; only mip 0 is ever written; no blit chain exists. `textureLod(environmentMap, L, roughness*4.0)` with `maxLod = m_mipLevels` reads uninitialised memory for every roughness > 0. | `prefilter_convolution.comp:96-97`, `IBLGenerator.cpp:319-365`, `VulkanCubemap.cpp:21-29,241` |
| CRIT-3 ✓ | **Double gamma correction in 8 shader copies.** `pow(mapped, vec3(1.0/2.2))` while the swapchain is `B8G8R8A8_SRGB`, so the hardware encodes again. Already fixed correctly in `particle_flow_example/shaders/tonemap.frag` and never propagated. | 7× `tonemap.frag:71-72`, `bloom_composite.frag:26`, vs `VulkanSwapChain.cpp:300` |
| CRIT-4 | **Dependency edges derive from JSON declaration order.** A graph whose producer is declared after its consumer compiles cleanly, culls nothing, emits no barriers for that resource, and reads undefined memory. Silent. | `FrameGraphCompiler.cpp:156-165,175` |
| CRIT-5 | **The compiler's barrier access masks are discarded by the executor.** Correct `srcAccess`/`dstAccess` are computed and then not passed; `imageBarrier` re-derives them from a 5-case layout heuristic yielding **0** for everything else. `COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL` gets `srcAccessMask = 0` — the previous pass's writes are never made available. | `FrameGraphCompiler.cpp:520-521`, `FrameGraphExecutor.cpp:227-233`, `VulkanCommandBuffer.cpp:308-321` |
| CRIT-6 | **Every barrier uses `VK_IMAGE_ASPECT_COLOR_BIT`, including depth images.** Spec violation; the transition is not performed. Every depth-sampling or shadow path hits it. `levelCount`/`layerCount` are likewise hardcoded to 1 at all five sites. | `VulkanCommandBuffer.cpp:302`; `FrameGraphExecutor.cpp:173,214,390,428` |
| CRIT-7 | **Barriers emitted only on layout *change*.** Compute→compute on a `GENERAL` storage image gets none; all buffer hazards return early. The compute→graphics SSBO case is patched by one blanket `VkMemoryBarrier` scoped `COMPUTE → VERTEX_INPUT\|VERTEX_SHADER`, so compute→fragment is uncovered. WAR and WAW are not modelled at all. | `FrameGraphCompiler.cpp:506-507,513`; `FrameGraphExecutor.cpp:141-158` |
| CRIT-8 ⊕ | **`destroyEntity` on any parent is undefined behaviour.** Range-for over `hierarchy->children` while each recursive call does `erase(remove(...))` on that same vector; *and* the `try_get` pointer to the parent's own `HierarchyComponent` can be relocated by EnTT's swap-and-pop mid-loop. Two independent corruptions. The test-mode branch at `SceneAPI.cpp:95` bypasses this entirely, which is why tests pass. | `ECS/Core.h:428-446`, `:54-56` |
| CRIT-9 | **`writeField` writes `sizeof(resolved type)`, not the declared field size,** with no bounds check against `layout.totalSize`. `{"type":"float","source":"const.0.5"}` resolves to vec2 → 8 bytes into a 4-byte slot. On the last field of a push-constant layout this overruns the `pushData` vector — heap corruption. | `DotPathResolver.cpp:570-579`, `.h:112-119`; `RenderGraph.cpp:2042` |
| CRIT-10 | **Every `mat3` uniform is silently garbage.** Both compilers reserve 48 bytes; the resolver writes 36 contiguously, so columns land at offsets 12/24 instead of 16/32. The trailing padding keeps the *next* field correct, which is why it hides. `BufferLayoutCompilerTest.cpp:237` locks in the wrong behaviour. | `DotPathResolver.cpp:578` vs `FrameGraphCompiler.cpp:1583`, `BufferLayoutCompiler.h:173-175` |
| CRIT-11 | **`VulkanPipeline::recreate()` destroys the layout and shader modules and recreates neither; `reloadShaders()` double-destroys the modules and loses the layout.** Any hot-reload or resize path through these is use-after-free. | `VulkanPipeline.cpp:291-306,467-482` |
| CRIT-12 | **Compute dispatch binds a graphics pipeline at `VK_PIPELINE_BIND_POINT_GRAPHICS`, then dispatches.** | `VulkanCommandBuffer.cpp:267-280`; `VulkanDescriptorSystem.cpp:326` |
| CRIT-13 | **16-bit index selection tests index *count* instead of max index *value*.** A 100k-vertex / 60k-index primitive takes the 16-bit path; every index ≥ 65536 is truncated. Scrambled triangles, no diagnostic. | `GltfSceneLoader.cpp:497,508` |
| CRIT-14 | **Unbounded recursion with no cycle detection in the transform graph.** `setParent` performs no ancestry check and `deserialize` restores links straight from JSON. Stack overflow, trivially reachable from a script. | `Systems.h:67-88`; `Core.h:437-442`; `SceneAPI.cpp:698-720`; `Scene.h:391-415` |
| CRIT-15 | **Bone SSBOs leak on every skinned-entity destroy** (no `on_destroy<SkeletonComponent>` hook exists) **and are written with no frames-in-flight protection** — one host-visible buffer memcpy'd every frame while prior frames may still be reading. | `SkeletonComponents.h:42`; `SkeletalAnimationSystem.cpp:142-178` |
| CRIT-16 | **Sub-API getters dereference null before `run()`.** `sceneAPI`/`inputAPI`/`physicsAPI`/`ecsAPI` are null until `onInit` runs inside `run()`. `docs/api/cpp/engine-api.md:78,212` explicitly says they are valid immediately after construction. | `EngineAPI.cpp:47-50,80-93,287-301` |
| CRIT-17 | **The documented way to configure the render graph crashes.** `getRenderGraph()` returns `*m_renderGraph`, constructed *after* `onInit()`. `docs/api/cpp/application-base.md:276` recommends exactly this. | `ApplicationBase.cpp:73,187,480`; `EngineAPI.cpp:376,380,384,388,392` |
| CRIT-18 | **Adding or removing a system from inside a system callback is UB;** `removeSystem("self")` destroys the executing `std::function`. `EcsAPI` is built for scripting, where this is the obvious idiom. | `Systems.h:275-281,261-268`; `EcsAPI.cpp:124,130` |
| CRIT-19 ✓ | **`sk.UI_ANCHOR_*` and `sk.TEXT_ALIGN_*` are not re-exported,** so two shipped Python demos raise `AttributeError` inside `on_init` — which `PyErr_Print()` swallows, leaving the engine running with a half-built scene. `Ecs` is also missing. | `python/shoonyakasha/__init__.py:13-44` vs `_shoonyakasha.pyx:79-92` |
| CRIT-20 | **Python sub-API wrappers hold a raw pointer with no reference to the owning `Engine`.** `sk.Engine(...).scene` is an immediate use-after-free. `sk.Scene()` is also directly constructible and null-derefs. | `_shoonyakasha.pyx:188-193,604-609,670-675,786-791,970-972` |
| CRIT-21 | **One stale `.spv`, and it is the one that matters.** `python/examples/shaders/particle_sim.comp.spv` was built from a single-attractor version; current GLSL has four attractors plus `groundY`. `python/examples/` has no CMake shader step, so the stale binary executes and every UBO field from offset 4 on is misinterpreted. (137 of 138 committed `.spv` are otherwise byte-identical to a fresh compile.) | `python/examples/shaders/particle_sim.comp[.spv]` |

### MAJOR — selected

Full lists are in the per-subsystem reports; these are the ones with the broadest blast radius.

- **Validation layers are never enabled.** `m_validationLayers` is never populated.
  (`VulkanInstance.h:29`, `VulkanInstance.cpp:69-70,120-143`)
- **`"execution": {"type": "compute_image"}` always dispatches zero workgroups** — group counts
  come from an extent that is only assigned for graphics passes.
  (`FrameGraphExecutor.cpp:639-643`, `FrameGraphCompiler.cpp:576`)
- **`sprite_geometry` passes never draw** — the pass type is installed but absent from the
  executor's dispatch chain; it falls through with no warning. Sprite *and* text rendering
  produce nothing. (`RenderGraph.cpp:1658` vs `FrameGraphExecutor.cpp:675-680`)
- **`std430` packing applies no alignment whatsoever** — offsets are `sizeof` sums, so `vec3`
  gets 12 and `mat3` 36. The docs recommend `std430` for SSBOs.
  (`FrameGraphCompiler.cpp:1629,1656-1664`)
- **Command buffers are indexed per swapchain image but fenced per frame-in-flight**
  (3 images, 2 fences, no `m_imagesInFlight`), and present semaphores are indexed by frame
  rather than by image. (`ApplicationBase.cpp:125,352,382-392,404`)
- **Minimize is entirely unhandled** — no `glfwWaitEvents` wait-while-minimized loop exists
  anywhere; `recompile` throws out of an event handler inside `pollEvents()`.
  (`ApplicationBase.cpp:418-446`; `VulkanWindow.cpp:130-136`)
- **Per-entity descriptor set cache survives recompile with destroyed layouts** — use-after-free
  on every window resize; the pool is never reset, so entries accumulate to a 4096 cap after
  which materials silently unbind. (`FrameGraph.h:1252`; `RenderGraph.cpp:2192,2205`)
- **Samplers leak on every recompile** — one per declared sampler per resize.
  (`RenderGraph.cpp:1429,1462-1501`)
- **Queue ownership transfers are emitted twice on the acquiring queue and never released on
  the source.** (`FrameGraphCompiler.cpp:526-536`; `FrameGraphExecutor.cpp:161-236`)
- **`ResourceManager::retrieve()` increments `referenceCount` and nothing ever decrements it** —
  nothing is evictable, the memory budget silently does not hold, and
  `ResourceCacheTest.cpp:229-246` asserts this as correct. (`ResourceManager.cpp:101,130,201`)
- **Unknown dot-paths fail completely silently** — zeros, no log line at any severity, no
  counter. `validatePath` has no production caller and its whitelist omits `lights` and
  `custom`. (`DotPathResolver.cpp:296,408,471-575`)
- **`ECS::InputSystem` never delivers input** — `syncToComponent()` is called from nowhere. The
  engine works around it with `StandaloneInputHandler`, leaving broken dead code in the public
  headers. (`InputSystem.h:68-87,153-163`; `ApplicationBase.cpp:318-332`)
- **C++ exceptions cross the Python boundary unguarded** — only 4 of ~134 declarations carry
  `except +`, while `src/` contains 123 `throw` statements including every texture and font
  load path. (`_engine_api.pxd`)
- **There is no way to stop a running engine from Python** — Ctrl+C is caught, printed and
  discarded every frame; `sys.exit()` calls `Py_Exit()` from inside the render loop with no
  Vulkan teardown. (`_callback_bridge.h:50,66,82,98,115,131`)
- **Rotation channels use nlerp with no shortest-path correction,** and glTF requires slerp for
  `LINEAR` rotation channels. (`AnimationEvaluator.cpp:64,191-199`)
- **`saveToFile` returns `true` when the file was never written** — no `is_open()` or stream
  state check. Silent data loss on the only persistence API. (`Scene.h:424-433`)
- **`setActive` is a semantic no-op** — no system reads `ActiveComponent::active`.
  (`SceneAPI.cpp:229-239`; `Core.h:471-474`)
- **Draco/meshopt geometry loads as `success = true` with every vertex at the origin** — no
  decoder is wired in and cgltf does not reject unsupported `extensionsRequired`.
  (`GltfSceneLoader.cpp:99`)
- **Equirect→cubemap mapping is vertically flipped** (LearnOpenGL formula without the
  compensating `stbi_set_flip_vertically_on_load`, which appears nowhere in the repo), and the
  **prefilter roughness↔mip mapping disagrees between generator (m/9 over 10 mips) and consumer
  (`roughness*4.0`)**, so rough surfaces render far too glossy.
  (`equirect_to_cubemap.comp:46-47`; `IBLGenerator.cpp:524` vs `pbr_ibl_lighting.frag:136-138`)
- **The CPU BRDF LUT uses `k = roughness⁴/2` while the unused `brdf_lut.comp` beside it uses the
  correct `k = roughness²/2`,** and it runs single-threaded on the main thread (~2.7×10⁸
  iterations) at startup while the working compute shader sits unreferenced.
  (`IBLGenerator.cpp:590-672`; `brdf_lut.comp:50-53`)
- **RAII wrappers are implicitly copyable** across six classes — one accidental copy is a
  double-destroy.
- **No warning flags on the engine library at all.** `/W4` and `-Wall -Wextra` appear on every
  example and on the test target, and nowhere on `Shoonyakasha` itself. The 14.7k-LOC Vulkan
  subsystem compiles at MSVC default `/W1`. (`CMakeLists.txt`)

---

## 5. Test suite and CI

**522 test macros / 618 ctest cases.** Not 582 — that figure appears in `README.md:169`,
`docs/faq.md:27,149`, `docs/architecture/overview.md:183` and
`docs/getting-started/prerequisites.md:34,73`, and the quoted "518 core + 64 facade" split is
also wrong (actual: 436 + 86).

The suite is genuinely good at what it covers and honest about its tier
(`SceneAPITest.cpp:4`: *"Tier 2: Uses entt::registry + ComponentRegistry directly (no GPU)"*).
Assertion discipline is strong, nothing is disabled, and only 4 of 522 tests assert nothing.

But 22 test files include only 13 distinct engine headers, and everything else is untested by
construction:

| Subsystem | Coverage |
|---|---|
| Dot-path binding | 92 tests — best-covered, appropriately so |
| ECS core | 109 tests — solid |
| Buffer layout packing | 45 tests |
| Facade (`SceneAPI`/`EcsAPI`/`FacadeTypes`) | 86 tests |
| Animation (CPU-side) | 39 tests |
| FrameGraph | **only the JSON string↔enum conversion tables.** No compile, no barrier analysis, no execution, no scheduling |
| Vulkan RHI (~14.7k LOC) | **zero** |
| Physics | **zero** |
| IBL | **zero** |
| App lifecycle | **zero** |
| glTF loading | **effectively zero** — no glTF file is ever parsed |
| Python bindings | **zero** — no pytest config anywhere |

**No GPU or integration tests exist.** `TestHelpers.h:49-63` returns
`reinterpret_cast<VkBuffer>(uintptr_t(1))` with the comment "never dereferenced". No
`VkInstance` is ever created. There is no smoke test that opens a window.

**There is no CI.** `.github/` does not exist.

---

## 6. Documentation accuracy

Verified claim-by-claim in [09-build-tests-docs.md](09-build-tests-docs.md). Summary:

- **The README build commands do not work.** `cmake .. -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON`
  omits `-DCMAKE_TOOLCHAIN_FILE=…vcpkg.cmake`, so all six `find_package(... CONFIG REQUIRED)`
  calls fail at configure; `BUILD_TESTS=ON` additionally needs
  `-DVCPKG_MANIFEST_FEATURES=tests`. BUILDING.md:182-190 has the correct invocation.
  (`README.md:145-156`)
- "CMake 3.12+" — actually 3.21 (`CMakeLists.txt:6`). "8 C++ example applications" — there are
  9, and the README's own table two sections later lists 9. "582 tests" — false. "vcpkg or
  manually installed" — manual installation is not a supported path.
- **`docs/getting-started/prerequisites.md` is the most inaccurate file in the repository** and
  is the linked entry point for new users: wrong C++ standard (C++17), wrong CMake version,
  wrong Python version, never mentions vcpkg, credits **tinygltf** (the project uses cgltf),
  hardcodes `H:\cpp_dev\Shoonyakasha` (a path that no longer exists), and tells users a test
  failure means their GPU drivers are out of date — for a suite that never creates a
  `VkInstance`.
- `docs/architecture/overview.md:35,176` also credits tinygltf.
- `docs/old/` (2 files) and `docs/plans/` (3 files) are orphaned — nothing in 47 markdown files
  links to them.
- ~25 concrete discrepancies between `docs/api/cpp/*.md` and the real signatures, including two
  that document crashing usage as recommended practice (CRIT-16, CRIT-17).

---

## 7. Repository hygiene

Total tracked content: **216.9 MiB across 574 files. ~96% of it is binary assets and build
artifacts;** source is ~3.4 MiB.

| File | Size | Note |
|---|---|---|
| `examples/physics_test/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr` | 94.39 MiB | |
| `examples/pbr_physics_particles/cubemaps_hdrs/kloofendal_…_8k.hdr` | 94.39 MiB | byte-identical duplicate |
| `python/examples/img.png` | 18.00 MiB | |
| `python/shoonyakasha/_shoonyakasha.pyd` | 2.32 MiB | matches `.gitignore:60`, committed before the rule |
| `logo.png` | 1.82 MiB | displayed at 300 px wide |
| 138 × `.spv` | 0.84 MiB | build output; `*.spv` is not in `.gitignore` despite BUILDING.md:174 saying to add it |
| `__pycache__/__init__.cpython-313.pyc` | 727 B | matches `.gitignore:52-53`, committed before the rule |

`.gitignore` is better than the on-disk clutter suggests — `cmake-build-*`, `.idea/`, `.venv/`,
`glTF-Sample-Assets/`, `pkg_a_curtains/` are all correctly ignored and untracked. The real gaps
are `*.spv` and two `cubemaps_hdrs` directories that the per-example rules missed
(`physics_test` and `pbr_physics_particles` — precisely the two holding the 94 MiB files). A
single `cubemaps_hdrs/` pattern with no leading slash covers all seven.

Removing these from the working tree does not shrink history; a fresh clone still pays. Fixing
that needs `git filter-repo`/BFG and a force-push — a separate decision.

`pkg_a_curtains/` is 1.5 GB on disk but tracked at zero files (`.gitignore:5`) and referenced by
nothing in the codebase. Local scratch.

---

## 8. Suggested order of work

Ordered by (impact × confidence) ÷ effort, not by severity alone.

**First — restore the feedback loop.** Nothing else compounds the way this does.

1. Populate `m_validationLayers` with `VK_LAYER_KHRONOS_validation`
   (`VulkanInstance.cpp:120-143`). One line. It will independently confirm or refute CRIT-5,
   CRIT-6, CRIT-7, CRIT-12, and a good share of the RHI findings — and it will keep confirming
   them for free from now on.
2. Add `/W4` / `-Wall -Wextra` to the `Shoonyakasha` target. The largest and most error-prone
   subsystem currently compiles at `/W1`.
3. Add a minimal CI: configure + build + `ctest` on Windows. Even one platform ends the
   "has anyone ever built this from scratch" question.

**Second — the cheap, high-visibility correctness fixes.**

4. CRIT-3 (double gamma) — delete 8 lines, propagate the fix you already wrote.
5. CRIT-19 (Python re-exports) — one import list. Two shipped demos start working.
6. CRIT-1 (IBL descriptor set per dispatch) — the pool already has capacity and
   `FREE_DESCRIPTOR_SET_BIT`.
7. CRIT-13 (16-bit index predicate) — one expression.
8. CRIT-21 (stale `.spv`) — recompile, and add a shader step for `python/examples/`.

**Third — the silent-corruption cluster.** These need care but are well-localised.

9. CRIT-9 / CRIT-10 (field size and mat3 packing) — and fix
   `BufferLayoutCompilerTest.cpp:237`, which currently locks in the wrong behaviour.
10. CRIT-5 / CRIT-6 (pass the access masks through; derive the aspect mask from the format).
11. CRIT-8 (`destroyEntity` — collect children into a local vector, then destroy) and CRIT-14
    (ancestry check in `setParent`, depth cap in the transform walk).
12. CRIT-2 + the IBL mip/roughness disagreement — decide on 5 mips or `MAX_REFLECTION_LOD = 9.0`
    and make generator and consumer agree.

**Fourth — the API contract holes.** These are what a new user hits first.

13. CRIT-16 / CRIT-17 — either construct the sub-APIs eagerly, or make the getters throw a clear
    error before `onInit`. Then fix the docs that recommend the crashing usage.
14. Add a programmatic exit (`EngineAPI::quit()`), and handle `KeyboardInterrupt`/`SystemExit`
    in the Python bridges instead of printing them.
15. Add `except +` across `_engine_api.pxd`. It costs nothing when nothing throws.
16. Make unknown dot-paths warn once at compile time. For an engine whose premise is "the JSON
    is the pipeline", silence here is the worst possible default.

**Fifth — structural, when there's appetite.**

17. Deduplicate the shaders (142 files → 39) and hoist `target_compile_shaders()` into one
    shared CMake module. This is the root cause of CRIT-3 and will cause the next one too.
18. Compile shaders into the *build* tree, add `*.spv` and `cubemaps_hdrs/` to `.gitignore`.
19. Document that JSON declaration order is the schedule (CRIT-4) — or make the compiler detect
    and reject a consumer declared before its producer.
20. Rewrite `docs/getting-started/prerequisites.md` from scratch; delete `docs/old/` and
    `docs/plans/` or move them out of the published tree.
21. Decide the fate of the dead subsystems: `ECS::InputSystem`, `EntityRenderExecutor`, the
    multi-queue path, the CPU→GPU half of `StagingBufferManager`,
    `GPUResourceFactory::createVertexBuffer/createIndexBuffer`. Each is either a missing feature
    or a trap; leaving them in the public headers is the one option that helps nobody.

---

## 9. Confidence and caveats

- Nothing was built or run. Every finding is derived from reading the source, except the shader
  review, which recompiled all 142 GLSL files with `glslc` 1.4.321.1 and byte-compared against
  the committed binaries.
- The 522 test-macro count is exact (two independent counting methods agreed); the 618 ctest
  figure is derived statically from the parameterized instantiations and was not confirmed
  against real `ctest` output.
- CRIT-1, CRIT-3 and CRIT-19 were re-verified by hand during synthesis.
- CRIT-8 was found independently by two reviewers with different scopes.
- Where a reviewer was unsure, the per-subsystem report says so explicitly. Findings marked
  "latent" have no live caller today and would only bite if the code is wired up.

---

## 10. Findings discovered during remediation (2026-08-01)

Recorded as they surfaced while working through the fixes. None are regressions
from that work; each was verified against the pre-change build where relevant.

### `facade_test` cannot run from its own directory

`examples/facade_test/` contains only `main.cpp` and `CMakeLists.txt`. The three
assets its `main.cpp` names — `pipeline.json`, `cubemaps_hdrs/…8k.hdr`, and
`Sponza/glTF/Sponza.gltf` — are all absent from the repository, so `run()`
throws `Cannot open frame graph file: 'pipeline.json'`. Run with a working
directory that has those assets, it initialises and renders normally.

The README lists it as the example that demonstrates the Facade API, so this is
the first example a new user is likely to try.

### Teardown after a failed `run()` segfaults

**Reproduced identically with and without the remediation work** (verified by
stashing it and rebuilding), so it is pre-existing.

When `run()` throws during initialisation, `onCleanup()` runs and logs, and the
process then segfaults during destruction. The normal exit path has not been
observed, because every example run so far was ended by a timeout rather than by
closing the window — so it is not yet known whether this affects clean shutdown
too. Worth establishing before anything else here: run one example and close its
window.

### Validation errors remaining in `bloom_test`

See `validation-bloom-test.md`. After the barrier work, four examples are
validation-clean and `bloom_test` is not. Its 50 messages are the two items
deliberately deferred out of that phase:

- queue ownership transfer emitted twice on the acquiring queue, never released
  on the source
- barriers emitted only on layout *change*, so same-layout hazards get none and
  the descriptor-declared layout drifts from the actual one

Plus one not previously reported: a descriptor `params` used in a dispatch that
was never updated.

### The swapchain `present` usage was content, not engine

`vkQueuePresentKHR` complained every frame that the swapchain image was in
`COLOR_ATTACHMENT_OPTIMAL`. `ResourceUsage::Present` already existed and was
handled correctly throughout the compiler; the shipped pipelines just declared
their final pass as `color_write`. Corrected in 14 of 17 pipelines.

`full_showcase/showcase_pipeline.json` is the exception: three passes write the
swapchain and two are `color_blend`. **A pass that both blends and presents
cannot be expressed** — `ColorAttachmentBlend` carries its own initialLayout
handling and previous-writer chain that `Present` does not. This is a genuine
schema gap.

### The per-draw path ignored the compiler's offsets

Worse than the mat3 defect originally reported, and previously unnoticed:
`bindEntityData` and `fillBuffer` recomputed field offsets with a tight-packing
accumulator, discarding the compiler's std140/std430 offsets entirely. They
agreed only because `MaterialPushConstants` happens to contain no `vec3`.

### `mat3` and `std430` were latent, not live

No shipped `bufferLayouts` entry uses `vec3`, `mat3`, `mat2`, `bool`, `double`,
`ivecN` or `uvecN` — every field is `mat4`, `vec4`, `vec2`, `float`, `uint`, or
`vec4[16]`. Correct std140 and correct std430 produce byte-identical layouts to
the pre-fix output for all of them, which is what made the packing refactor
safe to verify.
