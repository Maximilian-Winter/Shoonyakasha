# Irradiance cache

The irradiance cache of Embark's [kajiya](https://github.com/EmbarkStudios/kajiya) renderer, written for this engine's frame graph, lighting Crytek Sponza. It runs on its own [pipeline](pipeline/pipeline.json) of compute passes that trace with ray queries. The scene, lights and camera are those of the [reference path tracer](../reference_pt/README.md), so the two can be compared. It needs a GPU with ray queries.

```
python irradiance_cache.py
python irradiance_cache.py --frames 300 --screenshot ic.png    # run 300 frames, save, quit
python irradiance_cache.py --walk 1.5                          # walk the camera down the nave
```

**Sponza:** the scene is Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it, or with `--scene colonnade`, the example builds a colonnade of boxes.

**kajiya's scenes:** `--scene viziers`, `gas_stations`, `battle`, `warhammer` or `conference` loads one of the scenes in Embark's kajiya repository, scaled and placed as kajiya places it, with a camera and a sun of its own; two lamps hang under the conference room's ceiling. `python tools/fetch_assets.py kajiya` fetches them into `assets/kajiya/`; their credits are in the [asset README](../../../../assets/README.md#not-committed).

## The cache

**Cells.** Space around the camera is cut into 12 cascades of 32 × 32 × 32 cells. Cascade 0's cells are 2 cm wide, and each further cascade's are twice as wide. Every cascade is centred on the camera in whole cells of its own size and scrolls with it. A cell has an entry only once something looks it up, out of 65,536 entries in all.

**Lookups.** A point looks up the cell of the smallest cascade that holds it, half a cell out along its normal, so the two sides of a thin wall fall in different cells. Each lookup does three things:
- allocates the cell's entry, if it has none;
- keeps the entry alive;
- votes for the entry's next trace origin: a point a little towards where the lookup came from. Every vote that counts has the same chance of winning.

**Ranks and freeing.** A lookup from a camera ray has rank 0. One from a ray of a rank-0 entry has rank 1, and so on; from rank 3 on, lookups read entries but do not allocate them. An entry's life counts frames: a lookup sets it back to four times its rank, if that is lower, and the entry is freed when it reaches 12. That is 12 frames without a lookup for an entry camera rays use, 8 for rank 1 and 4 for rank 2. An entry counts votes only from lookups of its own rank or lower. Rays from entries outside a building, bounces away from the camera, therefore cannot move the trace origin of an entry the camera sees from inside. Only camera rays allocate cells that scrolled in this frame.

**Samples.** Each entry keeps 16 samples of the light arriving at its trace origin, one in each cell of a 4 × 4 octahedral map of the sphere. Each frame, for every live entry:
- **Validation:** 4 rays trace again the samples their strata hold, from where those were traced. A sample whose radiance changed by more than a tenth makes its reservoir trust its history less.
- **New samples:** 4 rays, in every fourth stratum in turn, find new samples. Each new sample competes with the stratum's old one, weighted by luminance. The old one counts for at most 30 candidates, so the cache keeps following changes in the light.
- **Accessibility:** a stratum whose old sample was traced from a point the entry's new origin cannot see keeps less of its history.

**What a ray adds.** At the surface a ray hits, it adds:
- the scene lights, through shadow rays;
- the surface's emission;
- its albedo times the cache looked up there, one rank further out.

The last term is how light keeps bouncing. Past the scene, a ray adds the sky.

**Irradiance.** The 16 samples are summed into L1 spherical harmonics per colour, a quarter new each frame. A lookup reads them with Geomerics' reconstruction, which stays positive. Rays of the cache read the samples themselves instead.

**The image.** Each pixel traces one camera ray through a random point of itself and adds:
- the scene lights through shadow rays;
- the surface's emission;
- its diffuse albedo (what Fresnel and metalness leave of it) times the cache.

The frames are averaged while the camera stands still, a new frame weighing at least a tenth. Specular light that bounced is not in the image.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **1** / **2** / **3** / **4** | Image / the cache alone / its entries / direct light only |
| **F** | Freeze the cache: it stops scrolling, aging and tracing, and lookups still allocate and vote |
| **R** | Start the image's average over |
| **F2** | The panel on or off |
| **P** | Screenshot into `irradiance_cache_captures/` |

The panel shows the GPU time and sets the exposure, the sky's intensity, the view and the freeze. The views:
- **Cache:** the irradiance the cache gives each visible point, before the albedo.
- **Entries:** each entry in its own colour, black where a cell has none.
- **Direct:** the scene lights and emission only, without the cache.

## Options

