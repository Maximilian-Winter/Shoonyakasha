# Using the default pipeline

The engine ships a complete renderer, the *default pipeline*, and uses it whenever you don't name a pipeline of your own. It is a clustered deferred PBR renderer for glTF scenes, with:
- cascaded sun shadows, optionally ray traced;
- spot and point light shadows;
- ambient occlusion and image-based light;
- temporal anti-aliasing, bloom and automatic exposure.

It needs no shaders or JSON from you. This guide covers what a scene needs to look right with it, the knobs worth knowing, and how to make a changed copy of it. Its [README](../../python/shoonyakasha/pipelines/default/README.md) is the reference: every setting, pass and preset, and the G-buffer layout.

## A first scene

```python
import shoonyakasha as sk

# No pipeline_json_path: the default pipeline.
engine = sk.Engine(title="First scene", width=1280, height=720,
                   hdr_environment_path="env/farm_sunset_1k.hdr")


def on_init():
    engine.create_camera(pos=(0.0, 1.5, 5.0), fov=60.0, near_plane=0.05, far_plane=200.0)

    sun = engine.create_directional_light(direction=(-0.45, -0.55, 0.7), intensity=3.0)
    engine.scene.set_light_cast_shadows(sun, True)
    engine.set_sun_shadows(cascades=4, max_distance=40.0)

    engine.load_gltf_scene("models/Box.gltf")


engine.set_on_init(on_init)
engine.run()
```

That gives a lit, anti-aliased and tone-mapped box against the environment. With something under it, the sun's shadow falls there too: [shrine_default.py](../../examples/python/rendering/japanese_shrine/shrine_default.py) makes a ground by scaling the same box. The same from C++ leaves `EngineConfig::pipelineJsonPath` empty; the rest is the facade calls of the same names (`createCamera`, `createDirectionalLight`, `setLightCastShadows`, `setSunShadowSettings`, `loadGltfScene`). [examples/cpp/rendering/default_pipeline](../../examples/cpp/rendering/default_pipeline/main.cpp) is a complete program. Python finds the pipeline inside the installed package, `shoonyakasha.pipeline.DEFAULT`. C++ finds it through `$SHOONYAKASHA_DEFAULT_PIPELINE`, or else in the source tree the engine was built from; a C++ program installed elsewhere sets the variable.

## What a scene needs

**A camera.** Create one first: without a main camera the log warns and there is no view to render. Keep `near_plane` as large as the scene allows (0.05 to 0.1 for human-scale scenes): depth precision, contact shadows and ambient occlusion all depend on it.

**Light.** Every directional, point and spot light counts, up to 128. Point and spot lights are sorted into screen clusters by their `range`, so give each one a range that fits: a lamp with a range of 50 lights, and costs, far more of the screen than one of 8. Image-based light comes from the HDR environment (`hdr_environment_path`), or from a single colour (`environment_color`) when there is none; the environment is also drawn as the sky.

**glTF materials.** Metallic-roughness materials are used as glTF defines them:
- **Textures:** base colour, normal, metallic-roughness, occlusion and emissive.
- **Alpha mode:** `OPAQUE` goes into the G-buffer. `MASK` is alpha-tested, in the G-buffer and in every shadow. `BLEND` is shaded forward over the lit scene, back to front, and receives shadows but casts none.
- **Changes at runtime:** `set_material_vec4(entity, "baseColorFactor", ...)`, `set_material_float(entity, "roughnessFactor", ...)` and the other factors take effect on the next frame.

**Animated models** render once loaded with `load_gltf_scene(path, load_skins=True, load_animations=True)` and played with `engine.scene.play_animation(entity, clip)`. They are skinned in the G-buffer and in the shadows, and carry motion vectors, so TAA follows them.

Not drawn by this pipeline: sprites, UI panels and text. Those need a pipeline with a `sprite_geometry` pass ([Sprites, UI and text](sprites-ui-text.md)).

## Lights and shadows

**The sun.** The first directional light whose shadow flag is on is the sun, and gets four cascades of 2048² fitted to the camera every frame. `max_distance` is how far from the camera they reach. Use the smallest distance that covers what matters, because the same texels are spread over all of it: 30 to 60 units for a scene you walk through, more for a landscape seen from above. Leave `resolution` at 2048, the size the pipeline declares. Contact shadows add the fine detail near where objects touch, which the cascades are too coarse for.

