# Default Render Pipeline and Modern Shadows

A plan for a shipped default 3D pipeline (JSON + shaders) with modern
shadowing, and for the frame graph features it needs first.

## Where the frame graph stands for shadows

What already works and is enough for a single shadow map (the shrine example
uses all of it):

- Depth-only graphics passes with a fixed-size depth resource
  (`"width": 2048, "height": 2048`), with the pass extent taken from it.
- `depthBias` (constant, slope, clamp) in the pipeline block.
- Comparison samplers: `compareEnable` / `compareOp` are parsed
  (`FrameGraphJson.cpp:424`) and applied (`FrameGraphCompiler.cpp:1493`), so
  `sampler2DShadow` with hardware PCF is available today. The shrine does not
  use it yet; it does 25 manual `nearest` taps.
- `entityDataBindings` are generic, so a shadow pass can bind material
  textures for alpha testing without C++ changes.
- `scene.custom.*` dot-paths let the application push matrices.
- Compute passes with storage images, so screen-space passes (contact
  shadows, shadow mask, AO) are expressible.

What blocks cascaded, local-light and "modern" shadows:

| # | Gap | Where | Effect |
|---|-----|-------|--------|
| 1 | `mipLevels` and `arrayLayers` are parsed but ignored. Every graph image is created as a single-mip, single-layer 2D image with one full view. | `FrameGraphJson.cpp:920`, `FrameGraphCompiler.cpp:438` | No shadow-map arrays (CSM), no cube maps (point lights), no mip chains (Hi-Z, bloom). JSON asking for them is silently accepted. |
| 2 | A pass access cannot name a layer or mip, and barriers track whole images. | `ResourceAccess`, `resolveLayoutsAndInsertBarriers` | Can't render cascade *i* into layer *i*, can't read mip *n-1* while writing mip *n*. |
| 3 | No way to stamp out N similar passes. | JSON loader | Four cascades = four copy-pasted passes differing by one number. |
| 4 | A geometry pass has no notion of "which view". The per-draw push constant carries only the model matrix, and the shadow matrix lives in a UBO the app fills. | `FrameGraphRenderer`, shrine | Every cascade/light needs its own UBO, and nothing tells a pass which one it is. |
| 5 | Light-space matrices are computed by the application (`shrine.py:97-121`). `LightComponent::castShadows` and `shadowMapSize` are stored but never read by the renderer. | `ECS/Core.h:158`, `SceneAPI.cpp:462` | Every app re-implements cascade splitting, fitting and texel snapping. |
| 6 | No frustum culling anywhere; one `vkCmdDrawIndexed` per entity. Sort distance is always from the main camera. | `FrameGraphRenderer::queryEntities` | Each cascade draws every caster in the scene. With 4 cascades + local lights this becomes the dominant CPU and vertex cost. |
| 7 | `depthClamp` exists in `PipelineStateBuilder` but isn't in JSON; depth compare op is fixed to `LESS`. | `FrameGraphCompiler.cpp:1334` | No shadow "pancaking" for directional lights, no reverse-Z, no `EQUAL` test after a depth prepass. |
| 8 | `shadow_casters` includes skinned and alpha-masked entities, but there is one vertex shader for all of them and no alpha test. | `FrameGraphRenderer.h:235` | Animated meshes cast bind-pose shadows; foliage casts solid-quad shadows. |
| 9 | `enabled` is parsed from JSON, but can't be changed at runtime, and a disabled pass was skipped together with its barriers. | `FrameGraphExecutor.cpp` | No quality tiers; turning a shadow pass off left later barriers expecting a layout the map was never put in. |
| 10 | No cross-frame (history) resources. | — | No TAA, no temporal shadow/AO filtering. |

Minor: the shrine's `ShadowPass` uses `opaque_geometry`, so it ignores the
per-entity `castShadows` flag. `EntityRenderExecutor` is dead code (its own
comments say so) and could go before this work starts, so there's one draw
path to extend.

## Target: the shipped default pipeline

Rasterized, deferred, one JSON + shaders under e.g.
`assets/pipelines/default/`, using the shared GLSL library
(`python/shoonyakasha/glsl/sk/`). Aimed at "what a current mid-size engine ships",
not at Nanite/VSM scale.

