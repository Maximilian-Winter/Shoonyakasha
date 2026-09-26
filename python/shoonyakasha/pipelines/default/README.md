# The default pipeline

What `sk.Engine()` renders with when no `pipeline_json_path` is given, and
what a C++ `ApplicationConfig` with an empty `pipelineJsonPath` loads.
Deferred PBR (glTF metallic-roughness) with:

- cascaded sun shadows: four cascades in one 2048² depth array, 16-tap Vogel
  PCF rotated per pixel, per-cascade normal offset, blended cascade seams
- contact shadows: a short screen-space march towards the sun, for the detail
  the cascades are too coarse for
- opaque, alpha-tested (glTF `MASK`) and skinned geometry, in the G-buffer
  and in the shadows
- up to 16 directional, point and spot lights
- image-based light from the HDR environment, or from a uniform environment
  when there is none
- emission, material occlusion, forward-shaded blended (glTF `BLEND`)
  materials that receive shadows
- ACES filmic tonemapping with exposure

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
declared in `pipeline.json`.

## Settings

Each is a `scene.custom.default.*` value with a default in the pipeline, so
set only what you want to change: `engine.set_custom_float("default.exposure", 0.8)`
(`set_custom_uint` for `debugView`).

| Setting | Default | Meaning |
|---|---|---|
| `exposure` | 1.0 | Multiplies the scene before tonemapping |
| `iblIntensity` | 1.0 | Scales image-based light and the sky |
| `skyBlur` | 0.0 | Mip of the environment shown as the sky; higher is blurrier |
| `shadowSoftness` | 1.5 | Shadow filter radius, in texels of the first cascade. Later cascades use the same world-space width |
| `shadowNormalBias` | 1.0 | How far lookups move off the surface along its normal, in shadow texels. Raise it for acne, lower it for light leaking at contact points |
| `shadowDepthBias` | 0.0002 | Constant depth bias, in shadow map depth |
| `cascadeBlend` | 0.1 | Fraction of each cascade blended into the next; 0 for hard seams |
| `contactShadowLength` | 0.25 | World-space length of the contact shadow march; 0 turns contact shadows off |
| `contactShadowThickness` | 0.1 | How thick things on screen are assumed to be, in world units |
| `shadowAmbient` | 1.0 | Ambient light kept in the sun's shadow. Below 1 darkens shadowed areas' sky light too |
| `debugView` | 0 | 1 cascades (red, green, blue, yellow), 2 shadow mask, 3 normals; shown untonemapped |

Without an HDR map the environment is one colour, `environment_color` on
`sk.Engine` (`uniformEnvironmentColor` in C++), default (0.25, 0.28, 0.33).

## Passes

| Pass | Draws |
|---|---|
| `ShadowOpaque0..3`, `ShadowMasked0..3`, `ShadowSkinned0..3` | Shadow casters into cascade 0..3, each culled against its cascade |
| `GBufferOpaque`, `GBufferMasked`, `GBufferSkinned`, `GBufferSkinnedMasked` | The G-buffer; emission goes straight into the HDR target |
| `ShadowMask` | Cascades and contact shadows resolved per pixel, into `shadowMask` |
| `Lighting` | Lights, image-based light and the sky, added onto the emission |
| `Transparent` | Blended materials, back to front, sampling the cascades directly |
| `Tonemap` | To the swapchain |

Any pass can be turned off at runtime, for instance for a low-quality tier:
`engine.set_pass_enabled("ShadowMasked{cascade}", False)` turns off all four
masked cascade passes (a repeated pass answers to its declared name); masked
geometry then casts no shadow. Turning off `ShadowMask`
leaves it cleared to fully lit.

G-buffer layout: `gAlbedo` (RGBA8 sRGB: albedo, material occlusion),
`gNormal` (RG16F: octahedral world normal), `gMaterial` (RG8: metallic,
roughness), `gDepth` (D32; positions are reconstructed from it) and
`hdrColor` (RGBA16F).

## Changing it

Copy this directory and pass the copy's `pipeline.json` as
`pipeline_json_path`. Shader paths are relative to the JSON file, so the copy
works from any working directory. `shaders/` holds the GLSL beside the
compiled SPIR-V; after editing, recompile with
`sk.shaders.compile_dir("shaders")`. The `.glsl` files are included by the
stage shaders: `common.glsl` holds the buffer blocks, which must match the
`bufferLayouts` in `pipeline.json`.

Not here yet: ambient occlusion, bloom and automatic exposure (next), shadows
from point and spot lights, temporal anti-aliasing.
