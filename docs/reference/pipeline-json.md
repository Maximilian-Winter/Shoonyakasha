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

These sections are conditionally parsed. Their absence is not a useful runnable pipeline: normal windowed applications need resources, a rendering pass, and a final present output. `standardBuffers` is obsolete. Unknown keys are not comprehensively rejected; accepted JSON is not proof a key is implemented.

## Resources and accesses

Each resource requires `name` and `kind` (`image` or `buffer`). `imported` defaults false. Imported resources need native backing; ApplicationBase supplies the imported `swapchain` image.

| Image key | Default | Meaning |
|---|---|---|
| `format` | native descriptor default if omitted | Use an explicit format for created targets |
| `width`, `height` | 0 | Reference-size dimensions when zero |
| `widthScale`, `heightScale` | 1.0 | Scale reference-size dimensions, e.g. 0.5 for bloom |
| `mipLevels` | 1 | Mip levels, or `"full"` for a complete chain at the resolved size |
| `arrayLayers` | 1 (6 for `cube`) | Array layers, e.g. one per shadow cascade |
| `viewType` | `auto` | How shaders sampling the whole image see it: `auto` (`2d` for one layer, `2d_array` for more), `2d`, `2d_array`, `cube` (6 square layers), `cube_array` (a multiple of 6) |
| `samples` | 1 | Vulkan sample count; must match the rendering configuration |
| `transient` | false | Transient image declaration |

Image properties live inside `image`. Buffer properties live inside `buffer`: `size` in bytes (default 0), `persistentlyMapped` (false). Creating buffers through `bufferLayouts` is separate from declaring graph resource accesses; follow the SSBO examples for imported layout-backed buffers.

Each pass input/output has a required `resource` name and `usage`. Optional `clear` is `[r,g,b,a]` or `{ "depth": 1.0, "stencil": 0 }`. Optional `present` marks the post-render presentation transition.

An access covers the whole image unless it names a part: `"mip": n` or `"mips": [first, count]`, and `"layer": n` or `"layers": [first, count]`. An attachment names one mip level; one layer renders into that layer, several render layered. Ranges outside the image, and one pass using the same mip and layer in two different layouts, fail compilation. Descriptor bindings with `autoBindResource` accept the same keys and bind a view of just that part, so a downsample pass can sample mip n-1 while rendering mip n:

```json
{ "name": "Downsample2",
  "inputs":  [{ "resource": "bloomChain", "usage": "shader_read", "mip": 1 }],
  "outputs": [{ "resource": "bloomChain", "usage": "color_write", "mip": 2 }] }
```

Dependencies and barriers are tracked per mip and layer: passes writing different layers of one image (shadow cascades) are independent, a pass reading the whole image depends on all of them, and each gets barriers for only its own layers. A write that loads rather than clears (no `clear`) depends on the previous writer.

| Usage | Meaning |
|---|---|
| `color_write`, `color_attachment_write` | Color attachment write |
| `color_blend`, `color_attachment_blend` | Color attachment read/modify/write |
| `depth_write`, `depth_stencil_write` | Depth/stencil write |
| `depth_read` | Read-only depth |
| `shader_read` | Shader read |
| `shader_read_write` | Shader read/write |
| `storage_image_write` | Storage-image write |
| `input_attachment` | Input attachment |
| `transfer_src`, `transfer_dst` | Transfer access |
| `present` | Legacy alias for color write plus presentation |

Prefer `"usage": "color_write", "present": true` on the final swapchain output. For an overlay, use `color_blend` plus `present: true`. Declare all inter-pass accesses; the graph cannot infer hazards from shader code.

## Vertex formats

A named format contains `attributes`, each with `name`, `type`, and `location`. The registry calculates offsets/stride. Match the actual uploaded geometry and GLSL locations; a declaration does not repack a mesh. See [VertexFormatRegistry](../../include/Vulkan/FrameGraph/VertexFormatRegistry.h) and the starter/skinned/sprite pipelines for compatible formats.

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

Scalar types are `float`, `double`, `int`, `uint`, `bool`; vector types are `vec2/3/4`, `ivec2/3/4`, `uvec2/3/4`; matrix types are `mat2/3/4`. `std140`, `std430`, and scalar packing differ in alignment/array stride. The shader block layout and push-constant byte range must agree with the compiled layout; device features and limits still apply.

A field `source` is a dot-path. A layout-level `source` is an initialization object; they have different meanings. Manual/on-change scheduling is native-managed behavior, not automatic Python-object observation.

## Dot-paths