```
 DepthPrepass? ──► GBuffer ──► HiZ (mips) ─┐
                     │                     │
 ShadowCascade[0..3] ┤                     ├─► ContactShadows ─┐
 LocalShadows[...]  ─┤                     ├─► GTAO ──────────┤
                     └────────────► ShadowMask (CSM + contact)◄┘
                                          │
                         Lighting (deferred, IBL, fog) ──► Transparent (forward)
                                          │
                                TAA? ──► Bloom ──► AutoExposure ──► Tonemap ──► swapchain
```

### Shadows in detail

**Sun: cascaded shadow maps**
- 4 cascades in one `D32_SFLOAT` 2D array, 2048² per layer by default
  (1024² low tier, 4096² ultra).
- Split scheme: practical split (blend of log and uniform, λ ≈ 0.75),
  shadow distance a pipeline/scene parameter.
- Stable fitting: bounding sphere per cascade, light-space texel snapping,
  so shadows don't shimmer when the camera rotates or moves.
- `depthClamp` on and the near plane pulled toward the light, so casters
  behind the camera frustum still land in the map without wasting depth
  range.
- Bias: hardware slope bias in the pass + normal offset scaled by texel
  world size per cascade (the shrine already does normal offset; this makes it
  per-cascade).
- Filtering: compare sampler (`sampler2DArrayShadow`), 16-tap Vogel disk
  rotated by interleaved gradient noise, radius scaled per cascade so the
  penumbra looks the same across cascade boundaries. Blend band between
  cascades.
- High tier: PCSS (blocker search + variable kernel) on cascades 0–1 only.
- Alpha-tested casters get their own pipeline variant with `discard`;
  skinned casters use the skinned vertex shader. Both are passes of their own
  or separate entity filters.

**Contact shadows** (compute, full res): a short screen-space ray march
toward the sun through the depth buffer (≈ 16 steps, few decimetres). Fills
in the detail CSM misses at contact points and hides peter-panning from bias.
This is cheap and already expressible with today's compute passes.

**Shadow mask** (compute): resolves CSM × contact shadows into an `R8_UNORM`
screen texture. Lighting reads one texture instead of doing cascade selection
itself, the forward transparent pass keeps sampling CSM directly, and a later
temporal filter slots in here.

**Local lights**
- Spot: perspective map per light, layers of a 2D array (budget e.g. 8).
- Point: cube-array (6 layers per light) or dual-paraboloid; rendered with
  the same repeat mechanism, one pass per face, culled per face.
- Assigned by the engine each frame by importance (distance × intensity ×
  screen size), respecting `LightComponent::castShadows` and a budget.
- A single shadow atlas with per-light rects is the more flexible long-term
  design, but it needs per-pass viewports/scissors into a sub-rect; start with
  arrays.

**Deliberately not in the default**
- Virtual shadow maps: excellent quality but need page tables, GPU-driven
  culling and sparse residency. Revisit once there's indirect drawing.
- VSM/EVSM/MSM: filterable, but light leaking; PCF with good bias is the
  common choice again.
- Ray-traced shadows (`VK_KHR_ray_query`): a good optional "ultra" tier for
  the sun later, feeding the same shadow mask.

### Rest of the default pipeline

- **GBuffer**: drop `gPosition` (RGBA16F) and reconstruct position from
  depth. Octahedral normals (`RG16_SNORM` or `A2B10G10R10`), `R8G8B8A8_SRGB`
  albedo, `RG8` or `RGBA8` for metal/rough/AO, `RG16F` motion vectors (needed
  for TAA).
- **GTAO**: half-res compute + bilateral upsample, multiplies IBL only.
- **Lighting**: fullscreen/compute deferred as now. Clustered light culling
  (compute building per-cluster light lists) once more than the 16 fixed
  lights are wanted.
- **Transparent**: forward pass sampling the CSM array.
- **Bloom**: downsample/upsample mip chain (Jimenez, CoD:AW) — needs mips.
- **Exposure**: histogram compute → 1×1 exposure buffer.
- **Tonemap**: AgX or Khronos PBR Neutral, with the existing tonemap
  library as the home for it.
- **TAA**: optional, needs history resources.

## JSON sketch for the cascade passes

What the target JSON should look like, to define the features:

