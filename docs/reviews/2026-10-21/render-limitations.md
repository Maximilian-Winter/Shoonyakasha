# Shoonyakasha — Rendering Limitations and Possible Enhancements

**Date:** 2026-10-21
**Scope:** repository at commit `9aafb7c` (branch `master`): the frame graph and pipeline JSON, the scene features of the default pipeline, and the Python API that examples drive rendering through.
**Method:** three read-only searches (frame graph and pipeline JSON; scene features; Python API), each required to cite `file:line` for every claim, with the central claims (2D-only images, sample count, dispatch sizes, Python setters, texture loading) checked again by hand. No code was modified.

Effort is a rough guess: **S** a day or less, **M** a few days, **L** a week or more. "Unlocks" names what an enhancement would let the examples, or new ones, do.

---

## 1. Images and render targets

| # | Limitation | Evidence | Effect in practice |
|---|---|---|---|
| 1.1 | **No 3D or 1D images.** Every image is created as `VK_IMAGE_TYPE_2D` with depth 1; `ImageDesc` has no depth field. | `src/Vulkan/VulkanImage.cpp:99,102`; `include/Vulkan/FrameGraph/FrameGraphResource.h:98-122` | Volume data has to live in a 2D array with interpolation between layers done by hand (the stage's fog grid, `fog_scatter.comp`, `fog.glsl` `fogAt`). No 3D LUTs for colour grading, no 3D noise textures. |
| 1.2 | **No MSAA.** `samples` is parsed but never passed on; the sample count is fixed at 1; no resolve attachments. The pipeline JSON reference implies it works. | `src/Vulkan/FrameGraph/FrameGraphJson.cpp:1381-1382`; `src/Vulkan/VulkanImage.cpp:110`; `src/Vulkan/FrameGraph/FrameGraphExecutor.cpp:129-135`; `docs/reference/pipeline-json.md:35` | Anti-aliasing comes from TAA only, which smears fast changes (the stage's strobes) and thin geometry. |
| 1.3 | **Fixed format list, no compressed or packed formats.** Only the formats in `s_formatMap` are accepted; no BCn/ASTC/ETC, no `B10G11R11_UFLOAT`, no `A2B10G10R10`. | `src/Vulkan/FrameGraph/FrameGraphJson.cpp:39-106` | HDR targets cost 8 bytes a pixel (RGBA16F) where 4 would do. |
| 1.4 | **No image copy or blit from JSON.** A `transfer` pass only changes barrier stages. | `docs/reference/pipeline-json.md:150`; `src/Vulkan/FrameGraph/FrameGraphSchedule.cpp:770-778` | History copies are written as passes (TAA's `TAAHistory`, the stage's `FogHistory`). |
| 1.5 | **No stencil.** Depth-stencil formats exist, but no stencil attachment is set and there is no stencil state in JSON. | `src/Vulkan/FrameGraph/FrameGraphExecutor.cpp:146-152` | No stencil masking for decals, portals or outlines. |
| 1.6 | **`transient` has no effect.** Parsed, reserved for a later aliasing phase. | `include/Vulkan/FrameGraph/FrameGraphResource.h:114-115` | Every target keeps its own memory; no aliasing between passes. |

**Possible enhancements**

- **3D images (M).** Add `depth` (and `depthScale`) to `ImageDesc`, create `VK_IMAGE_TYPE_3D` images and `VIEW_TYPE_3D` views, accept `viewType: "3d"`, and let storage-image bindings and `dispatch.z` resolve from the image's depth. *Unlocks:* hardware-filtered froxel grids (the stage's fog could drop its manual slice interpolation), 3D LUT colour grading, 3D noise.
- **MSAA (M–L).** Pass `samples` through image creation and pipeline creation, add `resolve` to attachment accesses (`resolveImageView` in `VkRenderingAttachmentInfo`). Deferred pipelines would still rely on TAA; forward and UI passes benefit first.
- **More formats (S).** Add `B10G11R11_UFLOAT_PACK32`, `A2B10G10R10_UNORM_PACK32` and the BC formats to the map. *Unlocks:* half-size HDR targets; BC textures once the loader can read them (§5).
- **Copy and blit passes (S–M).** An execution type `copy` / `blit` with source, destination and regions, recorded by the executor. *Unlocks:* history copies without a shader pass, mip generation for graph images.
- **Stencil (M).** A stencil attachment in rendering info and `stencil` state (ops, compare, reference) in pipeline JSON.

## 2. Pipeline state

| # | Limitation | Evidence | Effect in practice |
|---|---|---|---|
| 2.1 | **One blend state for every colour attachment;** write mask always RGBA; no logic op. | `src/Vulkan/VulkanPipeline.cpp:436-454` | A pass whose main output blends cannot plainly write its other outputs: the showroom's and stage's `LightingRT` clear its reflection and shadow targets to 0 so the additive blend acts as a write. |
| 2.2 | **Blend constants are never set;** the only dynamic states are viewport and scissor. | `src/Vulkan/FrameGraph/FrameGraphCompiler.cpp:1071-1072` | The constant-colour and constant-alpha blend factors cannot be used. |
| 2.3 | **Unknown names fall back silently:** blend factor → ONE, blend op → ADD, cull mode → `back`, topology → triangle list. | `FrameGraphCompiler.cpp:878,881-888,906,915` | A typo renders wrongly instead of failing at load (depth compare ops, by contrast, throw). |
| 2.4 | **Vertex, fragment and compute shaders only.** No geometry, tessellation, mesh or task shaders. | `src/Vulkan/FrameGraph/FrameGraphJson.cpp:1524-1526` | — |
| 2.5 | **Ray queries only, no ray tracing pipelines** (no raygen/hit/miss shaders, no `vkCmdTraceRays`). | no evidence in `src`/`include`; `docs/reference/pipeline-json.md:125` | Path tracers run as compute or fragment shaders with ray queries (`reference_pt`, `wave_optics`); no hardware-scheduled hit shaders, no shader binding table. |
| 2.6 | **Pipeline creation failures are warnings;** a pass over the device's push-constant limit draws nothing. | `FrameGraphCompiler.cpp:1086-1091,1099-1103` | Problems surface as missing images rather than load errors. |
| 2.7 | No JSON keys for `frontFace`, `lineWidth`, `primitiveRestart`, depth bounds. | `FrameGraphJson.cpp:1522-1556` | — |

**Possible enhancements**

- **Per-attachment blend state (S–M).** Accept `blending` on each output (or a `blendStates` array), and a `writeMask`. *Unlocks:* MRT passes that blend one target and write others; the clear-to-zero workaround in `LightingRT` goes away.
- **Strict names (S).** Throw on unknown blend factors, ops, cull modes and topologies, as depth compare ops already do.
- **Blend constants as dynamic state (S),** set from a dot-path.
- **Ray tracing pipelines (L).** Pipeline creation with raygen/miss/hit groups, a shader binding table, and an execution type `trace_rays`. Only worth it for a path tracer that needs hit shaders per material.

## 3. Passes, draws and dispatches

| # | Limitation | Evidence | Effect in practice |
|---|---|---|---|
| 3.1 | **No indexed draws and no custom vertex or index buffers from JSON.** `draw` binds no buffers; `vertexInput` only picks a format. | `src/Vulkan/FrameGraph/FrameGraphExecutor.cpp:554-575`; `FrameGraphCompiler.cpp:999-1014` | Only the engine's entity passes draw meshes; a pipeline cannot draw a GPU-generated mesh or a debug line list from a buffer. |
| 3.2 | **Dispatch sizes from a resource cover width and height, not z;** `vertexCount` from a resource is parsed but not implemented. | `FrameGraphExecutor.cpp:556-565,597-600` | The stage's fog passes hard-code `dispatch.z = 96` and must be edited together with `arrayLayers`. |
| 3.3 | **No async compute.** `queue: compute` only records intent; the application runs the single-queue path. | `src/App/ApplicationBase.cpp:706`; `src/Vulkan/FrameGraph/RenderGraph.cpp:2069` | Compute passes (fog, VSM, GTAO) serialise with graphics. |
| 3.4 | **No texel buffers;** bindless only as the `sceneTextures` array, which needs ray queries. | `FrameGraphJson.cpp:230-246,1300-1306`; `FrameGraphCompiler.cpp:765-767,812-827` | — |
| 3.5 | **Unknown execution types record nothing** with a warning. | `FrameGraphExecutor.cpp:665-672` | — |

**Possible enhancements**

- **Buffer-driven draws (M).** Let `draw`, `draw_indexed` and their indirect forms bind a vertex and index buffer from `bufferLayouts`. *Unlocks:* GPU particles drawn as geometry, debug lines (the stage's laser beams could be drawn as lines for reflections), meshes generated by compute.
- **`dispatch.z` and `vertexCount` from resources (S).** Resolve `layers` (and later `depth`) as a dimension.
- **Async compute in the application path (L).** Wire `executeMultiQueue` into `ApplicationBase` with the existing batch and sync-point generator.

## 4. Python API

| # | Limitation | Evidence | Effect in practice |
|---|---|---|---|
| 4.1 | **Only scalars, vectors and `mat4` reach shaders:** `set_custom_float/vec2/vec3/vec4/mat4/uint`; no int vectors, no arrays, no buffer upload at runtime. | `python/shoonyakasha/_shoonyakasha.pyx:2131-2157`; `docs/guides/custom-shader-uniforms.md:15,37` | Lists of things become numbered fields: the stage's `machine0`…`machine3`, `laser.origin0`…`color3` (17 `vec4` fields for four projectors). |
| 4.2 | **Images only from files.** `set_pipeline_image(name, path)` takes a path; no pixel data or NumPy arrays. | `_shoonyakasha.pyx:2283-2288` | Examples write PNGs to disk first (`showroom/studio.py`, `optics/optics_common.py`). |
| 4.3 | **No readback.** No buffer or intermediate-image readback to Python; `capture_screenshot` saves the final frame to a file. | `docs/guides/compute-and-data-flow.md:37`; `_shoonyakasha.pyx:1854-1860` | Measurements need screenshots and image analysis outside the engine. |
| 4.4 | **No procedural meshes.** Geometry comes only through `load_gltf_scene`; no vertex updates. | `_shoonyakasha.pyx:1954-1988`; `include/Facade/EngineAPI.h:221` | Examples write glTF files from Python (`showroom/studio.py`, `stage/stage_set.py`, `alley/kit.py`). |
| 4.5 | **No hot reload.** The pipeline JSON is chosen at construction; shaders compile only when an example starts. | `_shoonyakasha.pyx:1657,1686-1699`; `docs/guides/json-render-pipeline.md:37` | Each shader change means restarting the example. |
| 4.6 | **One camera.** No second camera rendering into a target, no split views. | `docs/reference/pipeline-json.md:104,245` | — |
| 4.7 | **No per-pass callbacks from Python** (only frame-level hooks and ECS systems). | `_shoonyakasha.pyx:1738-1776`; `docs/guides/compute-and-data-flow.md:17,37` | — |
| 4.8 | **The Python validator differs from the native parser.** | `docs/reference/pipeline-json.md:327` | `sk.pipeline.check` can pass a pipeline the engine rejects, or the reverse. |

**Possible enhancements**

- **Buffer upload (M).** `engine.set_buffer(name, bytes_or_array, offset=0)` writing a `bufferLayouts` buffer (uniform or storage) before this frame's uploads, with NumPy support through the buffer protocol. *Unlocks:* any list of lights, beams, particles or emitters without numbered fields; the stage's lasers and smoke machines as arrays.
- **Images from memory (S–M).** `set_pipeline_image(name, array, srgb=...)` from a NumPy array or bytes. *Unlocks:* generated textures without temporary files.
- **Readback (M).** `engine.read_image(name)` and `engine.read_buffer(name)` returning NumPy arrays, a frame or more late, built on the existing readback path. *Unlocks:* tests and measurements inside the engine (noise, convergence, the comparisons done by hand for the showroom's filters).
- **Procedural meshes (M).** `scene.create_mesh(positions, normals, uvs, indices, material)`.
- **Hot reload (M–L).** Recompile changed shaders and recreate the affected pipelines on a key or a file watch; reload the pipeline JSON with a graph recompile.
- **Array setters (S).** `set_custom_vec4_array(key, values)` for fields with `arrayCount`.

## 5. Scene features (default pipeline unless noted)

| # | Limitation | Evidence | Note |
|---|---|---|---|
| 5.1 | **Lights:** 8192 scene lights; at most 63 per cluster, extra lights dropped; no cookies, no IES. Area lights only in the showroom and stage pipelines. | `python/shoonyakasha/pipelines/default/pipeline.json:515-518`; `.../shaders/clusters.glsl:12` | The engine packs rectangle and image-slot data (`src/FrameGraph/DotPathResolver.cpp:104-111`); the default pipeline does not read it. |
| 5.2 | **Shadow maps:** 8 spot and 4 point shadow slots per frame, one shadowed directional light, 4 cascades. | `.../shaders/common.glsl:59-60`; `docs/guides/default-pipeline.md:68,251-252` | Lights are ranked each frame; the rest get no shadow. |
| 5.3 | **Ray-traced shadows:** the sun only in the default pipeline. Spot, point and area lights trace shadows only in the showroom and stage pipelines. | `python/shoonyakasha/pipelines/default/README.md:166-187` | — |
| 5.4 | **The acceleration structure holds static, opaque, shadow-casting meshes only;** skinned, alpha-tested and blended meshes are left out; bottom levels are built once, never refit. | `include/Vulkan/RayTracingScene.h:9-13`; `src/Vulkan/RayTracingScene.cpp:129-158,298-304,320-321` | Rigid motion works (the top level is rebuilt each frame); deforming meshes do not show in rays. |
| 5.5 | **Materials:** clear coat, transmission, IOR and iridescence are loaded but not shaded by the default pipeline (the showroom shades clear coat); volume, sheen, specular, emissive strength and `KHR_texture_transform` are not loaded; only `TEXCOORD_0`; `normalTexture.scale`, occlusion `strength` and glTF samplers are ignored. | `src/Resources/GltfSceneLoader.cpp:486-519,549,823-830`; `.../shaders/surface.glsl:34,86` | Meshes carry no tangents; normal maps use screen-space derivatives. |
| 5.6 | **Geometry:** no morph targets; shared meshes still draw one by one; no LOD; frustum culling only. | `include/Resources/AnimationData.h:89-93`; `docs/guides/instancing.md:11`; `include/FrameGraph/ViewCulling.h` | — |
| 5.7 | **Textures:** whatever stb_image decodes, always uploaded as RGBA8; no KTX2/DDS/Basis; no streaming; `max_texture_size` does not resize. | `src/Resources/GltfSceneLoader.cpp:767-803`; `docs/guides/loading-scenes.md:36` | Large scenes (Bistro) use full-size uncompressed textures. |
| 5.8 | **Lighting and effects:** no GI, no SSR, no OIT, no refraction, no motion blur in the default pipeline. Depth of field, fog, ray-traced reflections and their filters exist only in the showroom and stage pipelines; the Kajiya ports (`restir_gi`, `irradiance_cache`, `rt_reflections`) are separate pipelines. | `docs/guides/default-pipeline.md:163-244` | — |

**Possible enhancements**

- **Refit or rebuild bottom levels for skinned meshes (M).** Build skinned BLASes with `ALLOW_UPDATE` and refit them after skinning (needs skinned vertex output in a buffer). *Unlocks:* ray-traced shadows and reflections of animated characters.
- **Alpha-tested geometry in ray tracing (M).** Non-opaque instances with an any-hit-style test in ray queries (`rayQueryProceedEXT` candidate loop reading the material's alpha). *Unlocks:* foliage and fences in ray-traced shadows.
- **Tangents (S–M).** Load `TANGENT` or generate MikkTSpace tangents. *Unlocks:* correct normal maps on mirrored UVs and at grazing angles.
- **More glTF material extensions (M each).** Shade transmission (screen-space refraction from the opaque scene), sheen and specular; read `KHR_texture_transform` and `texCoord` sets.
- **Compressed textures (M).** KTX2 with BC7/BC5/BC1 (and the formats in §1). *Unlocks:* a quarter of the texture memory for large scenes.
- **Real instancing (M).** Draw entities that share a mesh and material with one instanced draw.
- **Area lights, ray-traced light shadows and the shadow and reflection filters in the default pipeline (M).** They exist in the showroom pipeline and could move into the default one.

## 6. Documentation errors found

- `docs/reference/pipeline-json.md:35` describes `samples` as a working image key; the value is never used (§1.2).
- `python/shoonyakasha/pipelines/default/README.md:150` says the per-draw push constants are 184 bytes, `docs/guides/default-pipeline.md:144` says 180; `pipeline.json` declares 184.

## 7. A suggested order

1. **Buffer upload and array setters from Python (§4)** — the most frequent workaround in the examples, and the base for GPU-driven effects.
2. **Per-attachment blend state and strict names (§2)** — small, and removes silent failures.
3. **3D images and `dispatch.z` from resources (§1, §3)** — volumetrics and grading.
4. **Readback to Python (§4)** — tests and measurements inside the engine.
5. **Tangents, then skinned and alpha-tested geometry in ray tracing (§5)** — correctness of what is already drawn.
6. **Compressed textures and the packed HDR formats (§1, §5)** — memory.
7. **MSAA, async compute, ray tracing pipelines** — larger projects, each for a specific need.