**Spot and point lights** cast shadows when their flag is on:

```python
lamp = engine.create_point_light(pos=(0.0, 1.2, 0.0), color=(1.0, 0.6, 0.3), intensity=12.0, range=10.0)
engine.scene.set_light_cast_shadows(lamp, True)

spot = engine.create_point_light(pos=(0.0, 4.0, 3.0), intensity=40.0, range=14.0)
engine.scene.set_light_type(spot, sk.LIGHT_SPOT)
engine.scene.set_rotation(spot, (-1.1, 0.0, 0.0))   # it shines along its forward axis: pitched down here
engine.scene.set_light_cast_shadows(spot, True)
```

At most 8 spot and 4 point lights get a shadow in any frame. Every frame the engine drops lights whose range can't reach the view, ranks the rest by brightness and nearness, and hands out the slots in that order, so more shadowed lights than that is fine: the ones that matter most get shadows. All the maps share one 4096² atlas. A light's tile grows as the camera nears it: up to 2048² for a spot light and 1024² per cube face for a point light, down to 128² far away. Spot lights have a 45° cone (`LightComponent::outerCone`); there is no Python setter for it yet.

## Quality presets and ray-traced shadows

A preset switches a group of passes and settings in one call:

```python
engine.apply_pipeline_preset("medium")
```

| Preset | What it's for |
|---|---|
| `high` | Everything on. What the pipeline starts with |
| `medium` | High without contact shadows |
| `low` | Integrated or older GPUs: no ambient occlusion, bloom, contact shadows, or shadows from alpha-tested geometry |
| `raytraced` | High, with the sun's shadows ray traced where the GPU supports ray queries |

Call it from `on_init` or at any time after, for instance from a key or a settings menu. A preset only touches what it names, so your own settings that it doesn't name survive it.

`raytraced` traces one ray per pixel towards a random point on the sun's disc, and TAA averages the rays over frames. Shadows come out sharp where an object meets the ground and softer farther from it, as real ones do, and there's no shadow-map resolution to run out of. It needs `VK_KHR_ray_query`. Check first:

```python
def on_init():
    if engine.ray_query_supported():
        engine.apply_pipeline_preset("raytraced")
    engine.set_custom_float("default.sunAngle", 0.27)   # the real sun; larger is softer
```

On a GPU without ray queries the preset is refused with a warning and nothing changes. `ray_query_supported()` answers from `on_init` on. The rays see static opaque geometry. Skinned and alpha-tested meshes keep casting through the cascades, and the two shadows are combined, so nothing loses its shadow. The noise needs TAA: with `taa` off, ray-traced shadow edges look grainy. Set `SHOONYAKASHA_DISABLE_RAY_QUERY=1` to try a program as it runs on a GPU without them.

## Settings

The pipeline's settings are `scene.custom.default.*` values with defaults in `pipeline.json`, so set only the ones you want to change, from `on_init` or any time later:

```python
engine.set_custom_float("default.exposure", 1.5)    # one stop brighter is 2.0
engine.set_custom_uint("default.autoExposure", 0)   # fixed exposure instead of adapting
```

The ones most worth knowing, by symptom:

| You see | Change |
|---|---|
| The image is too dark or too bright overall | `exposure` (compensation on top of automatic exposure); `exposureMinEV` if a dark scene is being brightened more than you want |
| Brightness drifts as the camera moves | That's automatic exposure adapting. Slow it with `exposureAdaptSpeed`, or turn it off with `autoExposure` 0 and set `exposure` yourself |
| Speckled or striped self-shadowing ("acne") on lit surfaces | Raise `shadowNormalBias` |
| Shadows detach from the objects casting them | Lower `shadowNormalBias` |
| Sun shadows too sharp or too blurry | `shadowSoftness`; with `raytraced`, `sunAngle` |
| Corners and crevices too dark, or not dark enough | `aoIntensity`, `aoRadius` |
| Bright highlights glow too much | `bloomIntensity` (0 turns bloom off) |
| The sky is too detailed behind the scene | `skyBlur` |
| Sky light too strong or too weak | `iblIntensity`; `shadowAmbient` below 1 also darkens sky light in the sun's shadow |

