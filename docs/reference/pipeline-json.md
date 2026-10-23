# Pipeline JSON reference

This reference describes the current [C++ parser](../../src/Vulkan/FrameGraph/FrameGraphJson.cpp), [declarations](../../include/Vulkan/FrameGraph/FrameGraph.h), [compiler](../../src/Vulkan/FrameGraph/FrameGraphCompiler.cpp), and [executor](../../src/Vulkan/FrameGraph/FrameGraphExecutor.cpp). Start with the [walkthrough](../guides/json-render-pipeline.md) for a complete working pipeline.

## Document structure

| Key | Shape | Role |
|---|---|---|
| `name`, `version` | string, integer by convention | Metadata used by examples; the loader does not enforce a versioned schema |
| `vertexFormats` | object keyed by name | Named vertex attribute sequences |
| `bufferLayouts` | object keyed by name | Packed fields, initialization, and data flow |
| `entityDataBindings` | object keyed by name | Per-draw, material, and skeleton binding configurations |
| `samplers` | object keyed by name | Sampler state |
| `descriptorSetLayouts` | object keyed by name | Descriptor binding declarations |
| `resources` | array | Images and buffers used by passes |
| `passes` | array | Pass declarations, analyzed for execution dependencies |
| `uniformBuffers` | object keyed by name | Older explicit-size/offset UBO declarations; prefer `bufferLayouts` for new source-driven buffers |
| `presets` | object keyed by name | Named sets of pass switches and scene values, such as quality tiers; below |
| `ui` | object | The depth image the canvas UI tests world-canvas hits against; below |

These sections are conditionally parsed. Their absence is not a useful runnable pipeline: normal windowed applications need resources, a rendering pass, and a final present output. `standardBuffers` is obsolete. Unknown keys are not comprehensively rejected; accepted JSON is not proof a key is implemented.

## Resources and accesses

Each resource requires `name` and `kind` (`image` or `buffer`). `imported` defaults false. Imported resources need native backing; ApplicationBase supplies the imported `swapchain` image.