```jsonc
"resources": [
  { "name": "sunShadow", "kind": "image",
    "image": { "format": "D32_SFLOAT", "width": 2048, "height": 2048,
               "arrayLayers": 4, "viewType": "2d_array" } }
],
"samplers": {
  "shadowCompare": { "magFilter": "linear", "minFilter": "linear",
                     "addressMode": "clamp_to_border", "borderColor": "float_opaque_white",
                     "compareEnable": true, "compareOp": "less_or_equal" }
},
"bufferLayouts": {
  "ShadowPerDraw": { "usage": "push_constant", "packing": "scalar",
    "fields": [
      { "name": "model",   "type": "mat4", "source": "entity.transform.worldMatrix" },
      { "name": "cascade", "type": "uint", "source": "pass.repeatIndex" }
    ] },
  "ShadowUBO": { "usage": "uniform_buffer", "packing": "std140", "updateFrequency": "per_frame",
    "fields": [
      { "name": "cascadeViewProj", "type": "mat4", "arrayCount": 4, "source": "scene.shadows.sun.cascades[i].viewProj" },
      { "name": "cascadeSplits",   "type": "vec4",                  "source": "scene.shadows.sun.splits" },
      { "name": "texelWorldSize",  "type": "vec4",                  "source": "scene.shadows.sun.texelWorldSize" }
    ] }
},
"passes": [
  {
    "name": "SunShadow",
    "repeat": { "count": 4, "index": "cascade" },
    "type": "graphics",
    "execution": {
      "type": "shadow_casters",
      "view": "shadows.sun.cascades[{cascade}]",   // cull against this frustum
      "entityDataBinding": "shadowCaster"
    },
    "outputs": [
      { "resource": "sunShadow", "layer": "{cascade}", "usage": "depth_write",
        "clear": { "depth": 1.0 } }
    ],
    "pipeline": {
      "vertexShader": "shaders/shadow_depth.vert.spv",
      "depthClamp": true,
      "depthCompareOp": "less",
      "depthBias": { "constant": 1.25, "slope": 1.75 },
      "cullMode": "none"
    },
    "descriptorSets": ["shadowWriteSet"],
    "enabled": "scene.shadows.sun.enabled"
  }
]
```

A reader then uses `"usage": "shader_read"` on `sunShadow` with no layer,
getting the full array view.

## Implementation phases

Each phase is usable on its own and keeps existing JSON valid.

### Phase 0 — small fixes (no new concepts) — done
- Expose `depthClamp` (with a device-feature check) and `depthCompareOp` in
  the pipeline JSON block.
- `RenderGraph::setPassEnabled(name, bool)` with facade and Python
  bindings. A disabled pass keeps its barriers and render pass, so its
  attachments are cleared and end in the expected layouts; only draws and
  dispatches are skipped.
- Split the shadow filter: `shadow_casters` excludes skinned entities; add
  `skinned_shadow_casters`. Execution `"alphaFilter": "opaque" | "mask"`
  lets opaque casters keep a fragment-shader-free pipeline.
- The entity geometry execution types live in one list
  (`isEntityGeometryExecutionType`) shared by the executor and RenderGraph.
- Warn when JSON sets `mipLevels`/`arrayLayers` until phase 1 lands.
- Move the shrine to `shadow_casters` and a compare sampler.

### Phase 1 — image subresources — done
- Vulkan 1.3 is required; graph passes render with dynamic rendering, so
  there are no render pass or framebuffer objects. Presentation is an explicit
  post-barrier.
- Images take `mipLevels` (or `"full"`), `arrayLayers` and `viewType`
  (`2d`, `2d_array`, `cube`, `cube_array`); cube images are created
  cube-compatible.
- Accesses and auto-bound descriptors take `"mip"`/`"mips"` and
  `"layer"`/`"layers"`. `PhysicalImage` keeps the sampled whole-image view
  plus views of parts, created on first use.
- Dependencies, culling and barriers work per mip and layer, in
  `FrameGraphSchedule` (device-free and unit-tested). Barriers are merged into
  ranges, are emitted for write hazards even without a layout change, and each
  frame's first barriers wait for the previous frame's last accesses.
- Passes run in declaration order, which is always a valid order now that
  every dependency points backwards.
