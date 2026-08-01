# Review 05 — Asset loading, resource management, IBL, third-party

## Scope

Files read in full:

- `include/Resources/GltfSceneLoader.h`, `src/Resources/GltfSceneLoader.cpp` (1124 lines)
- `include/Resources/ResourceManager.h`, `src/Resources/ResourceManager.cpp`
- `include/Resources/FontLoader.h`, `src/Resources/FontLoader.cpp`
- `include/Resources/Sprite2DManager.h`, `src/Resources/Sprite2DManager.cpp`
- `include/Resources/AnimationData.h`
- `include/IBL/IBLGenerator.h`, `src/IBL/IBLGenerator.cpp`
- `examples/declarative_sponza_test/shaders/ibl/{brdf_lut,equirect_to_cubemap,irradiance_convolution,prefilter_convolution}.comp`
- `src/ThirdParty/cgltf_impl.cpp`, `src/ThirdParty/stb_impl.cpp`, `CMakeLists.txt` (third-party section), `vcpkg.json`
- `tests/unit/ResourceCacheTest.cpp`, `tests/ecs/AnimationDataTest.cpp`

Read as necessary supporting context (the actual GPU upload path and cubemap object used by the code above):
`src/GPU/GPUResourceFactory.cpp`, `src/Vulkan/VulkanCubemap.cpp`, `src/Vulkan/VulkanTexture.cpp` (ctors),
`src/App/ApplicationBase.cpp:159-180`, `examples/declarative_sponza_test/shaders/pbr_ibl_lighting.frag`,
`third_party/cgltf/cgltf.h` (`cgltf_validate`, `cgltf_accessor_read_*`).

Third-party versions: cgltf **1.15**, stb_image **2.30**, stb_truetype **1.26**, VMA **3.4.0-development**,
tinyobjloader (vendored, see Finding R-13). Vendored under `third_party/`, added to the include path at
`CMakeLists.txt:65-70`; single-TU implementations in `src/ThirdParty/`. vcpkg pins a builtin-baseline
(`vcpkg.json`) and provides vulkan/nlohmann-json/entt/bullet3/glfw3/glm — none of the asset libraries.

---

## glTF support matrix

`+` = works, `~` = partial / degraded, `-` = silently ignored, `x` = actively wrong.

