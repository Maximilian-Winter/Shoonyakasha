# Why gold is gold

Four polished spheres of gold, copper, silver and aluminium, path traced by the [wave optics](../wave_optics/README.md) example's spectral pipeline from each metal's measured complex refractive index n + ik. A panel plots each metal's reflectance at normal incidence and the n and k of one of them, from the same tables the renderer reads, with a wavelength cursor that reads off n, k and the reflectance of every metal there. The single wavelength view renders the light at the cursor's wavelength alone. It needs a GPU with ray queries.

```
python gold.py
python gold.py --metal copper --wavelength 560                  # copper's n and k, the cursor at 560 nm
python gold.py --single --wavelength 450                        # the light at 450 nm alone
python gold.py --samples 512 --screenshot out.png               # converge, save, quit
```

## What it shows

The spheres are the wave optics pipeline's conductor material: GGX microfacets with the Fresnel equations for the metal's n + ik at each of a path's four wavelengths. Gold, silver and copper are from Johnson & Christy 1972 and aluminium from Rakić 1995, from 380 to 780 nm in 20 nm steps (`../wave_optics/pipeline/shaders/metals.glsl`). The lamp shines with a flat spectrum, so its highlights on the spheres take the colour of each metal's reflectance spectrum.

The panel reads `metals.glsl` and interpolates it linearly, as the shader does. Its plots:

- **Reflectance at normal incidence** of the four metals, R = ((n − 1)² + k²) / ((n + 1)² + k²), over the visible spectrum.
- **Refractive index** of the selected metal: n solid, k dashed.

Below them, the cursor's wavelength and photon energy (1239.84 / λ eV), n, k and R of each metal there, and a note on the selected metal. The swatch on each metal's button is the sRGB colour of its normal reflectance under the flat spectrum, through the CIE 1931 fit the renderer uses.

**Single wavelength.** With the **Single wavelength** button (or **V**, or `--single`), every path carries the cursor's wavelength alone (the pipeline's `wo.wavelength`), and the image shows the spectral radiance there in grey: 1 where a white surface would be under light of 1 at every wavelength. Moving the cursor starts the average over.

The names under the spheres are placed for the starting camera and hide once it moves.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera; moving starts the average over |
| **Left** / **Right** | Move the wavelength cursor by 5 nm |
| **V** | The full spectrum / the cursor's wavelength alone |
| **M** | Plot n and k of the next metal |
| **R** | Start the average over |
| **F2** | The panel on or off |
| **P** | Screenshot into `gold_captures/` |

The slider moves the cursor; the buttons pick the metal whose n and k are plotted.

## Options

| Option | Default | |
|---|---|---|
| `--metal NAME` | `gold` | The metal whose n and k are plotted: `gold`, `silver`, `copper`, `aluminium` |
| `--wavelength NM` | 450 | The cursor's wavelength, 380 to 780 nm |
| `--single` | off | Start in the single wavelength view |
| `--roughness X` | 0.04 | GGX roughness of the spheres |
| `--sky X` | 0.4 | Intensity of the sky's light |
| `--exposure X` | 1.0 | Multiplies the image before the tone curve |
| `--samples N` | 0 | Stop averaging at N paths per pixel (0: never) |
| `--screenshot FILE` | | Save once `--samples` paths are in, then quit |
| `--width`, `--height` | 1600, 900 | |

## Files

The example runs the wave optics example's [pipeline](../wave_optics/pipeline/pipeline.json) and compiles its shaders at start. The sphere is written as glTF, and the plots as PNG images, into a temporary directory at start.

## References

- P. B. Johnson, R. W. Christy. *Optical Constants of the Noble Metals.* Phys. Rev. B 6, 4370, 1972.
- A. D. Rakić. *Algorithm for the determination of intrinsic optical constants of metal films: application to aluminum.* Appl. Opt. 34, 4755, 1995.
- M. Born, E. Wolf. *Principles of Optics*, chapter 14 (optics of metals).
