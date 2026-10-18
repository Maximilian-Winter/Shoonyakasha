# ReSTIR GI

The ray-traced diffuse global illumination of Embark's [kajiya](https://github.com/EmbarkStudios/kajiya) renderer, written for this engine's frame graph, in Crytek Sponza. It runs on its own [pipeline](pipeline/pipeline.json) of compute passes that trace with ray queries. Kajiya's [irradiance cache](../irradiance_cache/README.md) supplies the light beyond the first bounce. The scene, lights and camera are those of the [reference path tracer](../reference_pt/README.md), so the two can be compared. It needs a GPU with ray queries.

```
python restir_gi.py
python restir_gi.py --frames 300 --screenshot gi.png    # run 300 frames, save, quit
python restir_gi.py --walk 1.5                          # walk the camera down the nave
python restir_gi.py --view 1 --stages 0                 # the raw candidates' indirect light
```

**Sponza:** the scene is Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it, or with `--scene colonnade`, the example builds a colonnade of boxes.

**kajiya's scenes:** `--scene viziers`, `gas_stations`, `battle`, `warhammer` or `conference` loads one of the scenes in Embark's kajiya repository, scaled and placed as kajiya places it, with a camera and a sun of its own; two lamps hang under the conference room's ceiling. `python tools/fetch_assets.py kajiya` fetches them into `assets/kajiya/`; their credits are in the [asset README](../../../../assets/README.md#not-committed).

## The technique

**G-buffer.** A camera ray through each pixel's centre finds the surface the pixel shows. The pass stores its position, view depth, normals and material, and the light it sends straight to the camera: the scene lights through shadow rays (towards the centre of area lights) and its emission.

**Reprojection.** Only the camera moves, so a surface's place on screen last frame comes from its position and last frame's view-projection. It counts as seen last frame when last frame's depth there agrees within 5%. Last frame's filtered indirect light is fetched there, from the texels whose depth matches.

**Candidates.** At half resolution, each pixel stands for one pixel of its 2 × 2 block, a different one each frame, and traces one ray in a direction uniform over the hemisphere around its normal. At the surface the ray hits it gathers:
- the scene lights through shadow rays;
- the surface's emission;
- the surface's albedo times either last frame's indirect light there (when the hit is on screen and was seen last frame) or the irradiance cache.

Past the scene a ray sees the sky. The ray's radiance, hit point and the normal there are kept.

**Validation.** Every third frame, instead of full-length rays (the pixels then trace only short ones, for the near field), each reservoir's sample is traced again from where it was first traced:
- where the radiance changed, the pixel's history counts for less;
- where the ray still hits about the same point, the sample takes the new radiance, with its brightness allowed to grow at most tenfold, through the reservoir's weight.

The changes are blurred, carried over in time, and raised where the camera just revealed surfaces.

**Temporal resampling.** Each pixel's reservoir starts from its candidate. It then merges last frame's reservoir at its reprojected place, and up to four neighbours picked through a permutation of the pixels that changes every frame, until it holds about 25 candidates. A history sample counts by:
- its luminance (zero below the surface);
- how alike its surface is in depth and normal;
- a jacobian for being seen from here rather than from where it was traced: squared distance, and how the hit faces this point.

At most 20 candidates of history count, fewer where validation found changes.

**Spatial resampling.** Two passes merge the reservoirs of neighbours on a golden-angle spiral:
- 8 neighbours within 32 to 12 pixels, then 5 within 16 to 6, the radius narrowing as a reservoir fills;
- each sample weighed by its luminance times the cosine from here, the surfaces' similarity, and the square root of the jacobian;
- the second pass also marches each sample's direction through the depth buffer, for occlusion near the surface.

**Resolve.** Full resolution indirect irradiance comes from four half resolution pixels on a tiny spiral, weighed by how alike their surfaces are. Near hits, within about 40 pixels' worth of distance at the surface's depth, come from the raw candidates, which keeps contact light in corners. Far hits come from the reservoirs.

**Filters.** A temporal filter clamps the reprojected history to the neighbourhood's spread and averages up to 32 frames, fewer where the light changes. A spatial filter then blurs what the temporal filter has not yet settled.

**Image.** The direct light plus the indirect irradiance times the diffuse albedo (what Fresnel and metalness leave of it), tone mapped as in the default pipeline. Specular light that bounced is not in the image.

## Compared with kajiya

- Camera rays find the G-buffer by ray queries, at pixel centres; nothing is rasterized and there is no temporal anti-aliasing.
- No ambient occlusion steers the kernels: the spatial passes' kernels narrow only as reservoirs fill, and the resolve takes the near field everywhere.
- History images are written to separate images each frame and copied back at its end.
- The invalidity blur has no wave intrinsics.
- Positions are stored in the G-buffer; depth comparisons use linear view depth.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **1** / **2** / **3** / **4** | Image / indirect light / irradiance cache / direct light only |
| **T** | All stages, or none (the raw candidates) |
| **F** | Freeze the irradiance cache |
| **F2** | The panel on or off |
| **P** | Screenshot into `restir_gi_captures/` |