| Feature | Status | file:line | Notes |
|---|---|---|---|
| `.gltf` + `.glb` | + | `GltfSceneLoader.cpp:102` | `cgltf_parse_file` handles both by magic |
| External `.bin` buffers | + | `:109` | `cgltf_load_buffers` |
| Base64 `data:` **buffer** URIs | + | `:109` | decoded by cgltf |
| Base64 `data:` **image** URIs | x | `:569,611` | cgltf does not decode image data URIs; `image->uri` is the whole `data:...` string, passed to `stbi_load` as a filename → fails, texture dropped |
| Percent-encoded image URIs (`%20`) | x | `:297-305` | raw URI concatenated to base path; no `cgltf_decode_uri`. Buffer URIs *are* decoded by cgltf, so `.bin` works and the texture next to it does not |
| Embedded images (GLB buffer view) | + | `:571-609` | decoded via `stbi_load_from_memory` |
| Structural validation | + | `:117` | `cgltf_validate` runs and its result is checked — this covers accessor/bufferView bounds, index-value bound vs. attribute count, node-parent cycles, primitive attribute count agreement |
| **Primitive modes** | ~ | `:241-243` | Only `TRIANGLES`. Points, lines, line loop/strip, triangle strip/fan are `continue`d with **no diagnostic** |
| POSITION | + | `:399,424` | |
| NORMAL | + | `:400,447-453` | fallback `(0,1,0)`; transformed by the inverse-transpose (`:414`) |
| TANGENT | - | — | never read; `StandardVertex` (`:34-39`) has no tangent slot at all, yet `normalMap` is bound (`:694`). Not generated either |
| TEXCOORD_0 | + | `:401` | |
| TEXCOORD_1+ | - | `:401`, `:936` | `if (attr.index == 0)`. `textureView.texcoord` is never consulted, so a material declaring `"texCoord": 1` silently samples UV0 |
| COLOR_0 | ~ | `:402,429-435` | rgb only, alpha dropped; not read at all on the skinned path (`SkinnedVertex` has no color) |
| COLOR_1+ | - | `:402` | |
| JOINTS_0 / WEIGHTS_0 | + | `:937-938,981-1002` | weights renormalised (`:994-999`) |
| JOINTS_1 / WEIGHTS_1 (>4 influences) | - | `:937-938` | index 1 dropped → wrong deformation on 8-influence rigs |
| Non-indexed geometry | + | `:488-493` | empty index buffer, `hasIndices()` false |
| Index component types u8/u16/u32 | + | `:508,531` | via `cgltf_accessor_read_index` |
| Index width choice | x | `:497` | `use16Bit = (indexCount <= 65535)` — the wrong test. See Finding R-1 |
| All accessor component types (i8/u8/i16/u16/u32/f32) + `normalized` | + | cgltf `cgltf_element_read_float` | conversion and normalisation handled by cgltf |
| **Sparse accessors** | + | cgltf `cgltf_accessor_read_float`/`_uint`/`_index` | all three consult `cgltf_find_sparse_index` in 1.15. (The doc comment at the top of `cgltf.h` claiming sparse is unsupported is stale.) Not covered by any test |
| Node TRS vs. 4×4 `matrix` | + | `:225` | `cgltf_node_transform_world` handles both |
| Node hierarchy | + | `:225,278-280` | world transform recomputed from the node's own ancestor chain; the `parentTransform` parameter is dead |
| Multiple scenes | ~ | `:153-168` | default scene, else `scenes[0]`, else all parentless nodes. Non-default scenes unreachable |
| Y-up / units | + (implicit) | — | no conversion; engine is assumed Y-up/metres |
| Instanced nodes (same mesh, N nodes) | ~ | `:237-274` | geometry is **re-uploaded per node** — no mesh-level dedup, only textures are cached |
| `EXT_mesh_gpu_instancing` | - | — | |
| Materials: `pbrMetallicRoughness` factors | + | `:335-344` | |
| baseColor texture (sRGB) | + | `:349` | |
| metallicRoughness texture (linear) | + | `:352` | |
| normal texture | + | `:358` | `normal_texture.scale` ignored |
| occlusion texture | + | `:362` | `occlusion_texture.scale` (strength) ignored; not packed with MR |
| emissive texture + factor | + | `:366,369` | `KHR_materials_emissive_strength` ignored |
| alphaMode / alphaCutoff / doubleSided | + | `:283-295,375-377` | |
| Texture **samplers** (wrapS/wrapT, min/mag filter) | x | `:646-653` | `texture->sampler` never read. Everything gets `REPEAT` + `LINEAR` + aniso 16. Assets authored with `CLAMP_TO_EDGE` show edge bleeding |
| `KHR_texture_transform` | - | — | `textureView.has_transform` never consulted → wrong UVs |
| `KHR_materials_pbrSpecularGlossiness` | - | `:335` | only `has_pbr_metallic_roughness`; a spec-gloss-only material renders with the struct defaults (white, metallic 0, roughness 0.5) |
| `KHR_materials_unlit` / `_transmission` / `_clearcoat` / `_sheen` / `_ior` / `_specular` / `_volume` / `_anisotropy` / `_iridescence` | - | — | parsed by cgltf, never read here |
| `KHR_materials_variants` | - | — | |
| `KHR_draco_mesh_compression` | x | — | cgltf parses the extension but never decompresses, and does not reject unsupported `extensionsRequired`. Draco attribute accessors have no `bufferView`, so `cgltf_accessor_read_float` memsets 0 and returns success → the model loads "successfully" as a degenerate blob at the origin |
| `EXT_meshopt_compression` | x | — | same failure mode; no meshopt decoder wired into `cgltf_options` |
| `KHR_texture_basisu` / KTX2 | x | `:598,611` | stb cannot decode KTX2/Basis → texture dropped with a `cerr` line |
| **Morph targets** | - | — | `primitive.targets` / mesh weights never touched |
| Cameras | - | — | `data->cameras` never read |
| `KHR_lights_punctual` | - | — | `data->lights` never read |
| Skins | ~ | `:736-816` | one skeleton per skin, IBMs, parent indices, default TRS. Only `skeletons[0]` is ever bound to an entity (`:181`, acknowledged TODO); `skin->skeleton` root node ignored |
| Animation channels T/R/S | + | `:852-864` | |
| Animation channel `weights` | - | `:862-863` | `continue`d (morph targets unsupported anyway) |
| Interpolation STEP / LINEAR / CUBICSPLINE | + (parsed) | `:867-877` | CUBICSPLINE stores the 3× in/value/out layout as documented in `AnimationData.h:105-106`; whether the playback system honours it is out of this scope |
| Animation → joint binding | x | `:843-846` | matched by **node name string**. glTF node names are optional and not unique. An unnamed target node, or a rig where the joint node name differs from the skeleton entry, silently drops the channel |

