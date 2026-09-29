# The default pipeline

What `sk.Engine()` renders with when no `pipeline_json_path` is given, and
what a C++ `ApplicationConfig` with an empty `pipelineJsonPath` loads. This
is the reference; [Using the default pipeline](../../../../docs/guides/default-pipeline.md)
is the guide: what a scene needs, which settings to reach for, and how to
change a copy.
Deferred PBR (glTF metallic-roughness) with:

- cascaded sun shadows: four cascades in one 2048² depth array, 16-tap Vogel
  PCF rotated per pixel, per-cascade normal offset, blended cascade seams
- ray-traced sun shadows, where the device has ray queries (the `raytraced`
  preset): one ray per pixel towards a random point of the sun's disc,
  averaged over frames by TAA into a penumbra that widens away from the caster
- contact shadows: a short screen-space march towards the sun, for the detail
  the cascades are too coarse for
- shadows from up to eight spot lights and four point lights, given each
  frame to the shadow-casting lights that matter most, as tiles of one 4096²
  atlas sized by how large each light looks (spot maps up to 2048², cube
  faces up to 1024²), filtered with 3x3 hardware comparisons
- opaque, alpha-tested (glTF `MASK`) and skinned geometry, in the G-buffer
  and in the shadows
- up to 128 directional, point and spot lights, clustered: a compute pass
  sorts the point and spot lights into 16 x 9 screen tiles x 24 depth slices
  by range, and each pixel shades only its cluster's (at most 63) and the
  directional lights
- image-based light from the HDR environment, or from a uniform environment
  when there is none
- ground-truth ambient occlusion (GTAO), with the light that bounces between
  occluders added back and occlusion of reflections
- emission, material occlusion, forward-shaded blended (glTF `BLEND`)
  materials that receive shadows
- temporal anti-aliasing: a sub-pixel camera jitter, history reprojected
  with per-pixel motion vectors (moving, skinned and animated objects as well
  as the camera) and clipped to the new frame's colours; it also averages the
  per-frame noise of shadow filtering and ambient occlusion
- bloom from a six-level mip chain, with no brightness threshold
- automatic exposure from a luminance histogram, adapting over time
- ACES filmic tonemapping

```python
import shoonyakasha as sk

engine = sk.Engine(title="My scene", hdr_environment_path="env/farm_sunset_1k.hdr")

def on_init():
    engine.load_gltf_scene("models/Box.gltf")
    engine.create_camera(pos=(0.0, 1.5, 5.0))
    sun = engine.create_directional_light(direction=(-0.45, -0.55, 0.7), intensity=3.0)
    engine.scene.set_light_cast_shadows(sun, True)      # the sun's cascades
    engine.set_sun_shadows(cascades=4, max_distance=60.0, resolution=2048)
    engine.set_custom_float("default.exposure", 0.9)

engine.set_on_init(on_init)
engine.run()
```

Only a directional light with its shadow flag set casts shadows; the first
such light is the sun. `resolution` should stay 2048, the size of the map
declared in `pipeline.json`. Spot and point lights cast shadows the same way,
with `set_light_cast_shadows(light, True)`; `set_local_shadows()` sets how many
get one, and its defaults match this pipeline.

## Settings

Each is a `scene.custom.default.*` value with a default in the pipeline, so
set only what you want to change: `engine.set_custom_float("default.exposure", 0.8)`
(`set_custom_uint` for `autoExposure`, `taa` and `debugView`).