The [README](../../python/shoonyakasha/pipelines/default/README.md#settings) lists all of them. Turning a feature off by its setting is usually better than turning off its passes: a disabled pass still clears its output, but other passes may read it. For example, bloom's mip chain also feeds automatic exposure, which is why bloom is turned off with `bloomIntensity`, not by disabling its passes. Passes that are safe to turn off with `engine.set_pass_enabled(name, False)`: `GTAO` (no ambient occlusion), `ShadowMask` (no sun shadows), and the shadow passes, by their declared names such as `"ShadowMasked{cascade}"`.

## Debug views

`engine.set_custom_uint("default.debugView", n)` replaces the image, bypassing tonemapping, bloom and exposure:

| n | Shows | Look for |
|---|---|---|
| 1 | Sun cascades, tinted red, green, blue, yellow | Where each cascade starts; a distant object in red means `max_distance` is too short for the view |
| 2 | Sun shadow mask | Shadows on their own, without light or texture |
| 3 | World normals | Normal maps the wrong way round, flipped faces |
| 4 | Ambient occlusion | Too much or too little darkening, halos around objects |
| 5 | Point and spot lights per screen cluster: blue none, green 8, red 16 or more | Lights whose range is much larger than they need |
| 6 | Motion vectors, hue for direction | Moving objects that TAA smears: they should show colour where they move |

`sponza.py` in [examples/python/rendering/sponza](../../examples/python/rendering/sponza) cycles through them with V, and applies presets with 1 to 4.

## Performance

- **Cost, from most to least:** local shadows (every shadowed light that's in use re-renders its casters, up to six times for a point light), sun cascades (four renders of the casters), then ambient occlusion and the screen-space passes, then TAA, bloom and exposure.
- **Trimming shadows:** turn off `set_light_cast_shadows` on lights the viewer won't miss them for, and shorten ranges. The `low` preset drops alpha-tested casters, which are often foliage and cost the most.
- **Memory:** the atlas takes 64 MB, the cascades another 64 MB, and the screen-sized targets about 60 bytes per pixel.
- **Push constants:** the pipeline pushes 180 bytes of per-draw constants. Every desktop GPU allows 256. A device limited to Vulkan's minimum of 128 logs the G-buffer passes as not fitting, and they draw nothing.

## Making it your own

When settings aren't enough, copy the pipeline and change the copy:

```python
import shutil
import shoonyakasha as sk

shutil.copytree(sk.pipeline.DEFAULT.parent, "my_pipeline")
engine = sk.Engine(title="Mine", pipeline_json_path="my_pipeline/pipeline.json")
```

- **Shader paths:** they are relative to the JSON file, so the copy works from any directory.
- **Shaders:** `shaders/` holds the GLSL beside the compiled SPIR-V. After editing, recompile with `sk.shaders.compile_dir("my_pipeline/shaders")` (needs `glslc`). `common.glsl` declares the buffer blocks, which must match the `bufferLayouts` in `pipeline.json`. The engine checks every shader against the JSON when it loads and names any mismatch.
- **Validation:** `sk.pipeline.check("my_pipeline/pipeline.json")` reads the JSON without starting the engine and raises on problems.
- **Engine updates:** your copy doesn't follow changes to the default pipeline. When the engine changes it, compare the two directories to pick the changes up.

**A worked example: distance fog.** A fullscreen pass after `Lighting` blends a fog colour over the lit opaque scene, more of it the farther each pixel is. It needs a shader, a buffer layout that reads the fog's parameters from `scene.custom`, a descriptor set, and the pass.

`my_pipeline/shaders/fog.frag`:

```glsl
#version 450
#include "common.glsl"

layout(set = 0, binding = 0) uniform sampler2D gDepth;
layout(set = 0, binding = 1) uniform Fog {
    vec4 color;
    float density;
} fog;
layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

void main() {
    float depth = textureLod(gDepth, fragTexCoord, 0.0).r;
    if (depth >= 1.0) discard;   // leave the sky alone
    float distance = length(viewPositionFromDepth(camera.invProj, fragTexCoord, depth));
    outColor = vec4(fog.color.rgb, 1.0 - exp(-fog.density * distance));
}
```

In `pipeline.json`, add a buffer layout next to the others in `bufferLayouts`:

```json
{ "bufferLayouts": {
    "FogSettings": {
      "usage": "uniform_buffer", "packing": "std140", "updateFrequency": "per_frame",
      "fields": [
        { "name": "color",   "type": "vec4",  "source": "scene.custom.fog.color",   "default": [0.62, 0.66, 0.72, 1.0] },
        { "name": "density", "type": "float", "source": "scene.custom.fog.density", "default": 0.04 }
      ]
    } } }
```

a descriptor set to `descriptorSetLayouts`:

```json
{ "descriptorSetLayouts": {
    "fogSet": {
      "bindings": [
        { "binding": 0, "type": "combined_image_sampler", "stages": ["fragment"], "name": "gDepth",
          "autoBindResource": "gDepth", "autoBindSampler": "nearestClamp" },
        { "binding": 1, "type": "uniform_buffer", "stages": ["fragment"], "name": "Fog",
          "autoBindBuffer": "FogSettings" }
      ]
    } } }
```

and the pass to `passes`, right after `Lighting`:

```json
{
  "name": "Fog",
  "type": "graphics",
  "execution": { "type": "fullscreen" },
  "inputs": [ { "resource": "gDepth", "usage": "shader_read" } ],
  "outputs": [ { "resource": "hdrColor", "usage": "color_blend" } ],
  "pipeline": {
    "vertexShader": "shaders/fullscreen.vert.spv", "fragmentShader": "shaders/fog.frag.spv",
    "depthTest": false, "depthWrite": false, "cullMode": "none", "vertexInput": "none",
    "blending": "alpha"
  },
  "descriptorSets": ["fogSet", "cameraSet"]
}
```

Compile the shaders, and the fog is there with its defaults. The application changes it like any other setting:

```python
engine.set_custom_float("fog.density", 0.08)
engine.set_custom_vec4("fog.color", (0.8, 0.7, 0.6, 1.0))
```

Some things this example shows about the pipeline:
- **Pass order:** passes that write the same image run in the order they're declared. Declaring `Fog` after `Lighting` and before `Transparent` fogs the opaque scene, and blended materials are drawn over it unfogged. They'd need the same formula in `forward_body.glsl`.
- **Parameters:** the fog's settings are plain dot-paths. Any `scene.custom` key a buffer field names becomes a setting, with the field's `default` until the application sets it.
- **Descriptor sets:** the list order gives the set numbers the shader uses: `fogSet` is set 0, `cameraSet` set 1.

## Troubleshooting

| Problem | Likely cause |
|---|---|
| Everything is black except the sky | No lights, or only lights whose range doesn't reach the scene. Check the sky light too: `iblIntensity`, and the environment map |
| A directional light casts no shadow | Its shadow flag is off, another directional light was flagged first, or what should be shadowed is beyond `max_distance` (debug view 1) |
| A spot or point light casts no shadow | Its flag is off; more than 8 spot or 4 point lights are flagged and it ranked lower; or its range doesn't reach the view |
| Blended glass casts no shadow | Blended materials receive shadows but don't cast them. Use `MASK` for things like leaves and fences |
| Shadows shimmer on distant objects | They're in the last cascade with few texels per unit. Shorten `max_distance`, or raise `split_lambda` so more of the resolution goes near the camera |
| `raytraced` does nothing | The GPU has no ray queries (see the warning in the log), or `SHOONYAKASHA_DISABLE_RAY_QUERY` is set |
| A copied pipeline fails to load after an edit | The log names the pass, shader and member whose declaration doesn't match the JSON. Recompile the shaders and run `sk.pipeline.check` |
| Moving objects leave a faint trail | Blended objects have no motion vectors of their own and rely on TAA's clipping. An opaque one should show colour in debug view 6 where it moves; if it doesn't, it's being moved some other way than its transform or skeleton |

## More

- [Default pipeline README](../../python/shoonyakasha/pipelines/default/README.md): every setting, preset, pass and resource.
- [Lighting and IBL](lighting-and-ibl.md): the sun cascades, local light shadows and ray-traced shadows from the engine's side, with their dot-paths.
- [Pipeline JSON reference](../reference/pipeline-json.md) and [walkthrough](json-render-pipeline.md), for changing a copy.
- Examples: [alley.py](../../examples/python/rendering/alley/alley.py), a tech demo with a dusk from day to night, [bistro.py](../../examples/python/rendering/bistro/bistro.py), the same on a 2.8-million-triangle street with 96 lights, [sponza.py](../../examples/python/rendering/sponza/sponza.py), [shrine_default.py](../../examples/python/rendering/japanese_shrine/shrine_default.py), and the C++ [default_pipeline](../../examples/cpp/rendering/default_pipeline/main.cpp).