**Headline.** The static-mesh core of glTF 2.0 is covered honestly: both containers, all accessor component
types, sparse accessors, TRS/matrix nodes, the base metallic-roughness material with all five texture slots,
alpha modes, and `cgltf_validate` is actually run and checked. Everything outside that core — a second UV set,
tangents, texture samplers, `KHR_texture_transform`, every `KHR_materials_*` extension, morph targets, cameras,
punctual lights, non-triangle primitives, Draco/meshopt — is dropped without a warning. Skinning works for the
single-skin, ≤4-influence, uniquely-named-joint case.

---

## IBL pipeline

`IBLGenerator::generate` (`IBLGenerator.cpp:229-266`) is called **once** at startup from
`ApplicationBase::loadIBLTextures` (`src/App/ApplicationBase.cpp:169-170`), inside a try/catch. Nothing
regenerates per frame. Five steps:

1. **HDR load** — `stbi_loadf(..., STBI_rgb_alpha)` → `VulkanTexture` as `VK_FORMAT_R32G32B32A32_SFLOAT`
   (`:210-219`). No `stbi_set_flip_vertically_on_load`.
2. **Equirect → cubemap** — `equirect_to_cubemap.comp`, 16×16 local size, one dispatch per face into a
   per-face 2D storage view (`:319-365`). Output `VulkanCubemap::createEnvironmentMap` = size 1024,
   `R16G16B16A16_SFLOAT`, `calculateMipLevels(1024)` = **11 mips** (`VulkanCubemap.cpp:21-29`).
   The cube-face direction basis (`equirect_to_cubemap.comp:27-34`) matches the Vulkan/GL cubemap
   convention exactly for all six faces — that part is correct.
3. **Irradiance** — `irradiance_convolution.comp`, 8×8, 32² faces, 2048 cosine-weighted Hammersley samples
   (`:422-465`). Stores `mean(L)`, and the consumer does `diffuse = irradiance * albedo`
   (`pbr_ibl_lighting.frag:116,129`) — that is the correct Lambertian normalisation, no missing π.
4. **Prefilter** — `prefilter_convolution.comp`, 8×8, 512² base, `calculateMipLevels(512)` = **10 mips**,
   `roughness = mip / (mipLevels - 1)` (`:520-524`), 1024 GGX samples, `R = V = N` split-sum assumption.
5. **BRDF LUT** — **the compute shader `brdf_lut.comp` is never used.** `generateBRDFLUT` (`:590-672`) is a
   single-threaded CPU loop, 512 × 512 × 1024 samples, uploaded as `R32G32B32A32_SFLOAT`.

Constants are all reachable via `IBLGenerationParams` (`IBLGenerator.h:20-28`) — env 1024, irradiance 32,
prefilter 512, LUT 512, samples 2048/1024/1024 — except the two hardcoded ones noted in Findings R-6 and R-7.

Cost, roughly: prefilter is ~2.2 × 10⁹ cubemap fetches across all mips in one submit (tens of ms on a desktop
GPU, fine); irradiance ~12.6 M (negligible); the CPU BRDF LUT is ~2.7 × 10⁸ inner iterations with
`sqrt`/`sin`/`cos`/`pow` each, single-threaded — seconds of startup stall (Finding R-6).

Correctness problems in this pipeline are Findings R-2 through R-8 below.

---

## Features

- **glTF**: `cgltf_validate` is called and its result checked (`GltfSceneLoader.cpp:117`) — the single most
  important robustness decision in the loader, and it is present.