| Root | Supported values |
|---|---|
| `scene.camera` | `view`, `projection`, `viewProjection`, `invView`, `invProj`, `position`, `fov`, `nearPlane`, `farPlane`, `aspect`, `positionVec4`, `nearFarFovAspect` |
| `scene.environment` | `irradianceMap`, `prefilterMap`, `brdfLUT`, `environmentMap` |
| `scene.time` | `elapsed`, `delta`, `frame` |
| `scene.screen` | `width`, `height`, `resolution` |
| `scene.lights` | `count`; indexed `scene.lights[N].positionType`, `colorIntensity`, `directionRange`, `attenuation` |
| `scene.shadows.sun` | `enabled`, `cascadeCount`, `splits`, `texelWorldSize`, `lightIndex`, `direction`; indexed `scene.shadows.sun.cascades[N].viewProj`. See [sun shadow cascades](../guides/lighting-and-ibl.md#sun-shadow-cascades) |
| `scene.custom` | Values explicitly published under a key by the application |
| `entity.transform` | `worldMatrix`, `localMatrix`, `position`, `rotation`, `scale` |
| `entity.material` | `params.<name>`, `textures.<slot>`, `textures.<slot>.exists`, `alphaCutoff`, `alphaMode`, `doubleSided` |
| `entity.mesh` | `vertexCount`, `indexCount` |
| `entity.skeleton` | `hasSkeleton`, `jointCount` |
| `const` | Constant expressions such as `const.0`, `const.1`, `const.1.0.0.1` |
| `pass` | `repeatIndex`, `repeatCount` (see [Repeated passes](#repeated-passes)), `extent` and `texelSize` of the pass being recorded. Push-constant layouts only: a buffer is filled once per frame, so any other layout using `pass.*` fails compilation |

The constant parser uses dots as vector separators: `const.0.5` is not a reliable spelling for scalar 0.5. Publish fractional scalars as custom values. Bare identifiers address resource bindings where supported. For fields with `arrayCount > 1`, a source such as `scene.lights[i].positionType` expands `[i]` for each element. Without `[i]`, the resolved value is broadcast to the array. These paths are implemented cases in [DotPathResolver](../../src/FrameGraph/DotPathResolver.cpp), not reflection over arbitrary C++ or Python fields. Component presence, value type, and initialization matter.

## Descriptors and samplers

Each descriptor-set layout has a `bindings` array. Bindings require `binding` and `type`; optional `count` defaults 1, `name` defaults `binding_<index>`, and `stages` selects shader visibility. Use the descriptor types accepted by [JsonUtils](../../src/Vulkan/FrameGraph/FrameGraphJson.cpp), such as `uniform_buffer`, `storage_buffer`, and `combined_image_sampler`.

`autoBindBuffer` references a named buffer, `autoBindResource` an image/resource source, and `autoBindSampler` a named sampler. Pass `descriptorSets` is an ordered list of layout names; that order supplies shader set indices.

Sampler keys: `magFilter`, `minFilter`, `mipmapMode` default `linear` (also `nearest`). Use `addressMode` for all axes or `addressModeU/V/W` individually (default `repeat`; also `clamp_to_edge`, `clamp_to_border`, `mirrored_repeat`). Other keys are `borderColor` (`float_opaque_black`), `anisotropyEnable` (false; alias `anisotropy`), `maxAnisotropy` (1), `compareEnable` (false), `compareOp` (`less`), `minLod`, `maxLod`, and `mipLodBias` (all 0). Set the LOD range deliberately when sampling mipmapped textures.

## Entity data bindings

Each named binding can contain:

- `perDraw`: `layoutRef` (preferred; legacy `layout`), `method` (`push_constant`), `offset` (0), `size` (64), `stages`, `set` (0), and `binding` (0).
- `material`: `layoutRef`, `method` (`descriptor_set`), `set` (1), and optional texture-name→binding-number `bindings`.
- `skeleton`: `layoutRef` for the skeleton descriptor set.

Use a matching `execution.entityDataBinding`. The parser accepting a method string is not a promise of an arbitrary binding backend; the supplied geometry examples use push constants for per-draw data and descriptor sets for materials/skeletons.

## Passes and pipeline state

Passes require `name` and `type` (`graphics`, `compute`, `transfer`). `queue` defaults `graphics`; `compute` requests the compute queue in a multi-queue execution setup. `enabled` defaults true and can be changed at runtime with `set_pass_enabled` (C++ `setPassEnabled`). A disabled pass draws and dispatches nothing, but its barriers still run and its attachments still begin and end rendering: they are cleared to their `clear` values and end in the layouts later passes expect, so a disabled shadow pass leaves a map cleared to far depth. `hasSideEffects` defaults false and prevents culling work whose outputs otherwise appear unused. A transfer type does not supply a JSON copy/blit command: use native recording callbacks where needed.

| `pipeline` key | Default / options |
|---|---|
| `vertexShader`, `fragmentShader`, `computeShader` | SPIR-V paths, default empty |
| `vertexInput` | `default`; select a registered matching format |
| `depthTest`, `depthWrite` | true |
| `depthCompareOp` | `less`; `never`, `equal`, `less_or_equal`, `greater`, `not_equal`, `greater_or_equal`, `always`. An unknown name fails at load |
| `depthClamp` | false; clamps depth instead of clipping at the near and far planes (directional shadow passes). Requires the `depthClamp` device feature and is ignored with a warning without it |
| `cullMode` | `back`; `front`, `none`, `front_and_back` |
| `blending` | `none`; `alpha`, `additive`, `custom` |
| `topology` | `triangle_list`; `triangle_strip`, `line_list`, `line_strip`, `point_list` |
| `wireframe` | false; requires device support |
| `depthBias` | absent = off; object `{constant, slope, clamp}`, each default 0. A non-zero `clamp` requires the `depthBiasClamp` device feature and is ignored with a warning without it |

For `custom` blending: `srcColorFactor=src_alpha`, `dstColorFactor=one_minus_src_alpha`, `colorBlendOp=add`, `srcAlphaFactor=one`, `dstAlphaFactor=zero`, `alphaBlendOp=add`. Operations are `add`, `subtract`, `reverse_subtract`, `min`, `max`. Factors include zero/one, source/destination color/alpha and their complements, constant color/alpha and complements, and `src_alpha_saturate`; native blend constants need appropriate setup. Unknown blend strings can fall back rather than fail, so use verified spellings.

`pushConstants` accepts one object or an array. Each range requires `size`; `offset` defaults 0, `stages` selects visibility. Optional `bindings` map named graph parameters using `name`, `offset` (0), and `type` (`float`). A binding named `pass.repeatIndex`, `pass.repeatCount`, `pass.extent` or `pass.texelSize` pushes that value of the pass instead, which is how a fullscreen or compute pass learns its mip level. These graph parameters are distinct from per-entity layout sources.

## Repeated passes

`"repeat": { "count": N, "index": "name", "first": 0 }` on a pass declares N passes at once. Each instance substitutes its index value (`first`, `first + 1`, ...) for `{name}`, `{name+K}` and `{name-K}` in every string of the pass; a string that is only a placeholder becomes a number. An instance is named by substituting into `name` when it contains the placeholder, and `Name[value]` otherwise. `index` defaults to `i`. Four shadow cascades:

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

`execution.type` defaults `none`; `bindPipeline` and `bindDescriptorSets` default true.

| Type | Work |
|---|---|
| `fullscreen` | Draw a three-vertex fullscreen triangle |
| `draw` | Non-indexed draw with vertex/instance counts |
| `compute_dispatch` | Explicit or parameter/resource-based group counts |
| `compute_image` | Group counts from render extent and `workgroupSize` |
| `scene_geometry`, `opaque_geometry`, `transparent_geometry` | Registered scene/entity rendering |
| `shadow_casters` | Static entities with `castShadows`, opaque or masked; requires the rest of a shadow pipeline |
| `skinned_geometry`, `skinned_transparent` | Skinned rendering with matching layouts/shaders |
| `skinned_shadow_casters` | Skinned entities with `castShadows`, opaque or masked; needs a skinned vertex shader and `skeleton` binding |
| `sprite_geometry` | Sprite and glyph rendering |
| `none`, `manual` | Native callback/manual recording use |

Draw fields: `vertexCount` (integer, or `{ "parameter": "count", "divisor": 1 }`), `instanceCount` (1), `firstVertex` (0), `firstInstance` (0). The parser also accepts resource/dimension on vertexCount, but the current draw executor implements fixed/parameter counts, not resource-derived vertex counts.

Compute dispatch uses `dispatch.x/y/z`, each a fixed group count, a parameter/divisor object, or `{ "resource": "imageName", "dimension": "width", "divisor": 16 }` (also `height`). Division rounds up. Use positive divisors and workgroup sizes; JSON does not alter GLSL `local_size`. `compute_image` uses `workgroupSize` to derive x/y groups and z=1.

Geometry execution also accepts `entityDataBinding`, `sortMode` (e.g. `front_to_back`, `back_to_front`, `sort_key`), `renderLayerMask` (default all bits), `alphaFilter`, `view`, and `lightIndex` (-1). `alphaFilter` is `any` (default), `opaque` or `mask`, and narrows the pass to materials with that alpha mode, so opaque and alpha-tested geometry can use separate pipelines (e.g. a depth-only shadow pass and one that samples albedo and discards). It is rejected at load on transparent and sprite types, where it could only match nothing. Entity masks are eight bits. The default facade application uses single-queue execution; native multi-queue recording/submission must be integrated explicitly for asynchronous compute.

`view` sets what the pass culls entities against and measures sort distances in: `camera` (the main camera's frustum), `none`, or `shadows.sun.cascades[N]` (sun cascade N's light volume, with no near plane so casters between the sun and the cascade still draw; sorting is by depth along the sun). It defaults to `camera`, except for shadow casters and sprites, which default to `none`: a caster outside the camera's view can still shadow what is in it. A repeated shadow pass culls each cascade with `"view": "shadows.sun.cascades[{cascade}]"`. Entities are tested by the bounds of their mesh (glTF meshes have them; meshes made in code without `hasBounds` are never culled), and skinned entities are never culled, since animation can leave their bind-pose bounds. `get_pass_draw_stats(name)` / `getPassDrawnCount` and `getPassCulledCount` report what a pass drew and culled when it last ran.

## Initialization, memory, and readback

Layout `source` accepts `type` (`initializer` by default), `seed` (42), and per-field initializers under `fields`: `constant`, `randomRange` (`min`/`max`), `gaussian` (`mean`/`stddev`), `grid` (`dimensions`/`origin`/`spacing`/`w`), or `sphere` (`center`/`radius`/`mode`/`w`). These initialize numeric components, not arbitrary structs. `type: file` uses a binary `path`; `type: buffer_ref` references a shared `ref` with `frequency` (default `per_frame`).

`memory` selects `location` (`device_local`, `host_visible`, `host_coherent`), `staging` (`auto`, `persistent`, `none`), and `transferDirection` (`gpu_only`, `cpu_to_gpu`, `gpu_to_cpu`, `bidirectional`). Defaults are the first value in each list.

Layout `target` is a string or an object with `name`, optional `readback`, and optional `save`. Readback has `frequency` (`manual`, `per_frame`, `every_n_frames`, `once`), `n` (1), `callback` (false), and `ringDepth` (0). Save has `path`, `trigger` (`manual`, `every_n_frames`, `on_readback`), `n` (1), and `autoCreateDirectories` (true). A save policy can enable readback automatically; explicit memory configuration is parsed afterwards and can override its transfer direction.

Image resources also accept `target`, `readback`, and `save`; resource-level policies take precedence over policies nested in the target object. For complete layouts, resource declarations, and native callback/trigger setup, see [compute/data flow](../guides/compute-and-data-flow.md).

## Validation and export

`sk.pipeline.validate(path)` returns diagnostic objects. `check(path, warnings_are_errors=False)` raises ValueError on errors and otherwise returns the diagnostic list; it does **not** return a success boolean. Neither verifies shader interfaces, device features, or every parser key.

The native compiler does verify shader interfaces, at startup. After compiling buffer layouts it reflects every pass's SPIR-V (with SPIRV-Reflect) and compares it with the JSON:

- every descriptor the shader uses must be declared in the pass's `descriptorSets` at the same set index and binding, with the same descriptor type;
- for a uniform or storage buffer bound through `autoBindBuffer`, and for the push constants of the pass's `entityDataBinding` `perDraw` layout, each block member's offset, base type, vector size, matrix stride, array length and array stride must match a field of the layout. Names are not compared. A shader may declare fewer members than the layout;
- push constants must fit inside the pass's `pushConstants` ranges.

Blocks ending in a runtime array, and arrays of structs, are not compared member by member. A mismatch fails compilation with one line per problem, naming the pass, the shader file and the member, so `Engine(...)` raises instead of rendering from misread bytes.

Known differences: the Python validator tolerates layout arrays and type strings such as `vec4[16]`, while native declarations use named layout objects and `arrayCount`. Conversely, native `descriptor_set` layout usage is not in the Python validator's buffer-usage vocabulary. Treat disagreements as tooling limitations and check native behavior.

The loader does not enforce `version` 2/3. Builder serialization currently writes version 1 and omits declarations such as buffer layouts/entity bindings and some execution/data-flow information. Diagnostic graph export is a separate representation. Neither is a safe substitute for keeping authored pipeline JSON under version control.
