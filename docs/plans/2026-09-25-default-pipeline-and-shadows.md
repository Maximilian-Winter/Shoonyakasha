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

### Phase 2 — pass repetition and pass-scoped data
- `"repeat": { "count": N, "index": "name" }` expands at JSON load into
  `Name[0]..Name[N-1]`; `{name}` is substituted in string values
  (layer, view, sources). `count` may be a dot-path read at compile time,
  with a recompile when it changes.
- New dot-path namespace `pass.*`: `pass.repeatIndex`, `pass.extent`,
  available to per-draw push constants. That removes the need for one UBO per
  cascade.

### Phase 3 — engine-side shadow setup
- A `ShadowSetupSystem` (per frame, before `updateSceneContext`) that reads
  lights with `castShadows`, computes sun cascades (split, fit, snap,
  near-extend) and local-light matrices, and assigns budgeted slots.
- Parameters as scene values (`shadows.sun.distance`, `.cascadeCount`,
  `.splitLambda`, `.resolution`) so Python/C++ can tune without touching JSON.
- Publishes `scene.shadows.sun.*` and `scene.shadows.local[i].*` dot-paths,
  including `viewProj`, `splits`, `texelWorldSize`, and each light's slot
  index in `scene.lights[i]` so lighting shaders can find its map.
- Facade: `setShadowSettings(...)`, `getShadowCascade(i)` for debugging.

### Phase 4 — per-view culling
- World-space AABB per mesh (computed at load, transformed per frame).
- `execution.view` names a view the renderer culls against; default is the
  main camera. Sorting uses the same view.
- Shadow views also cull casters whose shadow volume can't reach the camera
  frustum (cheap version: skip casters outside the cascade's light-space box).
- Later: instanced/indirect draws and GPU culling, which is what VSM would
  build on.

### Phase 5 — ship the default pipeline
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
