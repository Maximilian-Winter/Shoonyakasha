# Reference path tracer

Crytek Sponza path traced: one path per pixel per frame, averaged while the camera stands still, so the image converges to the scene's light transport. That includes the sun through the open roof, two warm lamps in the nave, the sky, and light bouncing between the walls. It runs on its own [pipeline](pipeline/pipeline.json) of a compute pass that traces with ray queries and a pass that shows the average. It needs a GPU with ray queries.

```
python reference_pt.py
python reference_pt.py --samples 1024 --screenshot pt.png        # average 1024 paths per pixel, save, quit
python reference_pt.py --pipeline default --screenshot raster.png
```

**Sponza:** the scene is Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it, or with `--scene colonnade`, the example builds a colonnade of boxes.

**kajiya's scenes:** `--scene viziers`, `gas_stations`, `battle`, `warhammer` or `conference` loads one of the scenes in Embark's kajiya repository, scaled and placed as kajiya places it, with a camera and a sun of its own; two lamps hang under the conference room's ceiling. `python tools/fetch_assets.py kajiya` fetches them into `assets/kajiya/`; their credits are in the [asset README](../../../../assets/README.md#not-committed).

## The paths

Each frame, every pixel traces one path. It starts at the camera through a random point of the pixel, and at every surface it hits it:
- adds the surface's emission;
- adds the light of every scene light, through a shadow ray towards it (next event estimation);
- continues in a direction drawn from the BRDF: the diffuse lobe by its cosine, or the specular lobe through GGX's visible normals (Heitz 2018), chosen by how much each reflects;
- from the third bounce on, ends by Russian roulette with a chance that follows how much light it still carries, and carries more when it survives.

A path that leaves the scene adds the sky. The pixel keeps the running mean of its paths and how many there are. When the camera or a setting changes, the mean starts over.

The material and lights are the default pipeline's. The BRDF is `cookTorrance` from `sk/pbr.glsl`, and the lights are `lights.glsl`'s model: colour times intensity, the same attenuation, range window and spot cone. Directional lights are discs of angular radius `sunAngle`, and point and spot lights with a source radius are spheres. The sky is the scene environment's prefiltered map at its sharpest level. With `--pipeline default` the example shows the same scene on the default pipeline, with the same exposure and tone curve and without bloom, to compare against.

At a hit the shader reads the surface through [`sk/scene_hits.glsl`](../../../../python/shoonyakasha/glsl/sk/scene_hits.glsl):
- the triangle's vertices: position, normal, uv and colour;
- the glTF material: base colour, metallic-roughness, emission and normal map, sampled at a level of detail from a ray cone.

The engine fills that data from the acceleration structure every frame (see [pipeline JSON: acceleration structures](../../../../docs/reference/pipeline-json.md#descriptors-and-samplers)). The structure holds the static, opaque, shadow-casting meshes. Rays pass through alpha-tested materials, such as Sponza's plants, and through skinned meshes.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera; moving starts the average over |
| **1** / **2** / **3** | Everything / indirect light only / white furnace |
| **R** | Start the average over |
| **F2** | The panel on or off |
| **P** | Screenshot into `reference_pt_captures/` |

The panel shows the paths per pixel and sets the bounces, the exposure and the sky's intensity. Exposure changes only the display, so the average carries on.

The views:
- **Indirect only:** what reaches the camera after at least one bounce. Leaves out the light and emission seen at the first hit.
- **White furnace:** every surface white and Lambertian, shaded with its triangle's normal, under a sky of 1, with up to 64 bounces. A path loses nothing at a bounce, so the image converges to 1 everywhere, shown as mid grey (the view writes half the value). Where light needs more than 64 bounces to get out, as under Sponza's covered arcades, the image stays darker. The colonnade converges to 1 within the 64.
- **Indirect irradiance** (`--mode 3` only): the first surface is white and Lambertian, without its emission or the scene lights' direct light, so each pixel converges to the light that reaches its surface from the rest of the scene and the sky, over pi. Pixels showing the sky are black. This is what the [ReSTIR GI](../restir_gi/README.md) example's **Indirect** view estimates.
- **Reflections** (`--mode 4` only): only the first surface's specular lobe (`cookTorrance`'s specular term, drawn from its own lobe) reflects, without its emission or the scene lights' direct light, so each pixel converges to the light the surface reflects from the rest of the scene and the sky. Pixels showing the sky are black. This is what the [ray-traced reflections](../rt_reflections/README.md) example's **Reflections** view estimates.

## Options

| Option | Default | |
|---|---|---|
| `--pipeline pt` / `default` | `pt` | The path tracer, or the scene on the default pipeline |
| `--scene NAME` | `sponza` | `sponza`, `colonnade`, or one of kajiya's scenes: `viziers`, `gas_stations`, `battle`, `warhammer`, `conference` |
| `--bounces N` | 8 | Bounces after the first hit, 1 to 16; 1 is direct light only |
| `--mode 0` to `4` | 0 | Everything, indirect only, white furnace, indirect irradiance, reflections |
| `--sky X` | 1.0 | Intensity of the sky's light, on both pipelines |
| `--exposure X` | 1.0 | Multiplies the image before the tone curve, on both pipelines |
| `--samples N` | 0 | Paths per pixel to stop at; 0 never stops |
| `--screenshot FILE` | | Save once `--samples` paths are in, or after `--settle` seconds without `--samples`, then quit |
| `--settle S` | 3.0 | |
| `--width`, `--height` | 1600, 900 | |

## The pipeline

| Pass | Work |
|---|---|
| `PathTrace` | Compute, 8 × 8 pixels per workgroup over the render size (`pathtrace.comp`). Reads and writes `accumulation`, an `R32G32B32A32_SFLOAT` persistent image: rgb the mean, a the number of paths. Requires `rayQuery` |
| `Display` | The mean times the exposure, tone mapped to the swapchain (`display.frag`) |
| `UIOverlay` | The panel |

Settings are `scene.custom.pt.*` values, read through the `PathTracer` uniform block:

| Setting | Default | Meaning |
|---|---|---|
| `reset` (uint) | 0 | 1 starts the average over in that frame |
| `maxBounces` (uint) | 8 | Bounces after the first hit, at most 16 |
| `mode` (uint) | 0 | 0 everything, 1 indirect only, 2 white furnace, 3 indirect irradiance, 4 reflections |
| `toneMapper` (uint) | 2 | 2 AgX, 1 Khronos PBR Neutral, 0 ACES, as in the default pipeline |
| `exposure` | 1.0 | Multiplies the mean before the tone curve |
| `environmentIntensity` | 1.0 | Multiplies the sky |
| `sunAngle` | 0.27 | Angular radius of directional lights, in degrees |
| `maxSamples` | 0 | Paths per pixel to stop at; 0 never stops |

The pass reads the scene through these bindings:
- `SceneLights`: `"source": {"type": "scene", "data": "lights"}`.
- `RayInstances`: `"source": {"type": "scene", "data": "rayInstances"}`, 16,384 rows.
- `sceneTextures`: `"source": "sceneTextures"`, 4,096 slots.
- `scene`: the acceleration structure.
- `iblSet`: the environment's maps, which the engine binds by that name.

Shaders recompile with `sk.shaders.compile_dir("pipeline/shaders")`, which the example runs at start.