- **Texture dedup within a load**: keyed by resolved path (or buffer-view pointer) plus an `_srgb`/`_linear`
  suffix, so the same image used as both albedo and MR gets two correctly-formatted uploads and no more
  (`:584-588`).
- **Mip generation** on glTF textures via `vkCmdBlitImage` chain with correct per-level barriers
  (`GPUResourceFactory.cpp:564-649`).
- **Skinning**: IBMs, parent indices, default TRS, weight renormalisation, per-skin skeleton cache
  (`m_skinCache`, `:132-139`).
- **ResourceCache**: genuinely thread-safe (one mutex, disciplined `*Unlocked` internals,
  `ResourceManager.cpp:175-217`), LRU-ordered eviction candidates, memory accounting. Decently unit-tested
  (`ResourceCacheTest.cpp`, 25 tests) — though the tests encode the refcount bug rather than catching it.
- **Windows CRT handle limit** raised to 8192 before `main` (`ResourceManager.cpp:19-36`) — a real fix for
  loading Sponza-scale texture sets.
- **FontLoader**: correct negative-result caching, and `stbtt_BakeFontBitmap`'s `result < 0` "atlas overflowed"
  case is actually distinguished from failure and warned about (`FontLoader.cpp:73-80`).

---

## Limitations

These are design boundaries rather than bugs, listed so the support claim stays honest:

- `GltfSceneLoader` owns nothing it creates. Its destructor is `= default` (`GltfSceneLoader.cpp:78`) and
  `m_textureCache` is cleared, not destroyed, at the start of every `load()` (`:90`). Every vertex buffer,
  index buffer, texture, view and sampler produced from a glTF file lives until process exit. There is no
  "unload a model" path anywhere.
- `ResourceManager` registers **no** loaders (`ResourceManager.cpp:309-310`) — it is infrastructure only, and
  `GltfSceneLoader` / `FontLoader` / `Sprite2DManager` do not go through it. So the cache, the budget, and the
  async path are currently unexercised by the engine's own asset loading.
- `IResourceLoader::unload()` (`ResourceManager.h:135`) is **never called from anywhere** in the repo.
  Resource teardown relies entirely on the `shared_ptr<void>` deleter surviving `static_pointer_cast`.
- All GPU uploads are synchronous: `vkQueueSubmit` + `vkQueueWaitIdle` per buffer and per texture
  (`GPUResourceFactory.cpp:154-155`, `322-323`). No transfer queue, no batching, no persistent staging ring.
- `FontLoader` is ASCII 32–126 only, `kNumChars = 96`, fixed 512×512 R8 atlas expanded to RGBA8, no SDF, no
  kerning, no fallback fonts, glyph map keyed by `char` (`FontLoader.cpp:19-21,124`;
  `FontLoader.h:30`). Non-ASCII text is silently unrenderable. `stbtt_BakeFontBitmap` is called with font
  offset 0, so `.ttc` collections only ever yield face 0 (`FontLoader.cpp:68-71`).
- `Sprite2DManager` has no atlas packer — one `GPUTexture` per file, one shared unit quad, mips disabled
  (`Sprite2DManager.cpp:112`), `CLAMP_TO_EDGE`. It does destroy what it owns (`:30-38`), unlike the glTF loader.
- No hot-reload / file-watching for glTF, fonts or sprites; `ResourceManager::hotReload` exists but only for
  resources that went through a registered loader.

---

## Findings

### CRITICAL

**R-1 — One descriptor set is mutated between recorded dispatches; all IBL passes write only the last face**
`IBLGenerator.cpp:303-365` (equirect), `:409-465` (irradiance), `:507-569` (prefilter).

A single `VkDescriptorSet` is allocated before the loop. Inside the loop the code does
`vkUpdateDescriptorSets` → `vkCmdBindDescriptorSets` → `vkCmdDispatch`, all into one command buffer that is
not submitted until `m_device.endSingleTimeCommands(cmd)` at `:376` / `:475` / `:580`. Vulkan descriptor sets
are not snapshotted at bind time — they are read by the shader at execution time. At submission the set holds
the **last** write, so all six recorded dispatches sample and store through face 5's views. Faces 0–4 of the
environment cubemap and irradiance map are never written (undefined memory, transitioned to
`SHADER_READ_ONLY` at `:368` / `:467` regardless), and face 5 is overwritten six times with six different
`faceIndex` push constants. The prefilter loop has the same structure over 10 mips × 6 faces (60 dispatches,
one set).

