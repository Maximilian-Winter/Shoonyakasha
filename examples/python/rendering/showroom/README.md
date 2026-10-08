# Showroom

A model showroom for cars and starships: each model on a turntable in an infinity-cove studio, lit by softboxes, hard light, spotlights or neon, with cinematic camera moves and automatic stills and video for posting. It runs on its own [showroom pipeline](pipeline/README.md), a copy of the default pipeline with four additions:
- **Virtual shadow map for the sun.** An 8-level clipmap of 4096² virtual texels per level, 1 mm texels close up. Only the pages visible pixels need are rendered, into a cached pool.
- **Rectangular area lights.** Each softbox lights as its panel, by linearly transformed cosines from a table fitted once: its highlight on the paint is the panel's shape, stretched along the body's curves. The panel's diffuser has a hotspot and a fabric grid, which glossy paint shows sharp and rough surfaces blurred.
- **Images on lights.** The stained-glass lighting's window shines with a picture: its colours light the car and its panes show in the paint and the polished floor. `--light-image` takes your own.
- **Soft ray-traced shadows.** Rays towards random points of each softbox give its shadows soft edges the shape of the panel; the spotlights are spheres, with penumbrae as wide as they are.
- **Ray-traced reflections** in glossy floors and paint.

```
python tools/fetch_assets.py showroom          # once, from the repository root; needs a Sketchfab API token
python showroom.py
```

