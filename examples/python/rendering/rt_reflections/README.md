# Ray-traced reflections

The ray-traced reflections of Embark's [kajiya](https://github.com/EmbarkStudios/kajiya) renderer, written for this engine's frame graph, in Crytek Sponza. They sit on top of kajiya's diffuse global illumination, which is the [ReSTIR GI](../restir_gi/README.md) example's, with the [irradiance cache](../irradiance_cache/README.md). It runs on its own [pipeline](pipeline/pipeline.json) of compute passes that trace with ray queries. The scene, lights and camera are those of the [reference path tracer](../reference_pt/README.md), so the two can be compared. It needs a GPU with ray queries.

```
python rt_reflections.py
python rt_reflections.py --frames 300 --screenshot rr.png    # run 300 frames, save, quit
python rt_reflections.py --view 4 --exposure 8               # the reflections alone, brighter
python rt_reflections.py --walk 1.5                          # walk the camera down the nave
```

**Sponza:** the scene is Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it, or with `--scene colonnade`, the example builds a colonnade of boxes.

**kajiya's scenes:** `--scene viziers`, `gas_stations`, `battle`, `warhammer` or `conference` loads one of the scenes in Embark's kajiya repository, scaled and placed as kajiya places it, with a camera and a sun of its own; two lamps hang under the conference room's ceiling. `python tools/fetch_assets.py kajiya` fetches them into `assets/kajiya/`; their credits are in the [asset README](../../../../assets/README.md#not-committed).

## The technique