- Compute passes get an extent (their first image output's mip size), so
  `compute_image` dispatches over it.
- Checked on lavapipe with synchronization validation: the shrine pipeline
  and a scratch pipeline with four cascades in one array and a three-level
  bloom mip chain run clean.

Left for later: the analyzer, exporter and debugger still print barriers
without their ranges.

### Phase 2 — pass repetition and pass-scoped data — done
- `"repeat": { "count": N, "index": "name", "first": 0 }` expands a pass at
  load into one pass per index value. `{name}`, `{name+K}` and `{name-K}` are
  substituted in every string, and a string that is only a placeholder becomes
  a number (`"layer": "{cascade}"`). Instances are named by the substituted
  name, or `Name[value]` when the name has no placeholder.
- Descriptor set layouts repeat the same way (`"downsample{m}"`), for passes
  that each bind a different mip or layer.
- `pass.repeatIndex`, `pass.repeatCount`, `pass.extent` and `pass.texelSize`
  resolve in push-constant layouts, including per-entity ones, and as named
  push-constant bindings of fullscreen and compute passes. Other layouts using
  them fail compilation.
- `setPassEnabled` with a repeated pass's declared name switches every
  instance.
- The Python validator expands repeats the same way before checking.
- Checked on lavapipe: the four-cascade scratch pipeline written as one
  repeated pass with one cascade UBO renders pixel-identically to the
  four-pass version, with no validation messages.

Not done: `count` from a graph parameter (with a recompile when it
changes). A fixed count covers cascades and mip chains.

### Phase 3 — engine-side shadow setup — done for the sun
- `ShadowCascades` (device-free, unit-tested) splits the view (blend of
  logarithmic and even splits), fits each cascade with the bounding sphere
  of its frustum slice, which depends only on depths, field of view and
  aspect ratio so it is unchanged by camera rotation, and snaps the matrix so
  the world moves across the map in whole texels.
- `SceneContext` fits them every frame for the first directional light with
  `castShadows` and publishes `scene.shadows.sun.*` (`cascades[N].viewProj`,
  `splits`, `texelWorldSize`, `cascadeCount`, `enabled`, `lightIndex`,
  `direction`).
- Settings through `setSunShadowSettings` / `set_sun_shadows`, debugging
  through `getSunShadowCascade` / `get_sun_shadow_cascade`.
- Found on the way: directional lights were created with their vertical
  direction inverted (a sun meant to shine down shone up), and the shader
  interface validator rejected every array of matrices. Both fixed.
- Checked on lavapipe: the four-cascade scratch pipeline driven entirely by
  `scene.shadows.sun.*` renders correct shadows with no validation messages.

Moved to phase 6: local-light (spot and point) shadow matrices and slot
assignment. Nothing consumes them before a pipeline has local-light shadow
maps, and the slot scheme belongs with that pipeline's layout (arrays or an
atlas).

### Phase 4 — per-view culling — done
- glTF meshes carry mesh-space bounds (`MeshComponent::boundsMin/Max`),
  transformed to world space per draw.
- `execution.view`: `camera` (default for camera-facing passes), `none`
  (default for shadow casters and sprites) or `shadows.sun.cascades[N]`,
  which culls against the cascade's light volume without a near plane and
  sorts by depth along the sun. Skinned entities are never culled.
- Culling math (`ViewCulling`) is device-free and unit-tested; per-pass
  drawn/culled counts are available through `get_pass_draw_stats`.
- Checked on lavapipe: with boxes around and behind the camera, draws per
  frame fell from 120 to 47 (G-buffer 11 of 24, cascades 3, 6, 8 and 19), and
  the frame was pixel-identical to the unculled one.

Not done: a caster whose shadow cannot reach the camera's view is still drawn
if it is inside the cascade (a tighter test would sweep its bounds along the
sun), and there is no GPU-driven culling or indirect drawing yet.

### Phase 5 — ship the default pipeline — done, except tiers and the examples