| Image key | Default | Meaning |
|---|---|---|
| `format` | native descriptor default if omitted | Use an explicit format for created targets. The Vulkan format name without `VK_FORMAT_`: `R8_UNORM`, `R8G8_UNORM`, `R8G8B8_UNORM`, `R8G8B8A8_UNORM`, `B8G8R8A8_UNORM`, `R8G8B8A8_SRGB`, `B8G8R8A8_SRGB`, `R8_SNORM`, `R8G8_SNORM`, `R8G8B8A8_SNORM`, `R16_SFLOAT`, `R16G16_SFLOAT`, `R16G16B16_SFLOAT`, `R16G16B16A16_SFLOAT`, `R32_SFLOAT`, `R32G32_SFLOAT`, `R32G32B32_SFLOAT`, `R32G32B32A32_SFLOAT`, `R8_UINT`, `R16_UINT`, `R32_UINT`, `R32G32_UINT`, `R32G32B32A32_UINT`, `R8_SINT`, `R16_SINT`, `R32_SINT`, `R16_UNORM`, `R16G16_UNORM`, `R16G16B16A16_UNORM`, `B10G11R11_UFLOAT_PACK32`, `A2B10G10R10_UNORM_PACK32`, `D16_UNORM`, `D32_SFLOAT`, `D16_UNORM_S8_UINT`, `D24_UNORM_S8_UINT`, `D32_SFLOAT_S8_UINT`. `B10G11R11_UFLOAT_PACK32` holds HDR colour without alpha in 4 bytes a texel (`r11f_g11f_b10f` as a storage image); `A2B10G10R10_UNORM_PACK32` holds 10 bits per colour channel and 2 of alpha (`rgb10_a2`). Whether a format can be an attachment or storage image depends on the device |
| `width`, `height` | 0 | Reference-size dimensions when zero |
| `widthScale`, `heightScale` | 1.0 | Scale reference-size dimensions, e.g. 0.5 for bloom |
| `mipLevels` | 1 | Mip levels, or `"full"` for a complete chain at the resolved size |
| `arrayLayers` | 1 (6 for `cube`) | Array layers, e.g. one per shadow cascade |
| `viewType` | `auto` | How shaders sampling the whole image see it: `auto` (`2d` for one layer, `2d_array` for more), `2d`, `2d_array`, `cube` (6 square layers), `cube_array` (a multiple of 6), `3d` (a volume of `depth` slices) |
| `depth` | 1 | Slices of a `3d` image; other view types take only 1. A `3d` image has one layer and a colour format, is written by compute passes as a storage image (`image3D`) and sampled as `sampler3D`, and cannot be an attachment or have a `readback` or `save`. A full mip chain also halves the depth |
| `samples` | 1 | Samples per pixel: 1, 2, 4, 8, 16, 32 or 64. A multisampled image has one mip level, is `2d` or `2d_array`, and has no `readback` or `save`; compiling fails when the device offers no such image of its format and usage. See [multisampling](#multisampling) |
| `transient` | false | The image may share memory with other transient images that no pass uses while it is in use. It is in use from the first to the last pass, in execution order, that names it as an input or output (a `resolve` target included) or binds it through one of its descriptor sets. When an image takes over its block's memory its contents are undefined, as at the start of any frame, and its first barrier waits for the previous image's last access. A transient image gets its own memory instead when it is `persistent`, has a `readback`, `save` or `target`, is the canvas UI's `depth`, or is used or bound by a pass on the compute queue or one that records through code the graph cannot see (an execute callback, or execution type `none`, `manual` or `scene_geometry`). Compiling logs the memory blocks and the memory saved. |
| `persistent` | false | Contents carry over from one frame to the next, for state a pass accumulates (an adapted exposure, a history). For last frame's image of a result, read a persistent image in the pass that makes the result, then copy the result into it in a pass after: the default pipeline's `TAA` and `TAAHistory` do this. Normally a frame's first access discards an image's contents; a persistent image's first barrier instead waits on and keeps the previous frame's. Cleared to zero (depth to 1) whenever the graph is compiled, including after a resize. A pass that writes a persistent image is never culled as unused: the next frame reads what it wrote |

Image properties live inside `image`. Buffer properties live inside `buffer`: `size` in bytes (default 0), `persistentlyMapped` (false). Creating buffers through `bufferLayouts` is separate from declaring graph resource accesses; follow the SSBO examples for imported layout-backed buffers.

Each pass input/output has a required `resource` name and `usage`. Optional `clear` is `[r,g,b,a]` or `{ "depth": 1.0, "stencil": 0 }`. Optional `present` marks the post-render presentation transition. Optional `resolve` on a `color_write` or `color_blend` output names the image a multisampled attachment resolves into; see [multisampling](#multisampling). Optional `blending` (with the `custom` factor keys) and `writeMask` on a colour output replace the pipeline's for that attachment; see [blending](#blending).

### Multisampling

Every attachment of a graphics pass has the same `samples`, and its pipeline rasterizes with that many; compiling fails otherwise. The canvas UI's pipeline follows the pass's sample count too. A multisampled colour attachment can name an image to resolve into at the end of the pass:

```json
"outputs": [{ "resource": "colorMS", "usage": "color_write", "clear": [0, 0, 0, 1], "resolve": "color" },
            { "resource": "depthMS", "usage": "depth_write", "clear": { "depth": 1.0 } }]
```

The pass then also writes `color`, as if it were declared as an output, so later passes that read it run after this one. The target is a single-sampled colour image of the same format, with one mip level as large as the rendered one and as many layers; it may be imported. Float and normalized formats resolve to the average of their samples, integer formats to sample 0. A multisampled image that no later pass reads is not stored, as for any attachment, so only its resolve target reaches memory. Depth is not resolved. Shaders read a multisampled image as `sampler2DMS` or `texture2DMS`. A copy pass needs both images with the same sample count, and a blit takes only single-sampled images.

An access covers the whole image unless it names a part: `"mip": n` or `"mips": [first, count]`, and `"layer": n` or `"layers": [first, count]`. An attachment names one mip level; one layer renders into that layer, several render layered. Ranges outside the image, and one pass using the same mip and layer in two different layouts, fail compilation. Descriptor bindings with `autoBindResource` accept the same keys and bind a view of just that part, so a downsample pass can sample mip n-1 while rendering mip n:

```json
{ "name": "Downsample2",
  "inputs":  [{ "resource": "bloomChain", "usage": "shader_read", "mip": 1 }],
  "outputs": [{ "resource": "bloomChain", "usage": "color_write", "mip": 2 }] }
```

Dependencies and barriers are tracked per mip and layer: passes writing different layers of one image (shadow cascades) are independent, a pass reading the whole image depends on all of them, and each gets barriers for only its own layers. A write that loads rather than clears (no `clear`) depends on the previous writer.

The compiler also picks each attachment's load and store operation. A write without `clear` that is the first access to its mips and layers this frame, of an image that is not `persistent`, would only load undefined contents, so it does not load them (`LOAD_OP_DONT_CARE`). A write that no later pass reads is not stored (`STORE_OP_DONT_CARE`): a later pass reads it by declaring it as an input, loading or blending onto it, or binding the image through any of its descriptor sets. Images that present, are imported or `persistent`, or have a `readback`, `save` or `target` are always stored, and so is anything a later pass with an execute callback or execution type `none`, `manual` or `scene_geometry` could reach. A depth buffer used only for its own pass's depth test is the usual case: on tile-based GPUs it then never leaves the tile.

| Usage | Meaning |
|---|---|
| `color_write`, `color_attachment_write` | Color attachment write |
| `color_blend`, `color_attachment_blend` | Color attachment read/modify/write |
| `depth_write`, `depth_stencil_write` | Depth/stencil write |
| `depth_read` | Read-only depth and stencil |
| `shader_read` | Shader read |
| `shader_read_write` | Shader read/write |
| `storage_image_write` | Storage-image write |
| `input_attachment` | Input attachment |
| `transfer_src`, `transfer_dst` | Transfer access |
| `present` | Legacy alias for color write plus presentation |

Prefer `"usage": "color_write", "present": true` on the final swapchain output. For an overlay, use `color_blend` plus `present: true`. Declare all inter-pass accesses; the graph cannot infer hazards from shader code.

## Vertex formats

A named format contains `attributes`, each with `name`, `type`, and `location`. The registry calculates offsets/stride. Match the actual uploaded geometry and GLSL locations; a declaration does not repack a mesh. See [VertexFormatRegistry](../../include/Vulkan/FrameGraph/VertexFormatRegistry.h) and the starter/skinned/sprite pipelines for compatible formats.

Meshes from glTF files and `create_mesh` hold one of two vertex layouts:

| Layout | Attributes, in order | Stride |
|---|---|---|
| standard | `position` vec3, `color` vec3, `texCoord` vec2, `normal` vec3, `tangent` vec4, `texCoord1` vec2 | 68 bytes |
| skinned | `position` vec3, `normal` vec3, `texCoord` vec2, `jointIndices` uvec4, `jointWeights` vec4, `tangent` vec4, `texCoord1` vec2 | 88 bytes |

`texCoord1` is a glTF primitive's `TEXCOORD_1`, 0 without one and on meshes from `create_mesh`. `vertexInput: "default"` is the standard layout's first five attributes at locations 0-4. The tangent's xyz points along +u, and its w (1 or -1) gives the bitangent, `w * cross(normal, tangent.xyz)`, which points up the texture image; it is 0 on meshes without uvs. Entity draws bind each mesh with its own stride, so a format can declare only the first attributes of a layout, as the starter's `standard` format does. Draws from buffers use the declared format's stride.

## Buffer layouts

| Key | Default / accepted forms |
|---|---|
| `usage` | `uniform_buffer`; also `storage_buffer`, `push_constant`, `descriptor_set` |
| `packing` | `std140`; also `std430`, `scalar`, `push_constant` |
| `updateFrequency` | `manual`; also `per_frame`, `every_n_frames`, `on_change`, `once` |
| `updateFrequencyN` | 1 for `every_n_frames` |
| `binding` | `set`, `binding`, `offset` default 0; `stages` selects shader stages |
| `fields` | Array of packed field declarations |
| `textures` | For descriptor-set layouts: `name`, `binding` (0), and `stages` |
| `elementCount` | Unsigned integer SSBO element count |
| `source`, `target`, `memory` | Initialization, output/readback/save, and memory policy; below |

Fields require `name`; `type` defaults `float`, `arrayCount` defaults 1, and `source` defaults empty. An explicit `offset`, including 0, overrides automatic placement subject to layout checks. Use `arrayCount` rather than embedding array syntax in the type name.

`default` is what a field holds while its `source` does not resolve, for instance a `scene.custom.*` value the application has not set, or a material parameter an entity lacks: a number for scalars, an array of numbers for vectors and matrices (column-major, as many as the type has components). Without one such a field is left as it was. A default that is not numbers fails at load; one with the wrong number of components is ignored with a warning:

```json
{ "name": "exposure", "type": "float", "source": "scene.custom.default.exposure", "default": 1.0 }
```

Scalar types are `float`, `double`, `int`, `uint`, `bool`; vector types are `vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`; matrix types are `mat2/3/4`. `std140`, `std430`, and scalar packing differ in alignment/array stride. The shader block layout and push-constant byte range must agree with the compiled layout; device features and limits still apply.

A field `source` is a dot-path. A layout-level `source` is an initialization object; they have different meanings. Manual/on-change scheduling is native-managed behavior, not automatic Python-object observation.

## Dot-paths

| Root | Supported values |
|---|---|
| `scene.camera` | `view`, `projection`, `viewProjection`, `prevViewProjection` (last frame's, for reprojection), `invView`, `invProj`, `position`, `fov`, `nearPlane`, `farPlane`, `aspect`, `positionVec4`, `nearFarFovAspect` |
| `scene.cameras.<name>` | The same, for the camera entity with that name; see [more cameras](../guides/cameras-and-controllers.md#more-cameras). Unresolved while no camera has the name |
| `scene.environment` | `irradianceMap`, `prefilterMap`, `brdfLUT`, `environmentMap` |
| `scene.time` | `elapsed`, `delta`, `frame` |
| `scene.screen` | `width`, `height`, `resolution`: the window's; `renderWidth`, `renderHeight`, `renderResolution`: the size the scene renders at, the window's times the [render scale](../guides/frame-capture.md#supersampling). Passes that write the swapchain run at the window's size, the others at the render size |
| `scene.lights` | `count` (all of the scene's); indexed, for fixed arrays (past the last light a field reads zero), `scene.lights[N].positionType`, `colorIntensity`, `directionRange`, `attenuation` (w: cos of the outer cone), `source` (x: source radius, y: 1 when it casts shadows, z: cos of the inner cone, w: image slot), `shape` (the emitting rectangle facing the light's direction: xyz its right axis times half its width, w half its height; without one, xyz the unit right axis and w 0) |
| `scene.shadows.sun` | `enabled`, `cascadeCount`, `splits`, `texelWorldSize`, `lightIndex`, `direction`; indexed `scene.shadows.sun.cascades[N].viewProj`. See [sun shadow cascades](../guides/lighting-and-ibl.md#sun-shadow-cascades) |
| `scene.shadows.spot`, `scene.shadows.point` | `count`; indexed `scene.shadows.spot[N].viewProj`, `lightIndex`, `params`, `rect`; `scene.shadows.point[N].lightIndex`, `positionFar`, `depthParams`; `scene.shadows.point.faces[N].viewProj`, `rect` with N = slot × 6 + face. See [spot and point light shadows](../guides/lighting-and-ibl.md#spot-and-point-light-shadows) |
| `scene.custom` | Values explicitly published under a key by the application |
| `entity.transform` | `worldMatrix`, `previousWorldMatrix` (last frame's, for motion vectors; the same as `worldMatrix` in an entity's first frame), `localMatrix`, `position`, `rotation`, `scale` |
| `entity.material` | `params.<name>`, `textures.<slot>`, `textures.<slot>.exists`, `textures.<slot>.slot` (a `uint`: the texture's slot in the [`sceneTextures`](#descriptors-and-samplers) array, `0xFFFFFFFF` for none), `alphaCutoff`, `alphaMode`, `doubleSided` |
| `entity.mesh` | `vertexCount`, `indexCount` |
| `entity.skeleton` | `hasSkeleton`, `jointCount`; `boneMatrices` for `autoBindBuffer`: this frame's bone matrices followed by last frame's, `jointCount` of each |
| `const` | Constant expressions such as `const.0`, `const.1`, `const.1.0.0.1` |
| `pass` | `repeatIndex`, `repeatCount` (see [Repeated passes](#repeated-passes)), `extent` and `texelSize` of the pass being recorded. Push-constant layouts only: a buffer is filled once per frame, so any other layout using `pass.*` fails compilation |

The constant parser uses dots as vector separators: `const.0.5` is not a reliable spelling for scalar 0.5. Publish fractional scalars as custom values. Bare identifiers address resource bindings where supported. For fields with `arrayCount > 1`, a source such as `scene.lights[i].positionType` expands `[i]` for each element. Without `[i]`, the resolved value is broadcast to the array. These paths are implemented cases in [DotPathResolver](../../src/FrameGraph/DotPathResolver.cpp), not reflection over arbitrary C++ or Python fields. Component presence, value type, and initialization matter.

## Descriptors and samplers

Each descriptor-set layout has a `bindings` array. Bindings require `binding` and `type`; optional `count` defaults 1, `name` defaults `binding_<index>`, and `stages` selects shader visibility: any of `vertex`, `tess_control`, `tess_eval`, `geometry`, `task`, `mesh`, `fragment`, `compute`, `raygen`, `miss`, `closest_hit`, `any_hit`, `intersection` and `all` (push constant `stages` take the same names). Use the descriptor types accepted by [JsonUtils](../../src/Vulkan/FrameGraph/FrameGraphJson.cpp), such as `uniform_buffer`, `storage_buffer`, and `combined_image_sampler`.

`uniform_texel_buffer` and `storage_texel_buffer` bind a storage buffer of `bufferLayouts` (named by `autoBindBuffer`, one per binding) as texels of the binding's `format`, any of the format names images take; shaders read them as `samplerBuffer` (`texelFetch`) and `imageBuffer` (`imageLoad`, `imageStore`, with a format qualifier). Only texel buffers take a `format`, and a texel buffer without one fails at load. Every storage buffer of `bufferLayouts` can be one. A binding whose buffer is not a storage buffer, or whose format the device does not offer for that kind of texel buffer, is left unbound with an error in the log. Their passes get buffer barriers as for storage buffers, a storage texel buffer counting as written.

`acceleration_structure` binds the engine's top-level acceleration structure of the scene, for ray queries (`GL_EXT_ray_query`; compile such a shader for Vulkan 1.2 or later). The engine keeps one only on a device with ray queries. It holds the opaque, shadow-casting meshes, and is rebuilt every frame from their transforms. Skinned meshes are in it posed: every frame a compute shader writes each one's vertices from its bone matrices, in the standard [layout](#vertex-formats), and its structure is refitted to them; their `rayInstances` rows name those vertices. The shader is `shaders/skinning/skin_vertices.comp.spv`, looked for beside the pipeline, in the working directory, then beside the default pipeline; without it skinned meshes stay out, with a warning in `ray_tracing.log`. With `"alphaTested": true` on the binding, the static shadow casters with alpha-tested materials (glTF `MASK`) go in too, as non-opaque instances: a ray query that does not force opacity sees their triangles as candidates, and the shader accepts a candidate or not, for instance with `skAlphaTestCandidate` from [`sk/scene_hits.glsl`](../../python/shoonyakasha/glsl/sk/scene_hits.glsl). The structure is the pipeline's, so one binding with the key puts them in for every pass; a ray query with `gl_RayFlagsOpaqueEXT` then sees them as solid. On a device without ray queries a layout with such a binding is skipped, and only a pass that `requires` `rayQuery` may use it.

A `combined_image_sampler` binding with `"source": "sceneTextures"` and a `count` holds the scene's textures: those of the materials of every mesh that renders, one slot per image view. Each texture keeps its own sampler. A draw reads its material's slots with `entity.material.textures.<name>.slot`, in its push constants for instance, and the [`rayInstances`](#initialization-memory-and-readback) rows name the same slots. Slots are given in order of first use every frame, and only the slots that changed are written. Slots past the scene's textures stay unwritten: the binding is partially bound, so a shader may only read the slots it is given. The `count` is lowered, with a warning, when it would not leave room for 256 other samplers within the device's limits. Textures past the count read as no texture. The array needs bindless textures (the device's descriptor indexing features, which every device with ray queries has): on a device without them a layout with such a binding is skipped, and only a pass that `requires` `bindlessTextures` may use it. Shaders declare it as an unsized array and index it with `nonuniformEXT`; the GLSL library's [`sk/scene_textures.glsl`](../../python/shoonyakasha/glsl/sk/scene_textures.glsl) declares it:

```json
{ "binding": 2, "type": "combined_image_sampler", "stages": ["compute"], "name": "sceneTextures",
  "count": 4096, "source": "sceneTextures" }
```

`autoBindBuffer` references a named buffer, `autoBindResource` an image/resource source, and `autoBindSampler` a named sampler. `externalImage` names an image the application supplies (`set_pipeline_image(name, path)` from a file, `set_pipeline_image_data(name, data, ...)` from texels in memory, C++ `EngineAPI::setPipelineImage`, `setPipelineImageData` or `RenderGraph::setExternalImage`): the binding samples white until it does, and takes a new one at each frame in flight's next recording, so it can change while running. With `autoBindSampler` it uses that sampler, otherwise the image's own. Pass `descriptorSets` is an ordered list of layout names; that order supplies shader set indices.

Sampler keys: `magFilter`, `minFilter`, `mipmapMode` default `linear` (also `nearest`). Use `addressMode` for all axes or `addressModeU/V/W` individually (default `repeat`; also `clamp_to_edge`, `clamp_to_border`, `mirrored_repeat`). Other keys are `borderColor` (`float_opaque_black`), `anisotropyEnable` (false; alias `anisotropy`), `maxAnisotropy` (1), `compareEnable` (false), `compareOp` (`less`), `minLod`, `maxLod`, and `mipLodBias` (all 0). Set the LOD range deliberately when sampling mipmapped textures.

## Entity data bindings

Each named binding can contain:

- `perDraw`: `layoutRef` (preferred; legacy `layout`), `method` (`push_constant`), `offset` (0), `size` (64), `stages`, `set` (0), and `binding` (0).
- `material`: `layoutRef`, `method` (`descriptor_set`), `set` (1), and optional texture-name→binding-number `bindings`. The layout's image bindings take the material's textures of the same names (white, or a flat normal and default metallic-roughness for `normalMap`, `metallicRoughnessMap` and `aoMap`, when it has none). A `uniform_buffer` binding with `"source": "material"` takes the bytes of the buffer layout named by its `name`, filled from the entity: one per material set, without `autoBindBuffer`. Its fields may read only `entity.material.*` and `const.*`. Equal materials share one set; one whose parameters or textures change gets another.
- `skeleton`: `layoutRef` for the skeleton descriptor set.

Use a matching `execution.entityDataBinding`. The parser accepting a method string is not a promise of an arbitrary binding backend; the supplied geometry examples use push constants for per-draw data and descriptor sets for materials/skeletons.

## Passes and pipeline state

Passes require `name` and `type` (`graphics`, `compute`, `transfer`, `ray_tracing`; see [ray tracing passes](#ray-tracing-passes)). `queue` defaults `graphics`. `compute` runs a compute, transfer or ray tracing pass on the device's compute queue, when it has one without graphics (async compute); a graphics pass cannot ask for it. Each queue's passes keep their execution order, and the two queues run alongside each other: a pass waits for the other queue only where it accesses an image the other queue accessed earlier in the frame, or such a buffer that some pass writes. The passes are grouped into batches, one submission each, split only where such a wait begins or ends. Compute work starts after the frame's uploads and acceleration structure builds, and the frame ends after both queues' work. Images that passes on both queues use are shared by both queue families. Without such a queue, or with `SHOONYAKASHA_DISABLE_ASYNC_COMPUTE` set to anything but `0`, every pass runs on the graphics queue. `enabled` defaults true and can be changed at runtime with `set_pass_enabled` (C++ `setPassEnabled`). A disabled pass draws and dispatches nothing, but its barriers still run and its attachments still begin and end rendering: they are cleared to their `clear` values and end in the layouts later passes expect, so a disabled shadow pass leaves a map cleared to far depth. `requires` lists device capabilities the pass needs: `rayQuery`, `drawIndirectCount` for a `draw_indirect` pass with a count buffer, `geometryShader`, `tessellationShader`, `meshShader`, `taskShader`, `rayTracingPipeline` and `bindlessTextures`. On a device without them the pass is unavailable: it loads, stays disabled, and `set_pass_enabled` or a preset that would turn it on is refused with a warning. Declare such a pass `"enabled": false` and turn it on with a preset, so the pipeline still runs everywhere. `viewport` is a dot-path to a vec4 (x, y, width, height) in fractions of the pass's attachments, resolved every frame: the pass renders into that rectangle only, its clears included, and renders nothing in a frame where it is empty. Such a pass keeps the rest of its outputs, so it is ordered after their earlier writers even when it clears; tiles of a shadow atlas are the use (`scene.shadows.spot[N].rect`). `hasSideEffects` defaults false and prevents culling work whose outputs otherwise appear unused. A transfer pass records a copy or blit with execution type `copy` or `blit` (see [Execution](#execution)), or anything else through a native recording callback.

| `pipeline` key | Default / options |
|---|---|
| `vertexShader`, `fragmentShader`, `computeShader` | SPIR-V paths, default empty. A relative path that names a file beside the pipeline JSON loads that file, so a pipeline and its shaders work from any working directory; otherwise it is relative to the working directory. A graphics pass with a vertex shader and no fragment shader is depth only |
| `geometryShader` | SPIR-V path, default empty; a geometry stage after the vertex (or tessellation) stage of a graphics pass. On a device without the `geometryShader` feature the pass fails to load unless it `requires` `geometryShader` |
| `tessControlShader`, `tessEvaluationShader`, `patchControlPoints` | SPIR-V paths, default empty, given together, with `"topology": "patch_list"` and `patchControlPoints` from 1 to 32 vertices per patch. On a device without the `tessellationShader` feature the pass fails to load unless it `requires` `tessellationShader` |
| `meshShader`, `taskShader` | SPIR-V paths, default empty. A `meshShader`, with an optional `taskShader` before it, takes the place of the vertex, tessellation and geometry shaders of a graphics pass: the pipeline has no vertex input, and the pass's execution type is `draw_mesh_tasks`. They need `VK_EXT_mesh_shader`; on a device without mesh (or task) shaders the pass fails to load unless it `requires` `meshShader` (or `taskShader`). Compile them for SPIR-V 1.4 or later, with `// glslc: --target-env=vulkan1.3` on the shader's second line |
| `vertexInput` | `default` (the standard [mesh layout](#vertex-formats)); select a registered matching format |
| `depthTest`, `depthWrite` | true |
| `depthCompareOp` | `less`; `never`, `equal`, `less_or_equal`, `greater`, `not_equal`, `greater_or_equal`, `always`. An unknown name fails at load |
| `depthClamp` | false; clamps depth instead of clipping at the near and far planes (directional shadow passes). Requires the `depthClamp` device feature and is ignored with a warning without it |
| `cullMode` | `back`; `front`, `none`, `front_and_back`, or `material` for entity passes: each draw culls the back faces of single-sided materials and none of double-sided ones (glTF `doubleSided`), and treats clockwise triangles as front faces where the entity's world matrix mirrors (negative determinant); cull mode and front face are then dynamic state |
| `frontFace` | `counter_clockwise`; `clockwise` |
| `blending` | `none`; `alpha`, `additive`, `custom`; see [blending](#blending) |
| `writeMask` | `rgba`; the channels written to colour attachments, any of `r`, `g`, `b` and `a` once each (`""` writes none) |
| `blendConstants` | absent = 0; `[r, g, b, a]`, or a dot-path to a vec4 resolved every frame (0 while it has no value), for the `constant_color` and `constant_alpha` factors |
| `topology` | `triangle_list`; `triangle_strip`, `line_list`, `line_strip`, `point_list`, `patch_list` (tessellation only) |
| `primitiveRestart` | false; in indexed draws of a strip topology, an index of all ones starts a new strip |
| `lineWidth` | 1; widths above 1 use the `wideLines` device feature |
| `wireframe` | false; requires device support |
| `depthBounds` | absent = off; `[min, max]`, drawing only where the stored depth lies between them. Requires the `depthBounds` device feature and is ignored with a warning without it |
| `depthBias` | absent = off; object `{constant, slope, clamp}`, each default 0. A non-zero `clamp` requires the `depthBiasClamp` device feature and is ignored with a warning without it |
| `stencil` | absent = off; the stencil test, below |

`stencil` turns the stencil test on. Its keys apply to both faces: `compareOp` (`always`; the names of `depthCompareOp`), `passOp`, `failOp` and `depthFailOp` (each `keep`; also `zero`, `replace`, `increment_and_clamp`, `decrement_and_clamp`, `invert`, `increment_and_wrap`, `decrement_and_wrap`), and `reference`, `compareMask` and `writeMask` (0, 255 and 255; integers from 0 to 255). `back`, an object with the same keys, overrides them for back faces; keys it leaves out keep the shared values. An unknown name or a value out of range fails at load. The pass needs a depth attachment whose format has stencil (`D16_UNORM_S8_UINT`, `D24_UNORM_S8_UINT` or `D32_SFLOAT_S8_UINT`), and one with usage `depth_write` or `depth_stencil_write` if its operations change stencil; otherwise compiling the graph fails. A depth attachment with a stencil format is the pass's stencil attachment as well: it is cleared to the `stencil` of its `clear`, loaded and stored with its depth, and every pipeline drawn into it declares its stencil format. In this example, with the passes' other keys left out, `Mark` writes 1 wherever it draws and `Tint` then draws only there:

```json
{ "name": "Mark",
  "outputs": [{ "resource": "maskDepth", "usage": "depth_stencil_write", "clear": { "depth": 1.0, "stencil": 0 } }],
  "pipeline": { "stencil": { "passOp": "replace", "reference": 1 } } },
{ "name": "Tint",
  "outputs": [{ "resource": "hdrColor", "usage": "color_blend" },
              { "resource": "maskDepth", "usage": "depth_read" }],
  "pipeline": { "depthTest": false, "depthWrite": false,
                "stencil": { "compareOp": "equal", "reference": 1 } } }
```

### Blending

`alpha` blends colour by source alpha (`src_alpha`, `one_minus_src_alpha`) and writes the source alpha; `additive` adds the colour (`one`, `one`) and writes the source alpha. For `custom` blending: `srcColorFactor=src_alpha`, `dstColorFactor=one_minus_src_alpha`, `colorBlendOp=add`, `srcAlphaFactor=one`, `dstAlphaFactor=zero`, `alphaBlendOp=add`. Operations are `add`, `subtract`, `reverse_subtract`, `min`, `max`. Factors are `zero`, `one`, `src_color`, `one_minus_src_color`, `dst_color`, `one_minus_dst_color`, `src_alpha`, `one_minus_src_alpha`, `dst_alpha`, `one_minus_dst_alpha`, `constant_color`, `one_minus_constant_color`, `constant_alpha`, `one_minus_constant_alpha` and `src_alpha_saturate`; the constants are `blendConstants`. An unknown name for blending, a factor, an operation, `cullMode`, `frontFace` or `topology` fails at load.

The pipeline's `blending` and `writeMask` apply to every colour attachment. A colour output can set its own `blending`, with the custom keys beside it when it is `custom`, and its own `writeMask`; the others keep the pipeline's. Different blending per attachment needs the `independentBlend` device feature, and compiling fails without it. Here the light is added onto `hdrColor` while `shadowMask` is written as it is:

```json
"outputs": [{ "resource": "hdrColor", "usage": "color_blend" },
            { "resource": "shadowMask", "usage": "color_write", "blending": "none", "writeMask": "rg" }],
"pipeline": { "blending": "additive" }
```

A pass whose graphics or compute pipeline cannot be created (a missing or invalid shader, for example) makes compiling fail with the reason. A graphics pass whose push constants exceed the device's limit still loads, and draws nothing.

`pushConstants` accepts one object or an array. Each range requires `size`; `offset` defaults 0, `stages` selects visibility. Optional `bindings` map named graph parameters using `name`, `offset` (0), and `type` (`float`). A binding named `pass.repeatIndex`, `pass.repeatCount`, `pass.extent` or `pass.texelSize` pushes that value of the pass instead, which is how a fullscreen or compute pass learns its mip level. These graph parameters are distinct from per-entity layout sources.

### Ray tracing passes

A pass with `"type": "ray_tracing"` runs a ray tracing pipeline (`VK_KHR_ray_tracing_pipeline`) with execution type `trace_rays`. Its `pipeline` takes:

| `pipeline` key | Default / options |
|---|---|
| `rayGenShader` | Required; the ray generation shader |
| `missShaders` | `[]`; miss shaders, numbered in order for `traceRayEXT`'s `missIndex` |
| `hitGroups` | `[]`; objects with `closestHit`, `anyHit` and `intersection` (at least one; with `intersection` the group is procedural), numbered in order |
| `maxRecursionDepth` | 1; how deep `traceRayEXT` may be called from hit and miss shaders. Above the device's limit, compiling fails |

No other shader keys apply. The pipeline's shader binding table holds one record per group: the ray generation shader, the miss shaders, then the hit groups, each record the group's handle and no data. Every instance of the scene's [acceleration structure](#descriptors-and-samplers) has an instance offset of 0, so a `traceRayEXT` call chooses its hit group with `sbtRecordOffset` (and `sbtRecordStride`, the number of hit groups per ray type) and its miss shader with `missIndex`. Hit shaders find the surface they hit as ray queries do: `gl_InstanceID` is its row of `rayInstances`, and its textures are in `sceneTextures`. The pass binds its descriptor sets and push constants at the ray tracing bind point, and its image and buffer accesses wait at the ray tracing shader stage. Compile its shaders for Vulkan 1.2 or later (`// glslc: --target-env=vulkan1.2` on the second line); `sk.shaders.compile_dir` compiles `.rgen`, `.rmiss`, `.rchit`, `.rahit` and `.rint` files.

Ray tracing pipelines need a device with ray queries and `VK_KHR_ray_tracing_pipeline`; on a device without them a `ray_tracing` pass fails to load unless it `requires` `rayTracingPipeline`.

```json
{ "name": "Trace", "type": "ray_tracing", "requires": ["rayTracingPipeline"], "enabled": false,
  "outputs": [{ "resource": "radiance", "usage": "storage_image_write" }],
  "descriptorSets": ["traceSet"],
  "pipeline": { "rayGenShader": "shaders/trace.rgen.spv",
                "missShaders": ["shaders/sky.rmiss.spv", "shaders/shadow.rmiss.spv"],
                "hitGroups": [{ "closestHit": "shaders/surface.rchit.spv" }] },
  "execution": { "type": "trace_rays",
                 "dispatch": { "x": { "resource": "radiance", "dimension": "width" },
                               "y": { "resource": "radiance", "dimension": "height" } } } }
```

## Repeated passes

`"repeat": { "count": N, "index": "name", "first": 0, "step": 1 }` on a pass declares N passes at once. Each instance substitutes its index value (`first`, `first + step`, ...; a `step` of -1 counts down, as an upsampling chain that starts from the smallest mip needs, and every value must stay at least 0) for `{name}`, `{name+K}` and `{name-K}` in every string of the pass; a string that is only a placeholder becomes a number. An instance is named by substituting into `name` when it contains the placeholder, and `Name[value]` otherwise. `index` defaults to `i`. Four shadow cascades:

```json
{ "name": "SunShadow", "type": "graphics",
  "repeat": { "count": 4, "index": "cascade" },
  "execution": { "type": "shadow_casters", "entityDataBinding": "shadowCaster" },
  "outputs": [{ "resource": "sunShadow", "usage": "depth_write", "layer": "{cascade}",
                "clear": { "depth": 1.0 } }],
  "pipeline": { "vertexShader": "shaders/shadow.vert.spv", "depthClamp": true },
  "descriptorSets": ["cascadeSet"],
  "pushConstants": [{ "stages": ["vertex"], "size": 68 }] }
```

with the per-draw layout reading which cascade it is drawing:

```json
{ "ShadowPerDraw": { "usage": "push_constant", "packing": "scalar", "fields": [
    { "name": "model",   "type": "mat4", "source": "entity.transform.worldMatrix" },
    { "name": "cascade", "type": "uint", "source": "pass.repeatIndex" } ] } }
```

Descriptor set layouts take the same `repeat`, for passes that bind a different part of an image each; the layout name must contain the placeholder:

```json
{ "downsample{m}": { "repeat": { "count": 4, "index": "m", "first": 1 },
    "bindings": [{ "binding": 0, "type": "combined_image_sampler", "name": "src",
                   "autoBindResource": "bloomChain", "autoBindSampler": "linearClamp", "mip": "{m-1}" }] } }
```

`set_pass_enabled` / `setPassEnabled` with the declared name (`"SunShadow"`) switches every instance, and `is_pass_enabled` on it is true when all of them are enabled. Repetition happens at load: `saveGraphToJson` writes the expanded passes. A `count` is a fixed number; it cannot come from a graph parameter yet.

## Execution

`execution.type` defaults `none`; `bindPipeline` and `bindDescriptorSets` default true. A type not in the table below fails at load.

| Type | Work |
|---|---|
| `fullscreen` | Draw a three-vertex fullscreen triangle |
| `draw` | Non-indexed draw with vertex/instance counts, optionally from a `vertexBuffer` |
| `draw_indexed` | Indexed draw (`vkCmdDrawIndexed`) from an `indexBuffer`, optionally with a `vertexBuffer` |
| `compute_dispatch` | Explicit or parameter/resource-based group counts |
| `compute_image` | Group counts from render extent and `workgroupSize` |
| `dispatch_indirect` | A compute dispatch whose group counts a storage buffer holds (`vkCmdDispatchIndirect`) |
| `draw_indirect` | Non-indexed draws whose arguments, and optionally their number, a storage buffer holds (`vkCmdDrawIndirect`, `vkCmdDrawIndirectCount`) |
| `draw_indexed_indirect` | The same for indexed draws from an `indexBuffer` (`vkCmdDrawIndexedIndirect`, `vkCmdDrawIndexedIndirectCount`) |
| `scene_geometry`, `opaque_geometry`, `transparent_geometry` | Registered scene/entity rendering |
| `shadow_casters` | Static entities with `castShadows`, opaque or masked; requires the rest of a shadow pipeline |
| `skinned_geometry`, `skinned_transparent` | Skinned rendering with matching layouts/shaders |
| `skinned_shadow_casters` | Skinned entities with `castShadows`, opaque or masked; needs a skinned vertex shader and `skeleton` binding |
| `sprite_geometry` | Sprite and glyph rendering |
| `ui_canvas` | Screen canvases of the canvas UI, in ascending `sortOrder`. Needs exactly one colour output, usually the swapchain with `color_blend`, and no `pipeline` block: the engine supplies the pipeline from `shaders/ui/`, looking beside the JSON, then in the working directory, then beside the default pipeline |
| `draw_mesh_tasks` | A pass with a `meshShader`: `vkCmdDrawMeshTasksEXT` with the group counts of `dispatch.x/y/z`, given as for `compute_dispatch` |
| `trace_rays` | A `ray_tracing` pass: `vkCmdTraceRaysKHR` with the ray counts of `dispatch.x/y/z`, given as for `compute_dispatch` (one ray per pixel: the image's `width` and `height` with divisor 1) |
| `copy` | A transfer pass: copy its `transfer_src` input into its `transfer_dst` output (`vkCmdCopyImage`) |
| `blit` | A transfer pass: the same, scaled to the output's size with `filter` (`vkCmdBlitImage`) |
| `none`, `manual` | Native callback/manual recording use |

A `copy` or `blit` pass has `"type": "transfer"`, exactly one input with usage `transfer_src` and one output with usage `transfer_dst`, no `pipeline`, and both images created by the graph (not imported). Each access may name mips and layers as any access does (`mip`/`mips`, `layer`/`layers`; the whole image by default), and both must name the same number of each: the first mip named is copied to the first mip named, and so on. The input and output may be one image, with disjoint mips or layers. A `copy` needs both images in one format and each pair of mips the same size. A `blit` scales each mip to the other's size, with `"filter": "linear"` (the default) or `"nearest"`; whether a format can be blitted, or blitted linearly, depends on the device. The barriers come from the usages, as for any pass.

```json
{ "name": "TAAHistory", "type": "transfer",
  "inputs":  [{ "resource": "taaColor",   "usage": "transfer_src" }],
  "outputs": [{ "resource": "taaHistory", "usage": "transfer_dst" }],
  "execution": { "type": "copy" } }
```

Draw fields: `vertexCount` (a count, as below; 0 when absent), `instanceCount` (1), `firstVertex` (0), `firstInstance` (0). `draw_indexed` takes `indexCount` (a count; 0 when absent), `firstIndex` (0), `vertexOffset` (0, added to each index), `instanceCount` and `firstInstance`.

The draw types (`draw`, `draw_indexed`, `draw_indirect`, `draw_indexed_indirect`) of a graphics pass can draw from buffers: `"vertexBuffer": { "buffer": "Particles", "offset": 0 }` is bound at binding 0, and the indexed types need `"indexBuffer": { "buffer": "Indices", "offset": 0, "type": "uint32" }` (`type` is `uint32` or `uint16`); other types take neither. Each names a storage buffer of `bufferLayouts`, or one the application registered with vertex or index buffer usage, and offsets are multiples of 4. Every storage buffer of `bufferLayouts` can serve. The pipeline's `vertexInput` names the [vertex format](#vertex-formats) the buffer holds. The pass is ordered after the last earlier-declared pass that writes either buffer, as for indirect arguments below, and waits for those writes at vertex input. A `bufferLayouts` buffer named must be a storage buffer, or compilation fails.

Compute dispatch uses `dispatch.x/y/z`. Each of them, and a draw's `vertexCount`, is a count: a number, `{ "parameter": "count", "divisor": 1 }` (a graph parameter), or `{ "resource": "imageName", "dimension": "width", "divisor": 16 }` with `dimension` one of `width`, `height`, `depth` (a 3d image's slices, 1 for other images) and `layers` (array layers). Division rounds up; `divisor` defaults 1 and must be at least 1. A resource that is not declared, or another dimension, fails at load. Use positive workgroup sizes; JSON does not alter GLSL `local_size`. `compute_image` uses `workgroupSize` to derive x/y groups from the extent of the first image the pass writes (or, with none, reads) at that access's mip level, and z groups from its depth, which is 1 unless it is a 3d image.

Indirect execution reads its arguments from a storage buffer that an earlier pass writes on the GPU, so the work can follow a count the CPU never sees:

```json
[ { "type": "dispatch_indirect", "arguments": { "buffer": "DispatchArgs", "offset": 0 } },
  { "type": "draw_indirect", "arguments": { "buffer": "DrawArgs", "offset": 0, "drawCount": 4, "stride": 16 } },
  { "type": "draw_indirect", "arguments": { "buffer": "DrawArgs", "offset": 16 },
    "count": { "buffer": "DrawArgs", "offset": 0, "maxDrawCount": 1024 } } ]
```

Three `execution` objects: a dispatch, four draws, and up to 1024 draws whose number the buffer holds.

- **Arguments:** `arguments.buffer` names a storage buffer of `bufferLayouts`, or one the application registered. At `offset` (a multiple of 4) a `dispatch_indirect` finds a `VkDispatchIndirectCommand` (x, y, z: 3 uints), a `draw_indirect` `drawCount` `VkDrawIndirectCommand`s (vertexCount, instanceCount, firstVertex, firstInstance) `stride` bytes apart (default 16, at least 16), and a `draw_indexed_indirect` `drawCount` `VkDrawIndexedIndirectCommand`s (indexCount, instanceCount, firstIndex, vertexOffset, firstInstance) `stride` bytes apart (default 20, at least 20). A `dispatch_indirect` must be a compute pass, the draw types graphics passes.
- **Count:** with `count`, the number of draws is the uint at `count.offset` in `count.buffer`, at most `maxDrawCount`. That needs the device feature `drawIndirectCount`; declare `"requires": ["drawIndirectCount"]` to leave the pass out where it is missing, since a pass with a count on such a device fails at load. More than one draw per call needs the device feature `multiDrawIndirect`; without it such a pass fails at load too.
- **Checks:** each `bufferLayouts` buffer named must be a storage buffer large enough for the arguments from its offset (the last of `drawCount` or `maxDrawCount` draws, and the count), or compilation fails. Every storage buffer of `bufferLayouts` can hold indirect arguments; one the application registers must be created with `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`. Group counts and draw arguments are not checked: the GPU writes them, within the device's `maxComputeWorkGroupCount`.
- **Order:** the pass gets a barrier after the writes to its arguments (`DRAW_INDIRECT`, `INDIRECT_COMMAND_READ`), and depends on the last earlier-declared pass that writes the buffer, through a storage binding with `autoBindBuffer` whose shader block is not `readonly`. That pass is then ordered first and not culled. Declare it before the indirect pass.
- **Stats:** render stats count an indirect dispatch as one dispatch and an indirect draw as `drawCount` draws (one with a count buffer), without vertices: the CPU never sees how many there were.

Geometry execution also accepts `entityDataBinding`, `sortMode` (e.g. `front_to_back`, `back_to_front`, `sort_key`; entities that sort alike keep their order), `renderLayerMask` (default all bits), `alphaFilter`, `view`, `occlusion`, and `lightIndex` (-1). `alphaFilter` is `any` (default), `opaque` or `mask`, and narrows the pass to materials with that alpha mode, so opaque and alpha-tested geometry can use separate pipelines (e.g. a depth-only shadow pass and one that samples albedo and discards). It is rejected at load on transparent and sprite types, where it could only match nothing. Entity masks are eight bits. The default facade application uses single-queue execution; native multi-queue recording/submission must be integrated explicitly for asynchronous compute.

`view` sets what the pass culls entities against and measures sort distances in: `camera` (the main camera's frustum), `cameras.<name>` (the frustum of the camera entity with that name; the pass draws nothing while there is none), `none`, `shadows.sun.cascades[N]` (sun cascade N's light volume, with no near plane so casters between the sun and the cascade still draw; sorting is by depth along the sun), `shadows.spot[N]` (spot shadow slot N's cone) or `shadows.point.faces[N]` (face N % 6 of point shadow slot N / 6). A spot or point view whose slot no light has this frame draws nothing. It defaults to `camera`, except for shadow casters and sprites, which default to `none`: a caster outside the camera's view can still shadow what is in it. A repeated shadow pass culls each cascade with `"view": "shadows.sun.cascades[{cascade}]"`. Entities are tested by the bounds of their mesh (glTF meshes have them; meshes made in code without `hasBounds` are never culled), and skinned entities are never culled, since animation can leave their bind-pose bounds. `get_pass_draw_stats(name)` / `getPassDrawnCount` and `getPassCulledCount` report what a pass drew and culled when it last ran.

`"occlusion"` (geometry execution types only) names an `R32_SFLOAT` image resource with a `readback`, each texel the farthest depth (0 near, 1 far) of the main camera's view over its block of the screen. A pass that culls against the main camera leaves out the entities whose bounds lie wholly behind it, tested with the camera of the frame that rendered it: the readback the CPU last received, as many frames old as there are frames in flight. Skinned entities, entities whose world matrix changed since the frame before, and boxes reaching in front of the near plane are always drawn. Nothing is left out while the camera has turned by more than 25 degrees or moved by more than a tenth of its far plane since that frame, or when no readback newer than 8 frames has arrived. `set_occlusion_culling(False)` (C++ `EngineAPI::setOcclusionCulling`) turns it off; `get_pass_occluded_count` reports what a pass left out. Loading fails when the image is not such a resource.

`"instancing": true` (geometry execution types only; rejected on others) draws the pass's entities that share a mesh and a material as one instanced draw. Two entities share a draw when their vertex and index buffers, vertex and index counts, stride, index type and material (parameters, texture views and samplers, alpha mode and cutoff, sidedness) are the same; skinned, morphed and sprite entities draw alone. The pipeline gets a second vertex buffer at binding 1, one row of 128 bytes per instance, read as eight `vec4` attributes from location 8: the instance's model matrix (locations 8 to 11) and last frame's (12 to 15), so a vertex shader declares `layout(location = 8) in mat4 instanceModel;` and, for motion vectors, `layout(location = 12) in mat4 instancePrevModel;`. The per-draw push constants are the first entity's. With `sortMode` `back_to_front` only neighbouring entities share a draw; otherwise each entity joins the draw of the first one like it, and draws keep the order of their first entities. The pass's drawn count (`get_pass_draw_stats`) counts draws.

## Presets

A preset switches passes and sets `scene.custom` values in one call, `apply_pipeline_preset(name)` (C++ `EngineAPI::applyPipelinePreset` or `RenderGraph::applyPreset`):

```json
{ "presets": {
    "low":  { "passes": { "GTAO": false, "ShadowMasked{cascade}": false },
              "values": { "default.contactShadowLength": 0, "default.bloomIntensity": 0 } },
    "high": { "passes": { "GTAO": true, "ShadowMasked{cascade}": true },
              "values": { "default.contactShadowLength": null, "default.bloomIntensity": null } } } }
```

`passes` maps pass names, or the declared name of a repeated pass, to `true` or `false`; a name that is not a pass fails at load. `values` maps `scene.custom` keys, without the prefix, to a number or an array of numbers, each set with the type of the buffer-layout field whose `source` reads it (so `0` above lands as a float), or as a float or vector when no field does. `null` removes the value so the field's `default` applies again, which is how a preset returns to the pipeline's defaults without repeating them. A preset that turns on a pass the device cannot run is refused whole, with a warning. Called before the pipeline is loaded, from `on_init`, the preset is applied once it is; an unknown name is then a warning at load.

## Canvas UI depth

```json
{ "ui": { "depth": "gDepth", "afterPass": "GBufferSkinnedMasked" } }
```

`depth` names an image resource and `afterPass` a pass that writes it; anything else fails at load. The image is created with transfer-source usage. Each frame the engine copies the texel under the canvas pointer right after that pass (`RenderGraph::requestPixelReadback`), and the canvas UI ignores world-canvas hits behind the depth it read; the value arrives as many frames later as there are frames in flight. Without the block, world-canvas hits are not tested against the scene. See the [canvas UI guide](../guides/canvas-ui.md#geometry-in-front-of-world-canvases).

## Initialization, memory, and readback

Layout `source` accepts `type` (`initializer` by default), `seed` (42), and per-field initializers under `fields`: `constant`, `randomRange` (`min`/`max`), `gaussian` (`mean`/`stddev`), `grid` (`dimensions`/`origin`/`spacing`/`w`), or `sphere` (`center`/`radius`/`mode`/`w`). These initialize numeric components, not arbitrary structs. `type: file` uses a binary `path`, exactly `elementCount` elements long, which resolves beside the pipeline JSON when the file is there, as shader paths do, and against the working directory otherwise; `type: buffer_ref` references a shared `ref` with `frequency` (default `per_frame`).

`type: scene` with `"data": "lights"` makes a storage buffer the engine refills from the scene every frame: every light, in the order and form of `scene.lights[N]`, as many as `elementCount` holds (the rest are left out, with a warning). Its fields must be the six `vec4`s `positionType`, `colorIntensity`, `directionRange`, `attenuation`, `source` and `shape`, or compilation fails. It is one host-visible buffer per frame in flight, written after that frame's fence, so it needs no barrier. `scene.lights.count` in a uniform buffer says how many there are; a shader declares the buffer `readonly` with a runtime array and reads `min(count, array length)` of them:

```json
{ "SceneLights": { "usage": "storage_buffer", "packing": "std430", "elementCount": 8192,
    "fields": [ { "name": "positionType", "type": "vec4" }, { "name": "colorIntensity", "type": "vec4" },
                { "name": "directionRange", "type": "vec4" }, { "name": "attenuation", "type": "vec4" },
                { "name": "source", "type": "vec4" }, { "name": "shape", "type": "vec4" } ],
    "source": { "type": "scene", "data": "lights" } } }
```

`type: scene` with `"data": "rayInstances"` holds a row for each instance of the scene's [acceleration structure](#descriptors-and-samplers), in the structure's order. A ray query that hits instance `rayQueryGetIntersectionInstanceIdEXT` reads that row for the surface it hit. The rows are written with the structure, so a pipeline with such a buffer also needs an `acceleration_structure` binding. As for lights, there is one host-visible buffer per frame in flight, holding as many rows as `elementCount` (the rest are left out, with a warning).

Its fields must be, in this order:

| Field | Type | Holds |
|---|---|---|
| `geometry` | `uvec4` | The device addresses of the vertex buffer (x low, y high bits) and the index buffer (z, w; zero when the mesh is not indexed) |
| `vertexLayout` | `uvec4` | x: the vertex stride in bytes; y: the index type (0 16-bit, 1 32-bit, 2 not indexed); z: byte offsets in the vertex (normal in bits 0-7, uv in 8-15, colour in 16-23); w: flags (1 double-sided) |
| `baseColor` | `vec4` | `baseColorFactor` |
| `emissive` | `vec4` | `emissiveFactor` in rgb |
| `surface` | `vec4` | `metallicFactor`, `roughnessFactor`, `alphaCutoff`, and the entity id's bits in w |
| `textures` | `uvec4` | The slots of `albedoMap`, `normalMap`, `metallicRoughnessMap` and `emissiveMap` in the `sceneTextures` binding; `0xFFFFFFFF` for none |
| `optics` | `vec4` | `transmissionFactor` (`KHR_materials_transmission`, 0 without), `ior` (`KHR_materials_ior`, 1.5 without), `iridescenceFactor` and `iridescenceIor` (`KHR_materials_iridescence`; 0 and 1.3 without) |
| `film` | `vec4` | The iridescence's minimum and maximum thickness in nanometres (100 and 400 without), and the bits of the slots of `iridescenceThicknessMap` (z) and `iridescenceMap` (w); `0xFFFFFFFF` for none |

Otherwise compilation fails. The GLSL library's [`sk/scene_hits.glsl`](../../python/shoonyakasha/glsl/sk/scene_hits.glsl) declares the buffer and the texture array, and turns a committed hit into its position, normals, uv, material, transmission, index of refraction and thin film:

```json
{ "RayInstances": { "usage": "storage_buffer", "packing": "std430", "elementCount": 16384,
    "fields": [ { "name": "geometry", "type": "uvec4" }, { "name": "vertexLayout", "type": "uvec4" },
                { "name": "baseColor", "type": "vec4" }, { "name": "emissive", "type": "vec4" },
                { "name": "surface", "type": "vec4" }, { "name": "textures", "type": "uvec4" },
                { "name": "optics", "type": "vec4" }, { "name": "film", "type": "vec4" } ],
    "source": { "type": "scene", "data": "rayInstances" } } }
```

`memory` selects `location` (`device_local`, `host_visible`, `host_coherent`), `staging` (`auto`, `persistent`, `none`), and `transferDirection` (`gpu_only`, `cpu_to_gpu`, `gpu_to_cpu`, `bidirectional`). Defaults are the first value in each list.

Layout `target` is a string or an object with `name`, optional `readback`, and optional `save`. Readback has `frequency` (`manual`, `per_frame`, `every_n_frames`, `once`), `n` (1), `callback` (false), and `ringDepth` (0). Save has `path`, `trigger` (`manual`, `every_n_frames`, `on_readback`), `n` (1), and `autoCreateDirectories` (true). A save policy can enable readback automatically; explicit memory configuration is parsed afterwards and can override its transfer direction.

Image resources also accept `target`, `readback`, and `save`; resource-level policies take precedence over policies nested in the target object. For complete layouts, resource declarations, and native callback/trigger setup, see [compute/data flow](../guides/compute-and-data-flow.md).

## Validation and export

`sk.pipeline.validate(path)` returns diagnostic objects. `check(path, warnings_are_errors=False)` raises ValueError on errors and otherwise returns the diagnostic list; it does **not** return a success boolean. Neither verifies shader interfaces or device features.

The native compiler does verify shader interfaces, at startup. After compiling buffer layouts it reflects every pass's SPIR-V (with SPIRV-Reflect) and compares it with the JSON:

- every descriptor the shader uses must be declared in the pass's `descriptorSets` at the same set index and binding, with the same descriptor type;
- for a uniform or storage buffer bound through `autoBindBuffer`, for a material buffer (`"source": "material"`), and for the push constants of the pass's `entityDataBinding` `perDraw` layout, each block member's offset, base type, vector size, matrix stride, array length and array stride must match a field of the layout. Names are not compared. A shader may declare fewer members than the layout;
- push constants must fit inside the pass's `pushConstants` ranges.

Blocks ending in a runtime array, and arrays of structs, are not compared member by member. A mismatch fails compilation with one line per problem, naming the pass, the shader file and the member, so `Engine(...)` raises instead of rendering from misread bytes.

When the engine extension is installed, `validate` also loads the pipeline with the engine's own loader, for a device with every capability, and reports its error as one more problem. Shader interfaces are checked only when the engine compiles the graph.

The loader does not enforce `version` 2/3. Builder serialization currently writes version 1 and omits declarations such as buffer layouts/entity bindings and some execution/data-flow information. Diagnostic graph export is a separate representation. Neither is a safe substitute for keeping authored pipeline JSON under version control.
