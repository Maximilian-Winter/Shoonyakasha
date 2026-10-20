# The stage pipeline

A copy of the [showroom pipeline](../../showroom/pipeline/README.md), with its virtual shadow map, area lights, ray-traced light shadows, reflections and their filters, plus smoke and haze lit by the scene's lights, and lasers drawn into it. `ltc.bin` is the showroom's, made by its `ltc_fit.py`.

## Fog

The fog lives in a grid that follows the camera: three 2D-array images, `fogScatter`, `fogHistory` and `fogIntegrated`, at a sixth of the render size, whose 96 layers are slices of view depth, spaced exponentially from `fog.grid.x` to `fog.grid.y` metres. Four passes run after `TransparentRT`, before `TAA`, in the `hybrid` and `raytraced` presets:

- **`FogScatter`** (`fog_scatter.comp`, needs ray queries) fills `fogScatter`. Each cell holds the fog's density at one point in it, which moves within the cell every frame while TAA is on, and the light scattered towards the eye there:
  - **Density:** haze that drifts with `fog.wind`, ground fog that thins out above `fog.motion.z` metres, and the plumes of up to four smoke machines: jets that widen, slow and rise as they leave the nozzle, carrying puffs. A burst (`fog.burst`) adds to the smoke that leaves the nozzles from then on, fading, so it travels out along the plumes. The fog fills the hall, a box `fog.hall` across, and fades over its last metre.
  - **Light:** every point and spot light reaching the point through its falloff and cone, scattered by a Henyey-Greenstein phase function of `g = fog.media.w`. For lights whose shadow flag is set, a ray to the light decides whether it gets there, so objects cut shadows into the beams. Lights with image slot 2 (`set_light_source_image`) light surfaces only. Directional lights do not light the fog.
  - **Over frames:** the result is blended with last frame's value where the point was then (`fogHistory`), keeping `fog.grid.z` of it.
- **`FogHistory`** (`fog_history.comp`) copies `fogScatter` into the persistent `fogHistory`.
- **`FogIntegrate`** (`fog_integrate.comp`) runs along each column of the grid from the eye outwards, adding up the scattered light, faded by the fog in front of it, and the transmittance. Each layer of `fogIntegrated` holds both up to the far end of its slice.
- **`FogApply`** (`fog_apply.frag`) reads `fogIntegrated` at each pixel's depth, interpolated between slices, and blends it onto `hdrColor` with the factors `one` and `src_alpha`: the colour becomes the fog's light plus the colour times the transmittance.

## The `Fog` block

`scene.custom.fog.*`, all `vec4`, set with `set_custom_vec4`:

| Field | Default | Meaning |
|---|---|---|
| `grid` | 0.3, 60, 0.8, 1 | x first and y last slice depth (metres); z share of last frame's value kept; w 0 makes `FogApply` leave the image as it is |
| `media` | 0.012, 1, 0, 0.55 | x haze density, y smoke machine output, z ground fog density (per metre); w phase function g |
| `motion` | 0, 1, 0.6, 1 | x seconds, for the motion of haze and smoke; y light the fog scatters, times; z height of the ground fog; w noise frequency, times |
| `wind` | 0.15, 0, 0.05 | xyz drift of the haze, metres per second |
| `hall` | 20, 14, 20 | x half width, y height, z half depth of the box the fog fills, centred on the origin, on the floor |
| `machine0` ... `machine3` | 0 | xyz the nozzle, w its output; 0 turns the machine off |
| `machineDir0` ... `machineDir3` | 0, 0, 1, 2 | xyz the direction the jet leaves in, w its speed (metres per second) |
| `burst` | -1000, 0, 1, 0 | x when the machines last burst (seconds, on `motion.x`'s clock); y how much more smoke they give then, times; z how fast that fades, per second |

## Lasers

Up to four laser projectors, each set by the `Lasers` block: its aperture, the direction it points, its up, its spread, its colour times its power and a pattern (`lasers.glsl`):

| Pattern | What it draws |
|---|---|
| 0 | Nothing |
| 1 | A fan of beams across the projector's right, `spread` either side of its axis, tilting and rolling slowly |
| 2 | The same fan, its beams waving up and down one after another |
| 3 | A tunnel: beams on a cone of half-angle `spread` round the axis, turning |
| 4 | A sheet of light: a plane through the aperture along the projector's right, `spread` either side of its axis, tilting slowly up and down |

Patterns 1 to 3 have up to 16 beams each. Two passes run after `FogApply`, in the `hybrid` and `raytraced` presets:

- **`LaserTrace`** (`laser_trace.comp`, needs ray queries) traces each beam from its aperture to the first surface it hits, at most `laser.params.w` metres, and writes it to `laserBeams`, a 2 x 64 image: row `j * 16 + k` is projector j's beam k, texel 0 the aperture and the length (0 for no beam), texel 1 the direction and the projector.
- **`Lasers`** (`lasers.frag`, needs ray queries) adds to `hdrColor`, for each pixel:
  - **Beams:** a beam is a Gaussian tube of radius `laser.params.y`, widened to the pixel's size where that is larger, so it keeps its brightness as it gets further away. Where the view ray passes closest to it, in front of the pixel's surface, the light is the beam's power times the fog's density there (`fogDensity`), the phase function, the tube's profile and the fog's transmittance to the eye, over the tube's width and the sine of the angle between ray and beam.
  - **Sheets:** where the view ray crosses a sheet, its power spread over the sheet's arc at that distance, times the fog's density, the phase function and the transmittance, over the cosine between ray and plane. A ray back to the aperture decides whether the point is in the sheet's shadow.
  - **On surfaces:** a dot where each beam lands and a line where each sheet crosses a surface, in their colour times the surface's albedo and `laser.params.z`.

With `fog.grid.w` at 0 the beams and sheets have no fog to light, and only the dots and lines remain.

## The `Lasers` block

`scene.custom.laser.*`, all `vec4`, set with `set_custom_vec4`:

| Field | Default | Meaning |
|---|---|---|
| `origin0` ... `origin3` | 0 | xyz the aperture; w the pattern |
| `axis0` ... `axis3` | 0, 0, 1, 0 | xyz the direction the projector points; w its beams (patterns 1 to 3) |
| `up0` ... `up3` | 0, 1, 0, 0 | xyz its up; w the spread, radians |
| `color0` ... `color3` | 0 | rgb colour times power, per beam, or for the whole sheet; w the phase of its motion |
| `params` | 0, 0.003, 1, 80 | x seconds, for the patterns' motion; y beam radius (metres); z brightness of dots and lines on surfaces; w longest beam (metres) |

## Changing it

Edit `pipeline.json` and the shaders as with any copy. Shaders recompile with `sk.shaders.compile_dir("shaders")`, and `sk.pipeline.check("pipeline.json")` validates the JSON without starting the engine. The grid's size is the three fog images' `widthScale`, `heightScale` and `arrayLayers`; `FogScatter` and `FogHistory` dispatch one workgroup layer per slice, so their `dispatch.z` is `arrayLayers`.

**Passes and shaders added over the showroom pipeline:**

- `FogScatter`, `FogHistory`, `FogIntegrate` and `FogApply`, in `fog_scatter.comp`, `fog_history.comp`, `fog_integrate.comp`, `fog_apply.frag` and `fog.glsl`
- `LaserTrace` and `Lasers`, in `laser_trace.comp`, `lasers.frag`, `lasers.glsl` and `lasers_block.glsl`