| Setting | Default | Meaning |
|---|---|---|
| `exposure` | 1.0 | Multiplies the scene before tonemapping; with automatic exposure on, exposure compensation (2.0 is one stop brighter) |
| `autoExposure` | 1 | 1 adapts exposure to the scene, 0 uses `exposure` alone (`set_custom_uint`) |
| `exposureAdaptSpeed` | 1.5 | How fast exposure follows the scene, per second; higher is faster |
| `exposureMinEV`, `exposureMaxEV` | -4, 14 | The range of scene brightness (EV100 of its average luminance) exposure adapts to. Darker scenes than the minimum stay dark |
| `iblIntensity` | 1.0 | Scales image-based light and the sky |
| `skyBlur` | 0.0 | Mip of the environment shown as the sky; higher is blurrier |
| `shadowSoftness` | 1.5 | Shadow filter radius, in texels of the first cascade. Later cascades use the same world-space width |
| `shadowNormalBias` | 1.0 | How far lookups move off the surface along its normal, in shadow texels. Raise it for acne, lower it for light leaking at contact points |
| `shadowDepthBias` | 0.0002 | Constant depth bias, in shadow map depth |
| `cascadeBlend` | 0.1 | Fraction of each cascade blended into the next; 0 for hard seams |
| `contactShadowLength` | 0.25 | World-space length of the contact shadow march; 0 turns contact shadows off |
| `contactShadowThickness` | 0.1 | How thick things on screen are assumed to be, in world units |
| `shadowAmbient` | 1.0 | Ambient light kept in the sun's shadow. Below 1 darkens shadowed areas' sky light too |
| `sunAngle` | 0.4 | Angular radius of the sun in degrees, for ray-traced shadows: how quickly their penumbra widens with distance from the caster. The real sun's is 0.27 |
| `aoRadius` | 0.6 | World-space reach of ambient occlusion; 0 turns it off |
| `aoIntensity` | 1.0 | Power applied to the occlusion; above 1 darkens it |
| `bloomIntensity` | 0.04 | How much of the bloom is mixed into the image |
| `bloomRadius` | 1.0 | Spread of each upsampling step, in texels |
| `taa` | 1 | 1 for temporal anti-aliasing, 0 for none (`set_custom_uint`). Turn it off here rather than turning its passes off: the passes after it read its output |
| `debugView` | 0 | 1 cascades (red, green, blue, yellow), 2 shadow mask, 3 normals, 4 ambient occlusion, 5 point and spot lights reaching each pixel's cluster (blue none, green 8, red 16 or more), 6 motion vectors (hue for direction, full brightness at 10 pixels a frame); shown without tonemapping, bloom or exposure (`set_custom_uint`) |

Without an HDR map the environment is one colour, `environment_color` on
`sk.Engine` (`uniformEnvironmentColor` in C++), default (0.25, 0.28, 0.33).

## Quality presets

The pipeline declares four presets; apply one with
`engine.apply_pipeline_preset("low")` (C++ `applyPipelinePreset`), from
`on_init` or at any time after:

| Preset | Changes |
|---|---|
| `low` | No ambient occlusion, no shadows from alpha-tested casters (sun, spot or point), no contact shadows, no bloom, a narrower shadow filter and hard cascade seams |
| `medium` | Everything but contact shadows |
| `high` | Everything, every setting back to its default. What the pipeline starts with |
| `raytraced` | `high`, with sun shadows from ray queries instead of the cascades' opaque casters, on blended materials too. Refused, with a warning, on a device without ray queries; check with `engine.ray_query_supported()` (C++ `rayQuerySupported`) |

A preset only touches the passes and settings it names, so settings of your
own that it does not mention stay as you set them.

## Passes

| Pass | Draws |
|---|---|
| `ShadowOpaque0..3`, `ShadowMasked0..3`, `ShadowSkinned0..3` | Shadow casters into cascade 0..3, each culled against its cascade |
| `SpotShadowOpaque0..7`, `SpotShadowMasked0..7`, `SpotShadowSkinned0..7` | Shadow casters into spot slot 0..7's tile of `localShadowAtlas`, culled against the light's cone; nothing when the slot is empty |
| `PointShadowOpaque0..23`, `PointShadowMasked0..23`, `PointShadowSkinned0..23` | Shadow casters into cube face 0..23's tile (four lights, six faces each), culled per face |
| `GBufferOpaque`, `GBufferMasked`, `GBufferSkinned`, `GBufferSkinnedMasked` | The G-buffer and motion vectors; emission goes straight into the HDR target |
| `ShadowMask` | Cascades and contact shadows resolved per pixel, into `shadowMask` |
| `ShadowMaskRT` | In the `raytraced` preset, instead of `ShadowMask`: a shadow ray per pixel, multiplied with the cascades of the casters the rays do not see. `"requires": ["rayQuery"]`; off by default |
| `GTAO`, `AODenoise` | Ambient occlusion from the depth buffer in a 4x4 pattern of slice directions, then a depth-aware 4x4 average |
| `LightClusters` | A compute pass listing the point and spot lights that reach each cluster of the view, into `lightClusters` |
| `Lighting` | Lights, image-based light and the sky, added onto the emission |
| `Transparent` | Blended materials, back to front, sampling the cascades directly |
| `TransparentRT` | In the `raytraced` preset, instead of `Transparent`: the same, with sun shadow rays. `"requires": ["rayQuery"]`; off by default |
| `TAA`, `TAAHistory` | The new frame blended with last frame's result into `taaColor`, which is then copied into the persistent `taaHistory` for the next frame |
| `BloomDown0`, `BloomDown1..5` | The HDR image down a half-resolution mip chain, with a 13-tap filter |
| `AutoExposure` | A compute pass: a histogram of mip 2 of the chain, and the adapted exposure in a persistent 1x1 image |
| `BloomUp4..0` | Back up the chain, each mip adding a tent-filtered copy of the one below |
| `Tonemap` | Bloom, exposure and tonemapping, to the swapchain |