Done: [`python/shoonyakasha/pipelines/default/`](../../python/shoonyakasha/pipelines/default/README.md),
under the Python package so the wheel ships it, with its SPIR-V committed.
`sk.Engine()` with no pipeline path loads it; so does a C++ config with an
empty `pipelineJsonPath` (`$SHOONYAKASHA_DEFAULT_PIPELINE`, else the source
tree). It has cascaded sun shadows with 16-tap Vogel PCF, per-cascade normal
offset and blended seams; contact shadows; opaque, masked and skinned
variants of the shadow and G-buffer passes; a shadow mask; deferred lighting
with IBL; forward transparency sampling the cascades; ACES tonemapping; and
debug views for cascades, the mask and normals. Its settings are
`scene.custom.default.*` values.

Differences from the sketch above: the shadow mask and contact shadows are one
fullscreen fragment pass rather than compute; the G-buffer has no motion
vectors yet; the shaders live beside the pipeline rather than in `sk/`, since
they share buffer blocks with its JSON; tonemapping is still ACES.

What it needed on the engine side:
- Buffer-layout fields take a `"default"`, written while their source does
  not resolve, so settings work without the application publishing them.
- Relative shader paths resolve beside the pipeline JSON when the file is
  there, so a pipeline loads from any working directory.
- Vertex-only graphics pipelines: a pass without a fragment shader used to
  fail to build, despite phase 0 describing depth-only casters.
- Material and skeleton sets bind at the index the current pass lists them;
  it used to be the index of whichever pass was found first or last.
- A skeleton set is rewritten only when its bone buffer changes. Rewriting it
  after an earlier pass in the frame bound it invalidated the command buffer
  as soon as two passes drew the same skinned mesh.
- Without an HDR map, or when it fails to load, a pipeline declaring an
  `iblSet` gets a uniform environment (`IBLGenerator::generateUniform`);
  IBL shaders are also looked for beside the pipeline.

Checked on lavapipe with sync validation: boxes, a skinned fox, a blended
pane and a point light, with and without an HDR map, from a directory with no
shaders of its own; no validation messages beyond the known screenshot one.

Second half, also done:
- GTAO at full resolution (2 slices x 8 steps each side, a 4x4 pattern of
  slice directions and a depth-aware 4x4 average), with multi-bounce and
  specular occlusion in the lighting.
- Bloom: 13-tap downsampling into a 6-mip half-resolution chain, the first
  step Karis-averaged, then tent-filtered upsampling added back up the chain.
  No threshold; mixed into the image before tonemapping.
- Automatic exposure without buffers: one compute workgroup builds a 64-bin
  log-luminance histogram of mip 2 of the bloom chain in shared memory,
  averages the 40th to 95th percentile and adapts a 1x1 image.
- For these the frame graph gained `"persistent"` images, whose contents
  survive into the next frame (their first barrier starts from last frame's
  layout; they are cleared once at compile time), and `"step"` on `repeat`,
  so the upsampling passes can be declared smallest mip first.

Checked on lavapipe with sync validation, with and without an HDR map: no
validation messages, a flat floor reads about 0.98 unoccluded, and exposure
brings an HDR scene that blows out at exposure 1 back into range.

Still to do in this phase:
- Quality tiers.
- Moving the shrine and Sponza examples onto it.

The original outline:
- `assets/pipelines/default/pipeline.json` + shaders, built by
  `CompileShaders.cmake`, loadable with one call from C++ and Python.
- Shaders: GBuffer (static/skinned/masked), shadow (static/skinned/masked),
  CSM sampling + Vogel PCF in `sk/shadows.glsl`, contact shadows, shadow
  mask, GTAO, deferred lighting, forward transparent, bloom, exposure,
  tonemap.
- Quality tiers as pass `enabled` + scene values, not separate JSON files.
- Update the shrine and Sponza examples to use it; keep the hand-written
  shrine JSON as the "write your own pipeline" example.

### Phase 6 — later
- Local-light shadows (needs phases 1–4).
- History resources (`"history": true`, readable as `name.prev`) → TAA,
  temporal shadow/AO filtering.
- Clustered lighting, shadow atlas, ray-query shadows.

## Test hooks

- Unit: the JSON expansion of `repeat`, subresource barrier merging, and
  cascade math (split distances, snapping keeps texel alignment under camera
  translation) are all device-free.
- Validation layers on the shrine with phase-1 barriers.
- Readback of a cascade layer with `saveRenderTarget` extended to take a
  layer, for visual checks.