Sketchfab only gives downloads to a signed-in account. Copy the API token from [sketchfab.com/settings/password](https://sketchfab.com/settings/password) and set it before fetching:

```
export SKETCHFAB_API_TOKEN=...                 # PowerShell: $env:SKETCHFAB_API_TOKEN = "..."
python tools/fetch_assets.py showroom          # all eight, or showroom/alfa_gtv6 for one
```

**Fetching without a token:** download each model's glTF archive from its page by hand and save it as `assets/showroom/source/<name>.zip`, with the names below. The fetch converts whatever it finds there.

**Converting:** needs Pillow and numpy (`pip install pillow numpy`). Textures come out at most 2k; `--resolution 4k` keeps more for close-ups.

**Before fetching:** the showroom runs anyway, with the bundled Fox. `--extra path/to/any.gltf` adds other models, and `--extra 4.5=car.gltf` shows one 4.5 m long.

## What it shows

| In the showroom | Pipeline feature |
|---|---|
| Studio softboxes, their rectangular highlights stretched across the paint, the neon strips as long streaks | Rectangular area lights: linearly transformed cosines, from a 64 × 64 table in `pipeline/ltc.bin` |
| The softboxes' grid in the paint's highlights; **U** switches to plain fabric or even panels | The diffuser's pattern in the light it casts, read where each surface's reflection lands and blurred as wide as it spreads |
| **L** to stained glass: the window's coloured panes in the paint and the polished floor, and its colours on the car | A rectangle light shining with a picture (`lightImage`), prefiltered so each surface sees it as sharp as it reflects |
| **J**: the same softboxes as spheres, round highlights | The representative-point sphere lights they replaced, for a before-and-after |
| Soft shadows under the car, long beside the strip lights | Ray-traced shadows towards random points of each panel, averaged by TAA |
| The turntable's shadow and a wheel's shadow on the floor, sharp to the millimetre in close-ups | The sun's virtual shadow map, its levels matched to pixel size |
| The car mirrored in the glossy floor | Ray-traced reflections: the ray finds what it hits, and last frame's image says what that looks like |
| **V**: coloured clipmap levels with their page borders, then the pages drawn this frame (red, under the turning car) against the cached ones (green) | The virtual shadow map's page table at work |
| **G** cycles shadows: VSM + ray-traced lights, everything ray traced, VSM with shadow-mapped lights, the default pipeline's cascades | Presets `hybrid`, `raytraced`, `high`, `cascades`: a before-and-after for the same frame |
| The LED rim of the turntable, softbox panels, neon strips | Emission and bloom |
| **B**: the floor and background softened, the shot's subject sharp | Depth of field from the depth buffer, after TAA |

**Keys:** **←/→** models · **L** lighting (studio, hard light, night, neon, stained glass) · **C** camera (turntable, cinematic, orbit: drag with the left mouse button and scroll, free: WASD/Q/E and the right mouse button) · **G** shadow technique · **R** reflections · **T** turntable · **K** paint colour (cars) · **X** clear coat on/off · **J** softboxes as rectangles or spheres · **U** softbox diffusers (grid, plain fabric, even) · **M** tone mapper (AgX, PBR Neutral, ACES) · **B** depth of field (off, f/2.8, f/0.8) · **Y** turn the model 90° · **F** dark or white floor · **V** debug views · **H** credits and status · **O** render stats (frame rate, GPU time per pass, draw calls) · **P** screenshot · **F9** record · **F1** help.

The window opens at 1920×1080; `--width`/`--height` change it, `--vertical` makes it 1080×1920 for phone-shaped video. `--lighting`, `--camera`, `--shadows` and `--softbox` choose what it opens with, and `--light-image picture.png` what the stained-glass window shows (stretched to its 1:2 shape), `--models alfa_gtv6,aat` which models and in what order.

## Stills and video for posting

```
python showroom.py --screenshots shots/                          # every model, its own lighting and the studio's, six views
python showroom.py --screenshots shots/ --width 3840 --height 2160 --lighting night
python showroom.py --record tour.mp4                             # every model through the cinematic shots
python showroom.py --record reel.mp4 --vertical --models alfa_33_stradale_2024 --seconds 20
python showroom.py --record tour.mp4 --dof 1.4 --grain 0.02 --vignette 0.15   # with depth of field, grain and vignette
```

**Stills:** each still is held for `--settle` frames (48), so exposure settles, and then `--samples` frames (64) of it are averaged: each is offset by TAA's sub-pixel jitter and draws new ray-traced samples, so the average has no aliasing and no ray-tracing noise. `--samples 256` is cleaner still, `--samples 1` captures without averaging. They are saved as `<model>_<lighting>_<view>.png`; the views are three-quarter front, side, three-quarter rear, a long-lens hero shot, top and a detail.

**Recordings:** they step the animation by exactly one frame of `--fps` (30) each frame, so the video plays smoothly however fast the machine renders, and fade through black between models. Each recorded frame is the average of `--motion-blur` sub-frames (8) spread over `--shutter` of the frame interval (0.5, a film camera's 180° shutter): motion blurs as it does on film. `--motion-blur 1` turns it off. Recordings render at twice the window's width and height and scale down, for crisper edges and detail: `--supersample` sets the factor (`--supersample 1` is quicker, and stills or the interactive showroom take it too). They take far longer than they play, and the window crawls meanwhile, so the console prints the frames done and the time left every few seconds; **Esc** or **Ctrl+C** stops early and keeps what is recorded. Recording needs ffmpeg on `PATH`.

**Depth of field:** `--dof 2.8` (or **B**) blurs what is nearer or further than where the camera looks, as a full-frame camera with that f-number and the shot's focal length would; `--focus` sets the distance in metres instead. It is a real lens's blur, so at real f-numbers whole-car shots stay mostly sharp and the close-ups and the floor in front of the camera blur; `--dof 0.8` goes further than a real lens. Stills and recordings average away the blur's sampling noise.

**Look:** `--vignette 0.15` darkens the corners a little and `--grain 0.02` adds fine film grain, which also keeps video encoders from banding smooth gradients. Both are off unless given.

**Credits:** the model's credit stays on screen in both unless you pass `--no-overlay`. **P** and **F9** do the same interactively, into `showroom_captures/`; **I** takes an averaged still there, holding the view while it averages `--samples` frames.

## The models

| Name | Model | Licence |
|---|---|---|
| `alfa_33_stradale_1968` | "1968 Alfa Romeo 33 Stradale" ([skfb.ly/pLFPr](https://skfb.ly/pLFPr)) by OUTPISTON | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `alfa_33_stradale_2024` | "2024 Alfa Romeo 33 Stradale ICE" ([skfb.ly/pNXHu](https://skfb.ly/pNXHu)) by Ddiaz Design | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `alfa_montreal` | "1970 Alfa Romeo Montreal" ([skfb.ly/pLKx8](https://skfb.ly/pLKx8)) by OUTPISTON | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `alfa_gtv6` | "1986 Alfa Romeo GTV-6" ([skfb.ly/pLNop](https://skfb.ly/pLNop)) by OUTPISTON | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `eta2_interceptor` | "SWBF2(Custom) - Anakin's Eta-2 Actis Interceptor" ([skfb.ly/pxvCx](https://skfb.ly/pxvCx)) by Zorg_Sinister | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `jedi_starfighter` | "Anakin's Jedi Starfighter - Star Wars" ([skfb.ly/6RSHr](https://skfb.ly/6RSHr)) by Quiznos323 | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `aat` | "-Star Wars- AAT" ([skfb.ly/owYtF](https://skfb.ly/owYtF)) by ARKON MAREK | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `alkesh` | "Inspired By Stargate SG-1: Goa´Uld Alkesh" ([skfb.ly/pNuWv](https://skfb.ly/pNuWv)) by Ska-Ara | [CC BY-NC-SA 4.0](http://creativecommons.org/licenses/by-nc-sa/4.0/) |
| `gaz13_chaika` | "GAZ 13 Chaika" ([skfb.ly/oIoSW](https://skfb.ly/oIoSW)) by panderlaike_design | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `shelby_cobra` | "Shelby Cobra \| www.vecarz.com" ([skfb.ly/psAAX](https://skfb.ly/psAAX)) by vecarz | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `chevrolet_camaro` | "1970 Chevrolet Camaro" ([skfb.ly/pryVB](https://skfb.ly/pryVB)) by DisneyCars | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `porsche_911_turbo` | "FREE 1975 Porsche 911 (930) Turbo" ([skfb.ly/6WZyV](https://skfb.ly/6WZyV)) by Lionsharp Studios | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `corvette_c8` | "2020 Chevrolet Corvette C8 Stingray Convertible" ([skfb.ly/pr7JI](https://skfb.ly/pr7JI)) by Ddiaz Design | [CC BY 4.0](http://creativecommons.org/licenses/by/4.0/) |
| `lamborghini_revuelto` | "Lamborghini Revuelto" ([skfb.ly/prqBx](https://skfb.ly/prqBx)) by Outlaw Games™ | [CC BY-NC 4.0](http://creativecommons.org/licenses/by-nc/4.0/) |

The full attribution lines, ready to paste into a post's caption:

```
"1986 Alfa Romeo GTV-6" (https://skfb.ly/pLNop) by OUTPISTON is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"1970 Alfa Romeo Montreal" (https://skfb.ly/pLKx8) by OUTPISTON is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"1968 Alfa Romeo 33 Stradale" (https://skfb.ly/pLFPr) by OUTPISTON is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"2024 Alfa Romeo 33 Stradale ICE" (https://skfb.ly/pNXHu) by Ddiaz Design is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"SWBF2(Custom) - Anakin's Eta-2 Actis Interceptor" (https://skfb.ly/pxvCx) by Zorg_Sinister is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"Anakin's Jedi Starfighter - Star Wars" (https://skfb.ly/6RSHr) by Quiznos323 is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"-Star Wars- AAT" (https://skfb.ly/owYtF) by ARKON MAREK is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"Inspired By Stargate SG-1: Goa´Uld Alkesh" (https://skfb.ly/pNuWv) by Ska-Ara is licensed under CC Attribution-NonCommercial-ShareAlike (http://creativecommons.org/licenses/by-nc-sa/4.0/).
"GAZ 13 Chaika" (https://skfb.ly/oIoSW) by panderlaike_design is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"Shelby Cobra | www.vecarz.com" (https://skfb.ly/psAAX) by vecarz is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"1970 Chevrolet Camaro" (https://skfb.ly/pryVB) by DisneyCars is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"FREE 1975 Porsche 911 (930) Turbo" (https://skfb.ly/6WZyV) by Lionsharp Studios is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"2020 Chevrolet Corvette C8 Stingray Convertible" (https://skfb.ly/pr7JI) by Ddiaz Design is licensed under Creative Commons Attribution (http://creativecommons.org/licenses/by/4.0/).
"Lamborghini Revuelto" (https://skfb.ly/prqBx) by Outlaw Games™ is licensed under Creative Commons Attribution-NonCommercial (http://creativecommons.org/licenses/by-nc/4.0/).
```

**Downloaded files:** the models are not in the repository. They are fetched into `assets/showroom/`, which Git ignores.

**Shipping the showroom with its models:** the converted files are adaptations under their models' licences: every one with these credits, the NC-SA ones under CC BY-NC-SA 4.0 again, and none of the NC or NC-SA ones for sale. `tools/sketchfab.py` records each model's credit in its `showroom.json` beside the converted file.

## How it is put together

- **[showroom.py](showroom.py)** is the application. It loads models on first view and then hides and shows them.
  - **Staging:** it stands each model on the turntable at its catalogue length.
  - **Lighting rigs:** the rigs are lists of lights placed around the model's centre and scaled with it, so a 12 m bomber is lit like a coupe. A softbox is a spot light with its panel's size (`set_light_source_size`), which the pipeline shades as that rectangle, and a panel mesh that casts no shadow, so its light can shine out of it.
  - **The map's cache:** it tells the virtual shadow map what moves. The turning car's bounding sphere is `showroom.vsmDirty0`, so only the pages under it are redrawn while the rest of the map stays cached.
- **[catalogue.py](catalogue.py)** says how to stand each model: its length, kind, hover height and paint materials. It also says how it faces: **Y** turns a model and prints the `yaw` to put here, for any whose nose does not point at the camera at the start of the cinematic shots.
- **[studio.py](studio.py)** writes the stage on first start into `assets/showroom/studio/`:
  - the infinity cove, the turntable with its light strip, the softbox and the plinth, as glTF;
  - an HDR environment with softboxes where the studio rig has its lights, so reflections agree with the lighting.
- **[tools/sketchfab.py](../../../../tools/sketchfab.py)** downloads the glTF archives and converts what the engine's loader does not read:
  - texture transforms, emissive strength, transmission (glass), specular-glossiness and unlit materials, and second UV sets;
  - textures larger than the chosen size, or in formats stb_image cannot decode;
  - mesh naming: every mesh gets a node named after its material, which is how **K** finds a car's paint.
- **[pipeline/](pipeline/README.md)** is the showroom pipeline.

## Notes

- **Catalogue settings:** each model's `yaw`, paint pattern and length in `catalogue.py` are first guesses. **Y** turns a model and prints the yaw to keep. The showroom prints every model's size and scale when it loads it; a model shown far too small usually has stray geometry far away, which the converter's bounds ignore (convert again with `python tools/fetch_assets.py showroom/<name>`: the download is kept).
- **Big models:** the stage grows with the model, so a 12 m bomber gets a larger cove, and the camera stays inside it.
- **Requirements:** ray-traced lights and reflections need `VK_KHR_ray_query`. Without it the showroom uses the virtual shadow map with shadow-mapped lights, and **G** cycles only `high` and `cascades`.