Any pass can be turned off at runtime, for instance for a low-quality tier:
`engine.set_pass_enabled("ShadowMasked{cascade}", False)` turns off all four
masked cascade passes (a repeated pass answers to its declared name); masked
geometry then casts no shadow. Turning off `ShadowMask` leaves it cleared to
fully lit, and turning off `GTAO` leaves no occlusion. For bloom, set
`bloomIntensity` to 0 rather than turning passes off: automatic exposure reads
the chain.

G-buffer layout: `gAlbedo` (RGBA8 sRGB: albedo, material occlusion),
`gNormal` (RG16F: octahedral world normal), `gMaterial` (RG8: metallic,
roughness), `gVelocity` (RG16F: this frame's screen position minus last
frame's, in UV units, without the jitter), `gDepth` (D32; positions are
reconstructed from it) and `hdrColor` (RGBA16F).

The G-buffer passes push 180 bytes of constants per draw (this and last
frame's model matrix, then the material factors). Every desktop GPU allows
256; a device that allows only Vulkan's minimum of 128 reports those passes
at load and draws nothing in them.

## Ray-traced shadows

Where the device has `VK_KHR_ray_query` (the engine enables it and
`VK_KHR_acceleration_structure` when present; set
`SHOONYAKASHA_DISABLE_RAY_QUERY=1` to leave them off), the engine keeps an
acceleration structure of the scene: one bottom-level structure per mesh,
built when the mesh is first seen, and a top-level one rebuilt every frame
from the transforms. `ShadowMaskRT` binds it as an `acceleration_structure`
descriptor and traces one ray per pixel.

Only static, opaque, shadow-casting meshes go into it. Skinned meshes would
need their structures rebuilt from the skinned vertices every frame, and
alpha-tested ones an any-hit test against their textures; both still cast
into the cascades, which is why the preset keeps `ShadowMasked{cascade}` and
`ShadowSkinned{cascade}` on and turns only `ShadowOpaque{cascade}` off. The
mask is the product of the two. Blended materials have no mask; the preset
swaps `Transparent` for `TransparentRT`, which traces four rays per pixel
itself (TAA cannot average a surface it sees through) and multiplies them
with the cascades the same way. Spot and point lights keep their shadow maps.

One ray per pixel is noisy on its own; TAA averages it. With `taa` 0 the
shadows' edges are left grainy.

## The shadow atlas

Spot and point light shadows share `localShadowAtlas`, 4096² of 32-bit depth
(64 MB). Each frame the engine gives every shadowed light a square tile, a
power of two by how large its range looks from the camera: the full 2048² for
a spot light the camera stands in, less as it recedes, down to 128². A point
light gets six tiles, one per cube face, up to 1024². When they would not all
fit, the least important lights' tiles are halved first. The tile passes
render into their rectangle only (`"viewport"` in `pipeline.json`), clearing
just that; the shaders read each light through its tile's rectangle and keep
the filter inside it, and pick the cube face of a point light themselves.

`set_local_shadows(spot=8, point=4, spot_resolution=2048,
point_resolution=1024, atlas_resolution=4096)` are the defaults; lower the
largest tiles for a smaller budget of memory bandwidth, or the atlas to fit a
smaller image declared in a copy of the pipeline. `atlas_resolution=0` is for
pipelines that keep one array layer per slot instead.

## Changing it

Copy this directory and pass the copy's `pipeline.json` as
`pipeline_json_path`. Shader paths are relative to the JSON file, so the copy
works from any working directory. `shaders/` holds the GLSL beside the
compiled SPIR-V; after editing, recompile with
`sk.shaders.compile_dir("shaders")`. The `.glsl` files are included by the
stage shaders: `common.glsl` holds the buffer blocks, which must match the
`bufferLayouts` in `pipeline.json`.
