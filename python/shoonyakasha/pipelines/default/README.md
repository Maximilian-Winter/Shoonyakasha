# The default pipeline

What `sk.Engine()` renders with when no `pipeline_json_path` is given, and
what a C++ `ApplicationConfig` with an empty `pipelineJsonPath` loads. This
is the reference; [Using the default pipeline](../../../../docs/guides/default-pipeline.md)
is the guide: what a scene needs, which settings to reach for, and how to
change a copy.
Deferred PBR (glTF metallic-roughness) with:

- cascaded sun shadows: four cascades in one 2048² depth array, 16-tap Vogel
  PCF rotated per pixel, per-cascade normal offset, blended cascade seams
- diffuse global illumination in the `raytraced` preset: an irradiance cache
  (after kajiya's) of light that bounced, from the suns, a sample of the
  other lights and the sky, in place of the environment's diffuse light
- ray-traced sun shadows, where the device has ray queries (the `raytraced`
  preset): one ray per pixel towards a random point of the sun's disc,
  averaged over frames by TAA into a penumbra that widens away from the caster
- contact shadows: a short screen-space march towards the sun, for the detail
  the cascades are too coarse for
- shadows from up to sixteen spot lights and eight point lights, given each
  frame to the shadow-casting lights that matter most, as tiles of one 4096²
  atlas sized by how large each light looks (spot maps up to 2048², cube
  faces up to 1024²), filtered with 3x3 hardware comparisons
- opaque, alpha-tested (glTF `MASK`) and skinned geometry, in the G-buffer
  and in the shadows
- up to 8192 directional, point and spot lights, read from the `SceneLights`
  storage buffer the engine refills every frame, and clustered: a compute
  pass sorts the point and spot lights into 16 x 9 screen tiles x 24 depth
  slices by range, testing each light against each cluster, and each pixel
  shades only its cluster's and the directional lights. The clusters' lists
  share one buffer of 524288 light indices
- rectangle lights: a point or spot light with a source size
  (`scene.set_light_source_size`) is shaded as that rectangle by linearly
  transformed cosines (`shaders/area_lights.glsl`, with the fitted table
  `ltc.bin`), so its highlights take the rectangle's shape; it lights only
  what is in front of it
- spot light cookies: a spot light without a rectangle whose image slot
  (`scene.set_light_source_image`) is N shines through tile N of
  `lightCookies`, an application image (`"externalImage"`) holding a 4 x 4
  grid of tiles; `shoonyakasha.cookies.atlas` builds one
- IES profiles: a point or spot light without a rectangle whose image slot
  is 16 + N follows tile N of `lightProfiles`, an application image laid
  out the same way, which scales its intensity by direction;
  `shoonyakasha.ies` reads IES files and builds one
- image-based light from the HDR environment, or from a uniform environment
  when there is none
- ground-truth ambient occlusion (GTAO), with the light that bounces between
  occluders added back and occlusion of reflections
- emission, material occlusion, forward-shaded blended (glTF `BLEND`)
  materials that receive shadows
- sheen (`KHR_materials_sheen`): a Charlie lobe over the base, for cloth
- specular (`KHR_materials_specular`, `KHR_materials_ior`): a dielectric's
  specular strength, colour and reflectance
- iridescence (`KHR_materials_iridescence`): a thin film over the base whose
  interference colours the specular by its thickness and index of refraction
- clear coat (`KHR_materials_clearcoat`): a glossy dielectric layer with its
  own roughness over the base, from every light and the environment
- transmission (`KHR_materials_transmission`, `KHR_materials_ior`,
  `KHR_materials_volume`): forward-shaded surfaces that let through the
  opaque scene behind them, refracted, blurred by roughness and attenuated
- temporal anti-aliasing: a sub-pixel camera jitter, history reprojected
  with per-pixel motion vectors (moving, skinned and animated objects as well
  as the camera) and clipped to the new frame's colours; it also averages the
  per-frame noise of shadow filtering and ambient occlusion
- bloom from a six-level mip chain, with no brightness threshold
- automatic exposure from a luminance histogram, adapting over time
- AgX tonemapping (Khronos PBR Neutral and ACES selectable)

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
| `reflections` | 1.0 | How much reflections of the scene (screen-space, or ray traced with `raytraced`) replace the environment's on glossy surfaces, 0 to 1; 0 traces none. The `low` preset sets 0 |
| `aoRadius` | 0.6 | World-space reach of ambient occlusion; 0 turns it off |
| `aoIntensity` | 1.0 | Power applied to the occlusion; above 1 darkens it |
| `toneMapper` | 2 | Tone curve: 2 AgX, which keeps mid-tones as exposed and rolls bright light off to white without shifting its hue; 1 Khronos PBR Neutral, which keeps base colours as authored, for exposures that put white at 1; 0 the ACES fit, more contrast (`set_custom_uint`) |
| `fogDensity` | 0.0 | Height fog's density per metre at `fogHeight`; 0 is no fog |
| `fogHeight` | 0.0 | Height in metres where the fog has `fogDensity` |
| `fogHeightFalloff` | 0.0 | How fast the fog thins with height: by a factor e every 1/falloff metres above `fogHeight`, thickening below it; 0 is the same density everywhere |
| `fogColor` | (1, 1, 1) | Tint of the fog's light, which is the environment's, blurred, times `iblIntensity` |
| `gi` | 0 | 1: `LightingRT` takes the diffuse ambient light from the irradiance cache (`giIrradiance`) instead of the environment's irradiance map, and `GiLookup` fills it. 2: it takes it from ReSTIR GI (`giIndirect`), which the `Rtdgi*` and `Restir*` passes fill, with the cache beyond the first bounce. The `raytraced` preset sets 1, the others 0 |
| `oit` | 0 | 1: blended materials use weighted blended order-independent transparency, whose result does not depend on draw order but is an approximation; 0: they are sorted back to front and blended in turn |
| `motionBlur` | 0.0 | Motion blur: the share of each frame the shutter is open, 0 to 1 (0.5 is a film camera's 180 degree shutter); 0 is none |
| `dof` | 0 | Depth of field: 1 blurs what is nearer or farther than `dofFocus` |
| `dofFocus` | 5.0 | Distance in focus, in metres |
| `dofFStop` | 2.8 | f-number of a full-frame camera with the view's focal length; smaller blurs more |
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
| `raytraced` | `high`, with sun shadows from ray queries instead of the cascades' opaque and alpha-tested casters, on blended materials too, and global illumination from the irradiance cache (`Ircache*`, `GiLookup`, `gi` 1); the ReSTIR GI passes (`Rtdgi*`, `Restir*`) run too, and do nothing unless `gi` is 2. Refused, with a warning, on a device without ray queries; check with `engine.ray_query_supported()` (C++ `rayQuerySupported`) |

A preset only touches the passes and settings it names, so settings of your
own that it does not mention stay as you set them.

## Passes

| Pass | Draws |
|---|---|
| `ShadowOpaque0..7`, `ShadowMasked0..7`, `ShadowSkinned0..7` | Shadow casters into `shadowMap` layer 0..7, each culled against its cascade: layers 0..3 the first sun's cascades, 4..7 the second sun's, which draw nothing without one |
| `SpotShadowOpaque0..7`, `SpotShadowMasked0..7`, `SpotShadowSkinned0..7` | Shadow casters into spot slot 0..7's tile of `localShadowAtlas`, culled against the light's cone; nothing when the slot is empty |
| `PointShadowOpaque0..23`, `PointShadowMasked0..23`, `PointShadowSkinned0..23` | Shadow casters into cube face 0..23's tile (four lights, six faces each), culled per face |
| `GBufferOpaque`, `GBufferMasked`, `GBufferSkinned`, `GBufferSkinnedMasked` | The G-buffer and motion vectors; emission goes straight into the HDR target |
| `HiZ` | The farthest depth of each 8 x 8 block of `gDepth`, into `hiz` (R32F, an eighth of the size, read back every frame). `GBufferOpaque`, `GBufferMasked`, `Transparent` and `TransparentRT` leave out the static entities it hides (execution `"occlusion": "hiz"`) |
| `ShadowMask` | Cascades and contact shadows resolved per pixel, into `shadowMask` (RGBA8: the first sun's visibility, its cascade for debug view 1, the second sun's visibility) |
| `ShadowMaskRT` | In the `raytraced` preset, instead of `ShadowMask`: a shadow ray per pixel, multiplied with the cascades of the casters the rays do not see. `"requires": ["rayQuery", "bindlessTextures"]`; off by default |
| `GTAO`, `AODenoise` | Ambient occlusion from the depth buffer in a 4x4 pattern of slice directions, then a depth-aware 4x4 average |
| `LightClusterReset` | Zeroes `ClusterLightCount`: the lights the clusters want, and the room in `ClusterLightIndices` taken |
| `LightClusterCount` | A compute pass counting the point and spot lights that reach each cluster of the view, into `lightClusters`, and their total |
| `LightClusters` | A compute pass listing them: each cluster takes room in `ClusterLightIndices` for its list, and `lightClusters` holds the list's count and offset. When the total does not fit, each cluster gets the same share of the room as of the total, and keeps its nearest lights |
| `Lighting` | Lights, image-based light and the sky, added onto the emission; screen-space reflections of glossy surfaces, written to `reflection` and `reflectionScale` |
| `IrcacheInit` ... `IrcacheSum` | In the `raytraced` preset: the irradiance cache's compute passes (`ircache_*.comp`). Around the camera, 12 cascades of 32³ cells, the smallest 2 cm across and each the next twice as wide, hold up to 65,536 entries, allocated where a lookup lands. Each frame every live entry traces 4 rays, validates 4 earlier ones and checks it still sees where its samples came from; a ray's hit is lit by the suns, two of the other lights picked at random (each with a shadow ray) and its emission, plus the cache one rank further out, and a miss by the environment (`iblIntensity`). The samples, kept in reservoirs per stratum, become L1 spherical harmonics. The cache's buffers (`Ircache*` in `bufferLayouts`, about 76 MB) exist in every preset. `"requires": ["rayQuery"]`, the tracing ones also `bindlessTextures`; off by default |
| `GiLookup` | In the `raytraced` preset: the cache looked up at every G-buffer pixel, with half a cell of jitter that TAA averages, into `giIrradiance` (RGBA16F); the lookups allocate the entries near the camera. Black unless `gi` is 1 |
| `RtdgiGBuffer` ... `RtdgiCopyHistoryFull` | In the `raytraced` preset, with `gi` 2 (they return at once otherwise): kajiya's ray-traced diffuse GI, ported from the [ReSTIR GI example](../../../../examples/python/rendering/restir_gi/README.md) (`rtdgi_*.comp`, `restir_*.comp`). `RtdgiGBuffer` writes each pixel's world position, view depth, normals and roughness (`giPosition`, `giNormal`, `giGeoNormal`); `RtdgiReproject` finds last frame's place of each pixel from `gVelocity`, valid where last frame's view depth there matches within 5% (`reproj`). At half resolution each pixel then traces one diffuse ray a frame, lit at its hit by the lights, its emission, and last frame's indirect light there when the hit is on screen or the irradiance cache otherwise; every third frame the rays of the reservoirs' samples are traced again instead. Temporal and two spatial reservoir passes reuse the samples, `RestirResolve` brings them to full resolution, and a temporal and a spatial filter write `giIndirect` (RGBA16F). The images take about 157 bytes per pixel, 31 of them history kept between frames. `"requires": ["rayQuery"]`, `RtdgiValidate` and `RtdgiTrace` also `bindlessTextures`; off by default |
| `LightingRT` | In the `raytraced` preset, instead of `Lighting`: the same, with shadow rays towards spot and point lights instead of the shadow atlas, a reflection ray per glossy pixel, written to `reflection` and `reflectionScale`, and with `gi` the diffuse ambient light from `giIrradiance` (1) or `giIndirect` (2), still occluded by `ao`. `"requires": ["rayQuery", "bindlessTextures"]`; off by default |
| `ReflectionDenoise` | The reflections `Lighting` or `LightingRT` wrote, averaged over each surface and added to the HDR target. Off in the `low` preset |
| `HeightFog` | Exponential height fog (`fog.glsl`) blended over the opaque scene and the sky: the fog's share along each view ray, to the far plane for the sky, of the environment's blurred light. `Transparent` and `TransparentRT` fog blended surfaces the same way. Draws nothing with `fogDensity` 0 |
| `SceneBehind`, `SceneBehindMips1`–`5` | Copy the lit, fogged opaque scene to `sceneBehind` and blit its mip chain, for transmissive surfaces to see through |
| `Transparent` | Blended and transmissive materials, back to front, sampling the cascades directly. With `oit` 1 blended ones go to `transparentAccum` and `transparentReveal` instead (weighted blended order-independent transparency): it clears both, and `TransparentRT` adds to them. Transmissive ones always draw straight into the HDR target |
| `TransparentRT` | In the `raytraced` preset, instead of `Transparent`: the same, with shadow rays towards the sun, spot and point lights. `"requires": ["rayQuery", "bindlessTextures"]`; off by default |
| `TransparentComposite` | With `oit` 1: the transparent surfaces' weighted average colour (`transparentAccum`) alpha-blended over the HDR target by their combined coverage (1 - `transparentReveal`). Draws nothing where none was accumulated |
| `TAA`, `TAAHistory` | The new frame blended with last frame's result into `taaColor`, which `TAAHistory`, a `copy` transfer pass, then copies into the persistent `taaHistory` for the next frame |
| `MotionBlur` | `taaColor` smeared along each pixel's motion (`gVelocity`; for the sky, the camera's turn) over the open shutter (`motionBlur`), 16 samples across it, at most 5% of the image width, into `motionColor`; samples nearer the camera count only where their own blur reaches the pixel. With `motionBlur` 0 or a debug view, a copy |
| `DepthOfField` | `motionColor` blurred by distance from the focus, as a full-frame camera with the view's focal length would (a single-pass gather on a golden-angle spiral, out to 2% of the image height), into `sceneColor`, which bloom and the tone mapping read; with `dof` 0 or a debug view, a copy |
| `BloomDown0`, `BloomDown1..5` | The HDR image down a half-resolution mip chain, with a 13-tap filter |
| `AutoExposure` | A compute pass: a histogram of mip 2 of the chain, and the adapted exposure in a persistent 1x1 image |
| `BloomUp4..0` | Back up the chain, each mip adding a tent-filtered copy of the one below |
| `Tonemap` | Bloom, exposure and tonemapping, to the swapchain |
| `UIOverlay` | Screen canvases of the canvas UI, blended over the swapchain, which it then presents. Its shaders are in `shaders/ui/` and the engine binds them itself |

Any pass can be turned off at runtime, for instance for a low-quality tier:
`engine.set_pass_enabled("ShadowMasked{cascade}", False)` turns off all eight
masked cascade passes (a repeated pass answers to its declared name); masked
geometry then casts no shadow. Turning off `ShadowMask` leaves it cleared to
fully lit, and turning off `GTAO` leaves no occlusion. For bloom, set
`bloomIntensity` to 0 rather than turning passes off: automatic exposure reads
the chain.

G-buffer layout: `gAlbedo` (RGBA8 sRGB: albedo, material occlusion),
`gNormal` (RG16F: octahedral world normal), `gMaterial` (RGBA8: metallic,
roughness, clear coat, clear coat roughness), `gSheen` (RGBA8 sRGB: sheen
colour, sheen roughness), `gSpecular` (RGBA8: the square root of the
dielectric's reflectance at normal incidence, specular strength),
`gIridescence` (RGBA8: the film's amount, its thickness over 0 to 2000 nm in
16 bits across g and b, and its index of refraction over 1 to 2.5), `gVelocity` (RG16F: this frame's screen position minus last
frame's, in UV units, without the jitter), `gDepth` (D32; positions are
reconstructed from it) and `hdrColor` (RGBA16F).

The G-buffer passes push 236 bytes of constants per draw (this and last
frame's model matrix, then the material factors). Every desktop GPU allows
256; a device that allows only Vulkan's minimum of 128 reports those passes
at load and draws nothing in them.

Each material texture is sampled at the uv its `MaterialData` rows give
(`material_data.glsl`): `TEXCOORD_0` or `TEXCOORD_1`, through the texture's
`KHR_texture_transform`. `MaterialData` is the material set's uniform buffer
at binding 9 (`"source": "material"`), filled from the material's
`<slot>UvRow0` and `<slot>UvRow1` parameters and its sheen, specular and
iridescence factors. The passes that read uvs take
the `standard` vertex format, with `TEXCOORD_1` at location 7.

The G-buffer, sun, spot and point shadow, and transparent passes for static
meshes are instanced (`"instancing": true`): entities sharing a mesh and a
material are one draw, and `gbuffer.vert`, `shadow_vert.glsl` and
`shadow_masked_vert.glsl` take each instance's model matrices from the
per-instance vertex buffer (locations 8 to 15) instead of the push
constants. The skinned passes draw each entity on its own.

The G-buffer and transparent passes cull by material (`"cullMode":
"material"`): single-sided materials show their front faces only,
double-sided ones both, and mirrored entities keep their front faces. The
shadow passes draw both sides of everything.

Materials can set `exposureCompensation`, from 0 (the default) to 1. At 1
the material's emission is divided by the exposure before Tonemap multiplies
by it, so it keeps its brightness whatever the exposure; canvas UI world
canvases use this. The G-buffer and transparent passes read the exposure from
`exposureReadSet`: the settings and last frame's adapted value from the 1x1
`exposure` image, which `AutoExposure` updates later in the frame.

The top-level `ui` block names `gDepth`, read after `GBufferSkinnedMasked`,
as the depth the canvas UI tests world-canvas hits against: geometry in front
of a world canvas stops the pointer.

Every image except `gDepth`, `taaHistory` and `exposure` is `transient`:
images that no pass uses at the same time share memory, and compiling logs
the blocks they share. A pass added to a copy that reads one of them must
declare it, as an input or through its descriptor sets.

## Ray-traced shadows

Where the device has `VK_KHR_ray_query` (the engine enables it and
`VK_KHR_acceleration_structure` when present; set
`SHOONYAKASHA_DISABLE_RAY_QUERY=1` to leave them off), the engine keeps an
acceleration structure of the scene: one bottom-level structure per mesh,
built when the mesh is first seen, and a top-level one rebuilt every frame
from the transforms. `ShadowMaskRT` binds it as an `acceleration_structure`
descriptor and traces one ray per pixel.

Shadow-casting meshes go into it, opaque and alpha-tested: the binding has
`"alphaTested": true`, and the rays test an alpha-tested triangle's albedo
alpha against the cutoff (`skAlphaTestCandidate`), reading it through
`RayInstances` and `sceneTextures`, the other two bindings of
`rayTracingSceneSet`. Skinned meshes are in it posed, by
`shaders/skinning/skin_vertices.comp`, which the engine runs every frame.
The preset turns `ShadowOpaque{cascade}` and `ShadowMasked{cascade}` off and
keeps `ShadowSkinned{cascade}` on, so skinned meshes also cast into the
cascades; the mask is the product of the rays and the cascades. Blended materials have no mask; the preset
swaps `Transparent` for `TransparentRT`, which traces four rays per pixel
itself (TAA cannot average a surface it sees through) and multiplies them
with the cascades the same way.

Spot and point lights are shadowed by rays as well: the preset swaps
`Lighting` for `LightingRT` (`lighting_rt.frag`, `lighting_body.glsl` built
with `RT_LOCAL_SHADOWS`) and turns every spot and point shadow-map pass off.
For each light with its shadow flag set, each pixel traces one ray
(`rt_lights.glsl`) towards a random point of the light: its rectangle, or the
disc of its source radius facing the pixel, or its centre for radius 0. Every
such light is traced, without the shadow atlas's slot budgets, and a ray costs
the same whatever the light's size; with many shadowed lights reaching the
same pixels, those pixels trace many rays. `TransparentRT` traces the same
rays. A light with source radius 0 casts hard, noise-free shadows; geometry
at the light itself should not cast shadows, or it hides the light.

`LightingRT` also traces reflections. On surfaces with roughness below 0.55
(fading out from 0.3), or their clear coat where they have one, each pixel
traces one ray along the reflection, spread by the roughness, to the nearest
surface, alpha-tested ones by their texture. Where that point is on screen
and not hidden there, last frame's image (`taaHistory`) at it is the
reflected light; where it is not, the environment's reflection is darkened
to 15%, and rays that hit nothing keep the environment. The difference from
the environment's reflection goes to `reflection` and `reflectionScale`;
`ReflectionDenoise` averages it over the surface around each pixel, over a
radius that grows with roughness, and adds it to the HDR target. The
`reflections` setting scales it.

Without `raytraced`, `Lighting` finds the reflections in screen space: the
same ray marches through the depth buffer, 40 steps across the screen
towards 40 metres, and at a step behind a surface, five halvings find the
crossing, a hit where the ray is behind the surface by less than 0.1 m
plus 2% of its distance. A hit takes last frame's image there, faded out
towards the screen's edges and the march's end; a ray that leaves the
screen or finds nothing keeps the environment's reflection. It writes the
same targets, which `ReflectionDenoise` filters the same way. Both lighting
passes write every pixel of those targets, without clearing them, so the
one the preset turns off leaves the other's in place.

One ray per pixel is noisy on its own; TAA averages it. With `taa` 0 the
shadows' and reflections' edges are left grainy.

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

`set_local_shadows(spot=16, point=8, spot_resolution=2048,
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