Fix: allocate one descriptor set per dispatch (the pool already has `maxSets = 64` and
`FREE_DESCRIPTOR_SET_BIT`, `:162-179`), or use push descriptors.

Confidence: high on the spec reasoning; I could not run the sample to confirm the visual symptom.

**R-2 — The prefilter shader samples environment mips that are never generated**
`prefilter_convolution.comp:96-97`, `IBLGenerator.cpp:319-365`, `VulkanCubemap.cpp:21-29`.

`createEnvironmentMap` allocates 11 mip levels. `convertEquirectToCubemap` only ever writes
`getFaceView(face, 0)` — there is no blit chain, no `generateMipmaps` equivalent for the cubemap. The
prefilter shader then does `textureLod(environmentMap, L, pc.roughness * 4.0)`, and the cubemap's sampler is
created with `maxLod = m_mipLevels` (`VulkanCubemap.cpp:241`), so mips 1–4 are sampled as uninitialised device
memory for every roughness > 0. Result is non-deterministic garbage (or zeros) in the entire specular IBL
chain above mip 0.

Fix: generate the environment mip chain after the equirect pass, or clamp `mipLevel` to 0.

**R-3 — 16-bit index selection uses index *count* instead of maximum index *value***
`GltfSceneLoader.cpp:497`.

```cpp
bool use16Bit = (indexCount <= 65535);
```

Indices address the primitive's own attribute accessors, whose length is `posAccessor->count`, not
`indexCount`. A primitive with 100 000 vertices and 60 000 indices takes the 16-bit path and every index
≥ 65536 is silently truncated by the `static_cast<uint16_t>` at `:508` — scrambled triangles, no diagnostic.
This is reachable with ordinary content (any partially-indexed or high-vertex-count primitive). The correct
predicate is `vertexCount <= 65536` (or the actual max index value).

### MAJOR

**R-4 — Equirectangular → cubemap mapping is vertically flipped under Vulkan texture conventions**
`equirect_to_cubemap.comp:46-47`, `IBLGenerator.cpp:210`.

`uv.y = theta / PI + 0.5` maps `dir.y = +1` (up) to `v = 1`. stb_image returns row 0 = the top scanline, and
Vulkan places row 0 at `v = 0`, so `v = 1` is the *bottom* of the HDR — the ground. This formula is lifted
from LearnOpenGL, which compensates with `stbi_set_flip_vertically_on_load(true)` because GL's `v = 0` is at
the bottom; that call does not exist anywhere in this repo (verified by grep). The sky ends up upside down,
and so does all the lighting derived from it. Fix: `uv.y = 0.5 - theta / PI`.

**R-5 — Prefilter roughness↔mip mapping disagrees with the shader that consumes it**
`IBLGenerator.cpp:524` writes `roughness = mip / (mipLevels - 1)` with `mipLevels = calculateMipLevels(512) = 10`,
so mip *m* holds roughness *m*/9. `pbr_ibl_lighting.frag:136-138` reads
`mipLevel = roughness * 4.0` under an explicit comment "Typically prefilter map has 5 mip levels". A
roughness-1.0 surface therefore samples mip 4, which was convolved for roughness 0.444, and mips 5–9 are dead
memory. Rough surfaces render far too glossy. Either build 5 mips or use `MAX_REFLECTION_LOD = 9.0`.

**R-6 — The BRDF LUT is computed on the CPU, single-threaded, while the compute shader for it sits unused**
`IBLGenerator.cpp:590-672`; `brdf_lut.comp` (and its committed `.spv`) never referenced.

512 × 512 × 1024 ≈ 2.7 × 10⁸ inner iterations, each with two `sqrt`, `sin`, `cos` and `pow(x, 5)` — a
multi-second, unparallelised, uninterruptible startup stall on the main thread. The in-code justification
("compute shader version needs storage image support", `:591`) does not hold: the generator already creates
three `STORAGE_IMAGE` cubemaps and `rg16f` storage-image support is mandatory in Vulkan core. The comment at
`:592` also says RGBA16F while the code produces `R32G32B32A32_SFLOAT` (`:669`) — 4 MB where 0.5 MB of RG16F
would do.