The specular lobe is the materials' BRDF's (`sk/pbr.glsl`'s `cookTorrance`): GGX of alpha = roughness², Schlick-GGX geometry, Schlick Fresnel. `specular.glsl` samples it through GGX's visible normals and keeps densities per projected solid angle, as kajiya does. Reflections use an alpha of at least 6e-4.

**Directional albedo.** `SpecLut` fills a 64 × 64 table over NdotV and roughness from 256 directions each:
- x and y such that the lobe reflects F0 · x + y of light from all directions;
- z, the fraction of drawn directions that are valid.

**Candidates.** At half resolution (the pixel of each 2 × 2 block the ReSTIR GI passes use), each pixel draws a direction from its surface's lobe. The draw is biased slightly away from grazing directions and repeated up to four times while invalid. At the surface the ray hits it gathers:
- the scene lights through shadow rays, with the hit's roughness pushed towards 1 by the reflecting surface's alpha, which keeps rough-smooth paths from making fireflies;
- the hit's emission;
- the hit's albedo times its indirect diffuse light: this frame's ReSTIR GI result when the hit is on screen and the same surface, the irradiance cache otherwise.

Past the scene the ray sees the sky. The pixel keeps the radiance, the hit, the normal there, the direction's density over the valid fraction, and the random state it started from.

**Validation.** Every frame, one pixel of each 2 × 2 group traces its reservoir's ray again, from where it started and with the random state it had. The new radiance replaces the old, and where they differ the reservoir forgets history. The group's other pixels take the new radiance where theirs was alike, and the same invalidity.

**Temporal resampling.** A pixel's reservoir starts from its candidate, weighed by luminance times the direction's density. It merges last frame's reservoir at the reprojected place, looking among the nearest pixels for the ray origin closest to its own. While its surface was not fully seen last frame, and until it holds 8 candidates, it merges up to four reprojected neighbours on a spiral too. A history sample is weighed by its luminance, its density and the jacobian of reaching its hit from here. It is dropped when the jacobian is far from 1, and counts for less where the view direction turned on smooth, flat surfaces.

**Resolve.** Up to eight reservoirs are picked in a world-space ellipse across the reflected lobe, sized by the lobe's width and the averaged distance to what is reflected. Each sample's radiance counts by a ratio estimator:
- the lobe's density towards its hit from here, over the density it was drawn with;
- the drawn density blends the sample's own lobe ("bent" by its half-vector angle) and this pixel's;
- a MIS weight against its ray's own density.

Neighbours with unlike normals or depths, and those whose hit lies below this surface, are left out. The result is divided by the lobe's directional albedo, so the filters work on the reflected light alone.

**Filters.** A temporal filter keeps two histories: one reprojected with the surface, one with the reflected image (the point the ray length puts behind the surface). Rough surfaces lean on the first. Smooth ones lean on whichever lies closer to the neighbourhood's mean. Both are softly clamped towards the 3 × 3 neighbourhood and blended over up to 16 frames. A spatial cleanup fills in where fewer than 8 frames are held.

**Image.** Direct light, plus the indirect diffuse irradiance times the diffuse albedo, plus the reflections times the lobe's directional albedo, tone mapped as in the default pipeline.

## Compared with kajiya

- The directional albedo table is computed for this engine's lobe. Kajiya's multiple-scattering energy compensation is left out, so the reflections match the reference path tracer's single-scattering BRDF.
- Every pixel traces its own reflection ray; kajiya reuses the diffuse rays on surfaces of alpha above 0.6.
- Hashes and the R2 sequence take the place of blue-noise tables; a generated spiral takes the place of the cleanup's offset table.
- History is fetched bilinearly over the texels whose depth matches, and averaged in linear colour.
- Ray origins are stored in world space; last frame's eye position comes from last frame's view-projection.
- The diffuse light is the ReSTIR GI example's, with its differences from kajiya.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **1** / **2** / **3** / **4** / **5** | Image / indirect diffuse light / irradiance cache / direct light / reflections |
| **T** | All stages, or none (the raw candidates) |
| **F** | Freeze the irradiance cache |
| **F2** | The panel on or off |
| **P** | Screenshot into `rt_reflections_captures/` |

The panel shows the GPU time and sets the exposure, the sky's intensity, the view, the stages (all or none) and the cache's freeze. **Reflections** shows the reflected light times the lobe's directional albedo, without the lights' direct specular light. The reference path tracer's `--mode 4` renders the same, converged.

## Options

| Option | Default | |
|---|---|---|
| `--scene NAME` | `sponza` | `sponza`, `colonnade`, or one of kajiya's scenes: `viziers`, `gas_stations`, `battle`, `warhammer`, `conference` |
| `--view 0` to `4` | 0 | Image, indirect diffuse light, irradiance cache, direct light, reflections |
| `--stages N` | 127 | The stages that run, a sum of: for the diffuse light 1 temporal resampling, 2 spatial resampling, 4 near-field split, 8 temporal filter, 16 spatial filter; for the reflections 32 temporal resampling and validation, 64 the temporal filter and cleanup |
| `--sky X` | 1.0 | Intensity of the sky's light |
| `--exposure X` | 1.0 | Multiplies the image before the tone curve |
| `--walk SPEED` | 0 | Walk the camera along the nave at SPEED metres a second; in kajiya's scenes, level towards what it looks at |
| `--frames N` | 300 | Frames before `--screenshot` |
| `--screenshot FILE` | | Save after `--frames` frames, print the GPU time, quit |
| `--width`, `--height` | 1600, 900 | |

## The pipeline

The [ReSTIR GI](../restir_gi/README.md#the-pipeline) example's passes come first, then, each one compute:

| Pass | Shader | Resolution | Work |
|---|---|---|---|
| `SpecLut` | `spec_lut.comp` | 64 × 64 | The lobe's directional albedo and valid fraction |
| `RtrTrace` | `rtr_trace.comp` | half | Candidates (`rtr_trace.glsl`) |
| `RtrValidate` | `rtr_validate.comp` | quarter | Reservoirs' rays traced again |
| `RtrTemporal` | `rtr_temporal.comp` | half | Temporal resampling |
| `RtrResolve` | `rtr_resolve.comp` | full | Ratio estimator; `rtrResolved`, `rtrRayLenOut` |
| `RtrTemporalFilter` | `rtr_temporal_filter.comp` | full | Two-history temporal filter |
| `RtrCleanup` | `rtr_cleanup.comp` | full | `reflections` |
| `CopyRtrHalf`, `CopyRtrFull` | `copy_rtr_half.comp`, `copy_rtr_full.comp` | half, full | This frame's results into the history images |
| `Display` | `display.frag` | | The image |

The reflections' history images are persistent:
- at half resolution, each pixel's sample: `rtrRadianceHistory` (rgb, 1 - cos theta of the half vector), `rtrRayOrigHistory` (origin, alpha), `rtrRayHistory` (hit - origin, density), `rtrRngHistory`, `rtrHitNormalHistory`, and its reservoir, `rtrReservoirHistory`;
- at full resolution, `rtrHistory` and `rtrRayLenHistory`.

Settings are `scene.custom.rr.*` values, read through the `Settings` uniform block, as in the ReSTIR GI example (`view` 4 shows the reflections; `stages` defaults to 127).

Shaders recompile with `sk.shaders.compile_dir("pipeline/shaders")`, which the example runs at start.