The panel shows the GPU time and sets the exposure, the sky's intensity, the view, the stages (all or none) and the cache's freeze. The views:
- **Indirect:** the indirect irradiance each visible surface receives, before its albedo.
- **Cache:** the irradiance cache looked up at each visible surface, in place of ReSTIR.
- **Direct:** the scene lights and emission only.

## Options

| Option | Default | |
|---|---|---|
| `--scene NAME` | `sponza` | `sponza`, `colonnade`, or one of kajiya's scenes: `viziers`, `gas_stations`, `battle`, `warhammer`, `conference` |
| `--view 0` / `1` / `2` / `3` | 0 | Image, indirect light, irradiance cache, direct light only |
| `--stages N` | 31 | The stages that run, a sum of: 1 temporal resampling, 2 spatial resampling, 4 near-field split, 8 temporal filter, 16 spatial filter |
| `--sky X` | 0.5 in kajiya's scenes, 1.0 in Sponza | Intensity of the sky's light |
| `--walk SPEED` | 0 | Walk the camera along the nave at SPEED metres a second; in kajiya's scenes, level towards what it looks at |
| `--frames N` | 300 | Frames before `--screenshot` |
| `--screenshot FILE` | | Save after `--frames` frames, print the GPU time, quit |
| `--width`, `--height` | 1600, 900 | |

The reference path tracer's `--mode 3` renders the same indirect irradiance as the **Indirect** view, converged, to compare against.

## The pipeline

The irradiance cache's passes come first, as in its [example](../irradiance_cache/README.md). Then, each one compute:

| Pass | Shader | Resolution | Work |
|---|---|---|---|
| `GBuffer` | `gbuffer.comp` | full | Camera rays: `gPosition`, `gNormal`, `gGeoNormal`, `gAlbedo`, `gDirect` |
| `Reproject` | `reproject.comp` | full | `reproj`: last frame's uv and whether it saw the surface |
| `GiReproject` | `gi_reproject.comp` | full | `reprojectedGi`: last frame's indirect light there |
| `RtdgiValidate` | `rtdgi_validate.comp` | half | Validation frames: trace the reservoirs' samples again |
| `RtdgiTrace` | `rtdgi_trace.comp` | half | New candidates |
| `ValidityIntegrate` | `validity_integrate.comp` | half | How far history can be trusted |
| `RestirTemporal` | `restir_temporal.comp` | half | Temporal resampling |
| `RestirSpatial0`, `RestirSpatial1` | `restir_spatial0.comp`, `restir_spatial1.comp` | half | Spatial resampling (`restir_spatial.glsl`) |
| `RestirResolve` | `restir_resolve.comp` | full | Indirect irradiance from the reservoirs and the near field |
| `TemporalFilter` | `temporal_filter.comp` | full | |
| `SpatialFilter` | `spatial_filter.comp` | full | `indirect` |
| `CopyHistoryHalf`, `CopyHistoryFull` | `copy_history_half.comp`, `copy_history_full.comp` | half, full | This frame's results into the history images |
| `Display` | `display.frag` | | Direct plus indirect light, tone mapped |
| `UIOverlay` | | | The panel |

Every pass binds its images as storage images in a descriptor set of its own, and declares them as inputs or outputs.

The images that carry history are persistent:
- at half resolution, each pixel's sample (`radianceHistory`, `rayOrigHistory`, `rayHistory`, `hitNormalHistory`), its reservoir (`reservoirHistory`, `R32G32_UINT`: payload, then M and W as halves) and `invalidityHistory`;
- at full resolution, `giHistory`, `varianceHistory` and `prevDepth`.

A reservoir's payload is the half resolution pixel that holds its sample.

Shaders shared between passes:
- `common.glsl`: the camera, the settings, the half resolution layout and the validation frames;
- `rtdgi_trace.glsl`: one diffuse ray, for candidates and validation;
- `lighting.glsl`, `ircache.glsl` and `ircache_trace.glsl`: the irradiance cache example's.

Settings are `scene.custom.rg.*` values, read through the `Settings` uniform block:

| Setting | Default | Meaning |
|---|---|---|
| `view` (uint) | 0 | 0 image, 1 indirect light, 2 irradiance cache, 3 direct light only |
| `toneMapper` (uint) | 2 | 2 AgX, 1 Khronos PBR Neutral, 0 ACES |
| `freeze` (uint) | 0 | 1 freezes the irradiance cache |
| `stages` (uint) | 31 | The stages that run (see `--stages`) |
| `exposure` | 1.0 | Multiplies the image before the tone curve |
| `environmentIntensity` | 1.0 | Multiplies the sky |
| `sunAngle` | 0.27 | Angular radius of directional lights, in degrees, for rays past the G-buffer |

Shaders recompile with `sk.shaders.compile_dir("pipeline/shaders")`, which the example runs at start.