**R-7 — CPU BRDF LUT uses a different (wrong) geometry term than the GLSL reference beside it**
`IBLGenerator.cpp:625,644` vs. `brdf_lut.comp:50-53`.

CPU: `a = roughness * roughness;` then `k = (a * a) / 2.0f` → **k = roughness⁴/2**.
Shader: `float a = roughness; float k = (a * a) / 2.0;` → **k = roughness²/2** (the standard IBL Smith-Schlick
k). At roughness 0.5 that is k = 0.031 instead of 0.125 — less shadowing, so the LUT is too bright across the
mid-roughness band. Two implementations of the same integral that disagree, with the wrong one shipping.

**R-8 — Cached resources are pinned forever: `referenceCount` is incremented and never decremented**
`ResourceManager.cpp:101`. `retrieve()` does `referenceCount++` on every call; grep across the repo finds no
decrement and no release API. Both `evictUnreferenced()` (`:130`) and `findEvictionCandidates()` (`:201`)
select only `referenceCount == 0`. Consequence: any resource that has ever been fetched once is permanently
un-evictable — `garbageCollect()` is a no-op for live data, and `store()`'s budget loop at `:77-83` spins,
finds `candidates.empty()`, `break`s, and stores anyway. The memory budget silently does not hold.
`ResourceCacheTest.cpp:229-246` asserts this behaviour as correct.

**R-9 — Draco / meshopt geometry loads as silent garbage instead of failing**
No decoder is wired into `cgltf_options` (`GltfSceneLoader.cpp:99`), and cgltf does not reject unsupported
`extensionsRequired`. Compressed primitives' attribute accessors have no `bufferView`, and
`cgltf_accessor_read_float` treats that as "return zeros, success" (cgltf.h). So `load()` returns
`success = true` with a correct-looking vertex count, all at the origin. A Draco/meshopt check before the node
walk, returning a real error, would cost a dozen lines.

**R-10 — Async loading is unusable with any GPU-touching loader**
`ResourceManager.cpp:453-462` dispatches `loadResource` onto 4 worker threads. Every upload helper the engine
has (`GPUResourceFactory::uploadBuffer` `:101-161`, `createTexture2DWithData` `:234-329`) allocates from a
single `VkCommandPool` and submits to a single `VkQueue`, both externally synchronised objects in Vulkan.
Two concurrent loads are a data race on both. Also: `loadResource` reads `m_extensionToLoader` / `m_typeToLoader`
without holding `m_mutex` (`:395-403`) while `registerLoader` (`ResourceManager.h:246-250`) writes them with no
lock at all. Currently latent — nothing registers a loader — but the API invites it.

### MINOR

