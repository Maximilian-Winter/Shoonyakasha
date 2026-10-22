# Optics episodes

Short demonstrations of optics on the [wave optics](../wave_optics/README.md) example's spectral, polarised path tracer, each beside a panel that plots the physics the image shows. They run the wave optics [pipeline](../wave_optics/pipeline/pipeline.json) and compile its shaders at start, and need a GPU with ray queries.

| Script | Episode |
|---|---|
| `gold.py` | [Why gold is gold](#why-gold-is-gold): four metals beside their reflectance and refractive indices |
| `brewster.py` | [Brewster's angle](#brewsters-angle): a polariser takes a glass slab's reflection away |

`optics_common.py` holds what they share: the pipeline's paths and settings, generated shapes that `engine.create_mesh` turns into entities, plots drawn into RGBA images that panels show with `ui.set_texture_data`, panel helpers and the average over paths. The average starts over whenever the camera or a setting the image depends on changes.

All episodes share these keys and options:

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera; moving starts the average over |
| **R** | Start the average over |
| **F2** | The panel on or off |
| **P** | Screenshot into `<episode>_captures/` |

| Option | Default | |
|---|---|---|
| `--exposure X` | 1.0 | Multiplies the image before the tone curve |
| `--samples N` | 0 | Stop averaging at N paths per pixel (0: never) |
| `--screenshot FILE` | | Save once `--samples` paths are in, then quit |
| `--width`, `--height` | 1600, 900 | |

## Why gold is gold

Four polished spheres of gold, copper, silver and aluminium, path traced from each metal's measured complex refractive index n + ik. A panel plots each metal's reflectance at normal incidence and the n and k of one of them, from the same tables the renderer reads, with a wavelength cursor that reads off n, k and the reflectance of every metal there. The scene is lit by white light, or by light of the cursor's wavelength alone; moving the cursor switches to the latter, so the spheres follow it.

```
python gold.py
python gold.py --metal copper --wavelength 560                  # copper's n and k, the cursor at 560 nm
python gold.py --single --wavelength 450                        # light of 450 nm alone
python gold.py --samples 512 --screenshot out.png               # converge, save, quit
```

### What it shows

The spheres are the wave optics pipeline's conductor material: GGX microfacets with the Fresnel equations for the metal's n + ik at each of a path's four wavelengths. Gold, silver and copper are from Johnson & Christy 1972 and aluminium from Rakić 1995, from 380 to 780 nm in 20 nm steps (`../wave_optics/pipeline/shaders/metals.glsl`). The lamp shines with a flat spectrum, so its highlights on the spheres take the colour of each metal's reflectance spectrum.

The panel reads `metals.glsl` and interpolates it linearly, as the shader does. Its plots:

- **Reflectance at normal incidence** of the four metals, R = ((n − 1)² + k²) / ((n + 1)² + k²), over the visible spectrum.
- **Refractive index** of the selected metal: n solid, k dashed.

Below them, the cursor's wavelength and photon energy (1239.84 / λ eV), n, k and R of each metal there, and a note on the selected metal. The swatch on each metal's button is the sRGB colour of its normal reflectance under the flat spectrum, through the CIE 1931 fit the renderer uses.

**Light of one wavelength.** Moving the cursor, the **One wavelength** button, **V** or `--single` light the scene with the cursor's wavelength alone: every path carries that wavelength (the pipeline's `wo.wavelength`), and the image shows the spectral radiance there in the wavelength's colour, at half the exposure. Each sphere is then as bright as its reflectance at that wavelength: at 450 nm gold and copper reflect the sky dimly and silver and aluminium brightly; at 650 nm all four alike. **White light** returns to the full spectrum.

The names under the spheres are placed for the starting camera and hide once it moves.

### Using it

| Key | |
|---|---|
| **Left** / **Right** | Move the wavelength cursor by 5 nm, in light of that wavelength |
| **V** | White light / light of the cursor's wavelength |
| **M** | Plot n and k of the next metal |

The slider moves the cursor, in light of its wavelength; **White light** and **One wavelength** switch the light; the metal buttons pick the metal whose n and k are plotted.

| Option | Default | |
|---|---|---|
| `--metal NAME` | `gold` | The metal whose n and k are plotted: `gold`, `silver`, `copper`, `aluminium` |
| `--wavelength NM` | 450 | The cursor's wavelength, 380 to 780 nm |
| `--single` | off | Start in light of the cursor's wavelength |
| `--roughness X` | 0.04 | GGX roughness of the spheres |
| `--sky X` | 0.4 | Intensity of the sky's light |

## Brewster's angle

A glass slab over a mosaic of coloured tiles. A lamp stands where the glass reflects it into the camera, and the sky reflects across the rest of the glass. A polariser in front of the camera turns; at Brewster's angle, tan θ_B = n, the glass reflects no p-polarised light, and a polariser that passes p alone takes the reflection away. A panel plots the reflectance of s and p light against the angle of incidence.

```
python brewster.py
python brewster.py --angle brewster --polariser 90              # the reflection gone
python brewster.py --material water
```

### What it shows

The slab is a box 2.4 × 0.04 × 1.6 m of `KHR_materials_transmission` glass, a solid of the set index with GGX roughness 0.01, 2 cm above 11 × 7 tiles. The wave optics pipeline reflects and refracts at its faces with the Fresnel equations as Mueller matrices, so reflected light leaves partly polarised, wholly s-polarised at Brewster's angle.

The camera stands 4.1 m from the glass's centre at the set angle of incidence there, in the plane of the glass's depth, and looks a little to the right of the centre. The lamp, of radius 0.3 m, stands 3 m from the centre in the mirror direction. Its reflection is a disc as wide as the lamp seen in the glass; across the disc and across the glass, the angle of incidence differs from the centre's.

The polariser is ideal: it passes half of unpolarised light. While it is on, the exposure doubles, so that light through it keeps its brightness. Its angle is measured from the image's horizontal, which at the glass's centre lies within a few degrees of the s direction, as the camera looks a little to the right of it.

The panel plots, for the selected index n:

- **Rs and Rp**, the reflectances of s and p light, and their mean, that of unpolarised light, from 0° to 90°, with Brewster's angle dashed.
- **The cursor** at the angle of incidence at the glass's centre, from wherever the camera is.

Below them, Rs, Rp and the unpolarised reflectance there, and the reflected light that reaches the camera: (Rs + Rp) / 2 without the polariser, Rs cos²φ + Rp sin²φ through it at angle φ from the s direction, on the doubled exposure's scale.

### Using it

| Key | |
|---|---|
| **Left** / **Right** | The angle of incidence, by 1° |
| **B** | Go to Brewster's angle |
| **T** | The polariser on or off |
| **Up** / **Down** | Turn the polariser by 15° |

The angle slider and **B** place the camera and the lamp; flying moves the camera alone. The buttons set the slab's index: water 1.33, glass 1.50 or diamond 2.42.

| Option | Default | |
|---|---|---|
| `--angle DEG` | 70 | The angle of incidence at the glass's centre, 5 to 85, or `brewster` |
| `--material NAME` | `glass` | `water`, `glass` or `diamond` |
| `--polariser DEG` | off | The polariser's angle from the image's horizontal |
| `--roughness X` | 0.01 | GGX roughness of the glass |
| `--sky X` | 0.8 | Intensity of the sky's light |

## References

- P. B. Johnson, R. W. Christy. *Optical Constants of the Noble Metals.* Phys. Rev. B 6, 4370, 1972.
- A. D. Rakić. *Algorithm for the determination of intrinsic optical constants of metal films: application to aluminum.* Appl. Opt. 34, 4755, 1995.
- M. Born, E. Wolf. *Principles of Optics*, sections 1.5 (reflection and refraction of a plane wave) and chapter 14 (optics of metals).