| Option | Default | |
|---|---|---|
| `--scene NAME` | `sponza` | `sponza`, `colonnade`, or one of kajiya's scenes: `viziers`, `gas_stations`, `battle`, `warhammer`, `conference` |
| `--view 0` / `1` / `2` / `3` | 0 | Image, cache, entries, direct light only |
| `--sky X` | 1.0 | Intensity of the sky's light |
| `--walk SPEED` | 0 | Walk the camera along the nave at SPEED metres a second; in kajiya's scenes, level towards what it looks at |
| `--frames N` | 300 | Frames before `--screenshot` |
| `--screenshot FILE` | | Save after `--frames` frames, print the GPU time, quit |
| `--width`, `--height` | 1600, 900 | |

## The pipeline

Passes in order, each one compute:

| Pass | Shader | Work |
|---|---|---|
| `IrcacheInit` | `ircache_init.comp` | Once: every entry into the pool, marked free |
| `IrcacheCascades` | `ircache_cascades.comp` | One thread: each cascade's origin and how far it moved |
| `IrcacheScroll` | `ircache_scroll.comp` | One thread per cell: moves the cells into the scrolled grid, frees those that leave |
| `IrcacheCopyGrid` | `ircache_copy_grid.comp` | The scrolled grid back into the grid |
| `IrcacheAgeArgs` | `ircache_age_args.comp` | The aging pass's group count; empties the list of live entries |
| `IrcacheAge` | `ircache_age.comp` | Indirect, one thread per entry: ages, frees, takes the voted trace origin, lists the live entries |
| `IrcacheTraceArgs` | `ircache_trace_args.comp` | Freezes the live count and writes the group counts below |
| `IrcacheReset` | `ircache_reset.comp` | Indirect, per live entry: clears the samples of entries with no irradiance yet |
| `IrcacheAccessibility` | `ircache_accessibility.comp` | Indirect, per stratum: a shadow ray between the old and new trace origins |
| `IrcacheValidate` | `ircache_validate.comp` | Indirect, per sample: traces old samples again |
| `IrcacheTrace` | `ircache_trace.comp` | Indirect, per sample: new samples into the reservoirs |
| `IrcacheSum` | `ircache_sum.comp` | Indirect, per live entry: samples into spherical harmonics |
| `Shade` | `shade.comp` | Per pixel: the camera ray, lit by the lights and the cache; averaged into `image` |
| `Display` | `display.frag` | Exposure and tone curve to the swapchain |
| `UIOverlay` | | The panel |

The cache lives in storage buffers declared in `bufferLayouts`. Each is one device-local buffer that keeps its contents from frame to frame, so the passes need no buffers of their own. All of them are in the `ircacheSet` layout and declared in `ircache.glsl`:

| Buffer | Elements | Holds |
|---|---|---|
| `IrcacheMeta` | 16 uint | Counts: live entries being traced, initialized, highest entry, allocated, listed |
| `IrcacheCascades` | 26 ivec4 | Each cascade's origin and scroll, then the eye |
| `IrcacheGrid`, `IrcacheGridScrolled` | 2 uint per cell | The cell's entry and flags (occupied, allocated this frame) |
| `IrcacheEntryCell` | 65,536 uint | Each entry's cell |
| `IrcacheSpatial`, `IrcacheProposal` | 65,536 uvec4 | Each entry's trace origin, and the one voted for next |
| `IrcacheProposalCount` | 65,536 uint | Votes this frame |
| `IrcacheIrradiance` | 3 vec4 per entry | L1 spherical harmonics of r, g and b |
| `IrcacheAux` | 64 uvec4 per entry | 16 reservoirs, 16 sample values, 16 trace origins |
| `IrcacheLife`, `IrcachePool`, `IrcacheIndirection` | 65,536 uint | Life and rank; free entries; this frame's live entries |
| `IrcacheArgs` | 8 uvec4 | The indirect passes' group counts |

`lighting.glsl` holds the light model and the sky, both the reference path tracer's. `ircache_trace.glsl` holds one ray of an entry.

Settings are `scene.custom.ic.*` values, read through the `Settings` uniform block:

| Setting | Default | Meaning |
|---|---|---|
| `reset` (uint) | 0 | 1 starts the image's average over in that frame |
| `view` (uint) | 0 | 0 image, 1 cache, 2 entries, 3 direct light only |
| `toneMapper` (uint) | 2 | 2 AgX, 1 Khronos PBR Neutral, 0 ACES |
| `freeze` (uint) | 0 | 1 freezes the cache |
| `exposure` | 1.0 | Multiplies the image before the tone curve |
| `environmentIntensity` | 1.0 | Multiplies the sky |
| `sunAngle` | 0.27 | Angular radius of directional lights, in degrees |
| `history` | 0.1 | The least weight a new frame has in the image's average |

Shaders recompile with `sk.shaders.compile_dir("pipeline/shaders")`, which the example runs at start.