**R-11 — `flattenHierarchy` and `srgbAlbedo` are plumbed end-to-end and then ignored**
Declared at `GltfSceneLoader.h:119,124`, mirrored in `Facade/FacadeTypes.h:114,117`, copied across in
`src/Facade/EngineAPI.cpp:213,216`, asserted in `tests/facade/FacadeTypesTest.cpp:96,99` — and never read in
`GltfSceneLoader.cpp`. Transforms are always baked (`:323`, `:425`); albedo sRGB is hardcoded `true` at
`:349`. `maxTextureSize` is honoured only as a `cout` warning (`:618-624`, self-documented as "resize not yet
implemented"), and the examples all set it to 4096 expecting it to do something.

**R-12 — Skinned meshes upload their geometry twice**
`GltfSceneLoader.cpp:253-266`: `processPrimitive` builds and uploads a full `StandardVertex` buffer, then the
skinned path immediately builds a second `SkinnedVertex` buffer and destroys the first. Every skinned vertex
is read from the accessor twice and staged to the GPU twice.

**R-13 — `tinyobjloader` is vendored and on the public include path but nothing uses it**
`third_party/tinyobjloader/tiny_obj_loader.h`, `CMakeLists.txt:69`. Grep across the repo finds zero
references. Dead dependency and dead include-path entry.

**R-14 — Failed loads leak map entries and orphan cache entries**
`ResourceManager.cpp:412` calls `generateHandle` before the load is known to succeed; on failure the handle
lands in `m_nameToHandle`/`m_handleToName` with nothing in the cache, and `m_handleToName` grows unboundedly
across retries. Separately, `registerResource` (`ResourceManager.h:285-291`) mints a fresh id every call, so
re-registering the same name orphans the previous cache entry — unreachable by name, still counted against
`m_currentMemoryUsage`.

**R-15 — `AsyncLoader::waitForAll()` does not wait for in-flight tasks**
`ResourceManager.cpp:261-270`: it busy-spins (`std::this_thread::yield()`) until the *queue* is empty and
returns while workers may still be running. The unit tests work around this with
`sleep_for(10ms)` / `sleep_for(50ms)` (`ResourceCacheTest.cpp:313,326`), which is the tell. `~ResourceManager`
survives only because `m_asyncLoader` is declared last and so destructs (and joins) first.

**R-16 — `R32G32B32A32_SFLOAT` sampled with `VK_FILTER_LINEAR`, and 16× anisotropy, without capability checks**
The HDR equirect (`IBLGenerator.cpp:219`) and the BRDF LUT (`:669`) are 128-bit float images sampled through
linear-filtering samplers (`:185-186`, `VulkanTexture::createTextureSampler`).
`VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` is optional for 32-bit float formats; RGBA16F would be
both universally filterable and half the memory. Separately, `GltfSceneLoader.cpp:650` passes
`maxAnisotropy = 16.0f` with no clamp against `limits.maxSamplerAnisotropy` (the `samplerAnisotropy` *feature*
is correctly enabled and required at `src/Vulkan/VulkanDevice.cpp:103,246`, but the limit is not consulted).
`generateMipmaps` (`GPUResourceFactory.cpp:564`) likewise does not query
`VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` before blitting.

**R-17 — Every `cgltf_accessor_read_*` return value is discarded**
`GltfSceneLoader.cpp:424, 431, 440, 449, 753, 884, 895, 959, 965, 973, 983, 992`. A read failure (null buffer
data, element size too small for the accessor type) leaves the stack initialiser in place and is
indistinguishable from a legitimate zero. Combined with R-9 this is why compressed geometry loads as
"successful".

**R-18 — Joint indices are not range-checked**
`GltfSceneLoader.cpp:981-987` writes whatever `JOINTS_0` contains into `SkinnedVertex::joints` with no
`< skeleton->jointCount()` clamp. `cgltf_validate` does not check joint index bounds. A malformed or
mismatched rig indexes past the end of the joint matrix array on the GPU.

**R-19 — Loading the same glTF file twice duplicates all its GPU memory**
`GltfSceneLoader.cpp:90` clears `m_textureCache` at the top of every `load()`. Combined with the absence of
any destroy path (see Limitations), a second `load()` of the same file re-uploads every texture, and the
first set becomes permanently unreachable. Geometry is never deduplicated at all, even within one load — N
nodes referencing one mesh produce N uploads (`:237-274`).

**R-20 — Skinned entity transform decomposition is hand-rolled and fragile**
`GltfSceneLoader.cpp:1045-1063` extracts scale from column lengths, then Euler angles assuming a specific
Y→X→Z convention. Negative scale, shear, and gimbal-degenerate orientations are all mishandled. `glm::decompose`
or storing the quaternion directly would be exact.

**R-21 — Multi-skeleton files load duplicate animation clips and bind the wrong skeleton**
`GltfSceneLoader.cpp:143-149` re-parses **all** animations once per skeleton, keeping every clip whose channels
matched; `:178-183` then binds `result.skeletons[0]` to every skinned primitive regardless of which skin the
node actually referenced (the TODO is in the code). `m_skinCache` is populated but never consulted after
`load()`'s prologue.

**R-22 — `FontLoader.cpp:45` structured-binding names are swapped**
`auto [inserted, _] = m_cache.emplace(...)` — `inserted` is the iterator and `_` is the success flag. Behaviour
is correct; the names say the opposite of what they hold.

---

## Robustness against malformed input

The loader is in better shape here than the rest of this report might suggest, and the reason is one line:
`cgltf_validate` at `GltfSceneLoader.cpp:117`, with its result checked. That covers, from cgltf 1.15's
implementation:

- accessor `offset + stride*(count-1) + elementSize` ≤ bufferView size
- bufferView `offset + size` ≤ buffer size
- **index values < attribute count** (`cgltf_calc_index_bound`) — the classic OOB-vertex-fetch surface
- sparse index bound < accessor count, and legal sparse index component types
- index accessors restricted to scalar u8/u16/u32 with matching stride
- all attributes within a primitive having equal counts, non-zero
- node parent cycles (Floyd), and scene roots having no parent

So out-of-bounds accessor indices, truncated buffers, and hostile index values are rejected before any of this
code runs. Huge `count` values are caught by the bufferView size check. `cgltf_parse_file`,
`cgltf_load_buffers` and `cgltf_validate` failures each produce a distinct error string and an early return
with `success = false` (`:103-122`) — no leaks, `cgltf_free` on both post-parse paths.

What is **not** defended:

- **Absolute-path escape from image URIs.** `resolveTexturePath` (`:297-305`) only prefixes `m_basePath` when
  `texPath.is_relative()`. A URI of `C:/Windows/...` or `/etc/passwd` is passed straight to `stbi_load`. Relative
  `../../..` traversal also works (no normalisation, no containment check against `m_basePath`). The impact is
  bounded — stb either decodes it as an image or fails — but an attacker-supplied `.gltf` chooses what file the
  process opens. cgltf applies the same lack of containment to buffer URIs.
- **Joint indices** (R-18) — the one index class `cgltf_validate` does not bound-check.
- **Compressed geometry** (R-9) is accepted as valid and produces zeros rather than an error.
- **Discarded read return codes** (R-17) turn several classes of malformed accessor into silent defaults.
- **`data->scene->nodes[i]`** is dereferenced without a null check (`:155`); cgltf's PTRFIXUP makes a null here
  unlikely, and `cgltf_validate` checks scene roots have no parent, but the check is absent on this side.
- **Recursion depth**: `processNode` (`:217-281`) recurses per node with no depth cap. cgltf's cycle check
  prevents infinite recursion; a legitimately deep (tens of thousands) linear chain would still be a
  stack-depth question. Low practical risk.
- **`stbi_load` output is trusted**: `width * height * 4` at `:627` is computed in `VkDeviceSize` from `int`s —
  fine — but there is no upper bound on decoded image dimensions before allocating the staging buffer, so a
  crafted (or merely enormous) PNG dictates an unbounded allocation. `maxTextureSize` warns instead of acting
  (R-11).
- **`FontLoader::bakeFont`** passes an unvalidated font blob to `stbtt_BakeFontBitmap` with offset 0
  (`FontLoader.cpp:68`); stb_truetype's parser is the trust boundary and receives no pre-check.

---

## Open questions

1. **Does the sponza example actually render correct IBL today?** R-1 (descriptor aliasing) and R-2
   (unwritten environment mips) both predict visibly broken output, yet the example ships with an
   `isnan/isinf` debug branch in `pbr_ibl_lighting.frag:119-120` that outputs green — which suggests someone
   was already chasing bad IBL samples. Running it with validation layers on would settle R-1 immediately.
2. **Is R-4 (flipped equirect) masked by a compensating flip elsewhere?** I checked the whole repo for
   `stbi_set_flip_vertically_on_load` and the skybox consumer; I did not trace whether the skybox pass negates
   Y in the sampling direction.
3. **Does `SkeletalAnimationSystem` implement CUBICSPLINE?** The loader stores the 3× in/value/out layout
   faithfully; whether playback honours it is outside this scope but determines whether the "supported" row in
   the matrix is real.
4. **Is `ResourceManager` intended to become the path for glTF/font/sprite loading?** Its refcount, budget and
   async design are all shaped for that, but nothing uses it, and R-8/R-10 would surface the moment it did.
5. **Why is `GltfSceneLoader::m_defaultTextures` (`GltfSceneLoader.h:255`) declared?** It is never assigned;
   missing-texture fallback is handled instead in `src/Vulkan/FrameGraph/RenderGraph.cpp:2279-2285`. Probably
   just dead, but worth confirming before deleting.
6. **Intended lifetime story for glTF GPU resources** — is "load once, live until exit" the deliberate design,
   or an unfinished ownership model? It determines whether R-19 is a bug or a documented constraint.
