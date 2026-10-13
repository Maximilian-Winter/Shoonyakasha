# Wave optics

A spectral, polarised path tracer in which light keeps a little of its wave nature: how coherent it is when it reaches a surface. A compact disc's grooves split the lamp's light into rainbows, and a sphere with a thin film shows interference colours. The rainbows are as sharp as the lamp is small and far away; a large, close lamp blurs them, as it widens every highlight. Gold and silver spheres reflect with their measured refractive indices, and a polariser in front of the camera shows the polarisation of what reaches it. It runs on its own [pipeline](pipeline/pipeline.json): one compute pass that traces with ray queries and averages one path per pixel per frame, as the [reference path tracer](../reference_pt/README.md) does. It needs a GPU with ray queries.

The light transport follows the papers on physical light transport by Shlomi Steinberg and coauthors (references below), in a reduced form described here; it is written from the papers and standard optics.

```
python wave_optics.py
python wave_optics.py --lamp-radius 0.3 --lamp-distance 1.0     # a large, close lamp: blurred rainbows
python wave_optics.py --polariser 90                            # a vertical polariser
python wave_optics.py --view 1                                  # the coherence width of the lamp's light
python wave_optics.py --samples 512 --screenshot out.png        # converge, save, quit
```

## The technique

**Wavelengths.** Each path carries four wavelengths: a hero wavelength drawn uniformly from 380 to 780 nm and three more a quarter of that range apart (Wilkie et al. 2014). Colours become radiance through the CIE 1931 colour matching functions (the fit of Wyman, Sloan and Shirley 2013) and linear sRGB, scaled so that a flat spectrum is white. RGB colours of materials and lights become spectra through three smooth bands that sum to one at every wavelength (`spectral.glsl`).

**Polarisation.** Light is a Stokes vector and each surface a 4 × 4 Mueller matrix in the frame of its plane of incidence. Paths start at the camera, so each wavelength carries the first row of the Mueller chain back to the camera: what the camera measures of light arriving at the current surface. The camera's row is (1, 0, 0, 0), or that of an ideal linear polariser at the set angle from the image's horizontal. Frames rotate between surfaces; diffuse surfaces depolarise (`optics.glsl`).

**Coherence.** A source of angular radius *a*, seen from a surface, gives its light a transverse coherence width

  σ = λ / (π a),

for which the coherence blur of a specular reflection, λ / (2π σ) in direction cosines, is a / 2: as wide as the source appears (the relation |Θ| = λ² / Ω of the generalized ray formulation, Eq. 41 of its arXiv version). A lamp of radius *r* at distance *d* has *a* = asin(*r* / *d*); a directional light has the sun's angular radius (0.27°). The panel shows σ at 550 nm for the lamp's light at the disc. At every surface each scene light is connected towards its centre through a shadow ray, with its own σ:

- a **grating** spreads each diffraction order into a Gaussian lobe of deviation a / 2;
- glossy lobes widen with the source: GGX alpha + a / 4;
- a **thin film** shows its interference fringes as far as σ reaches across the film's shear (below).

The path continues in a direction drawn from the surface, for light coherent as if from a source of angular radius `--indirect` (5° by default): the sky and light from other surfaces.

**Materials.** Surfaces are diffuse unless the example gives them one of these materials:

- **Conductor** (gold, silver): GGX microfacets with the Fresnel equations for the metal's complex refractive index, from 380 to 780 nm in 20 nm steps (`metals.glsl`: gold, silver and copper from Johnson & Christy 1972, aluminium from Rakić 1995, through the refractiveindex.info database).
- **Grating** (the disc): an aluminium surface with circular grooves about the disc's centre, a sinusoidal profile of 0.1 µm depth and the set spacing (1.6 µm, a CD's). Order *n* leaves where the tangential direction cosines satisfy the grating equation u_o + u_i = n λ / Λ across the grooves, with Kirchhoff's scalar efficiency J_n(k h (cos θ_i + cos θ_o) / 2)² for depth *h*; orders up to |n| = 4 that propagate. The interface's Fresnel Mueller matrix at the half vector multiplies the sum. A path continues into an order drawn by its efficiency at the hero wavelength. A non-zero order bends each wavelength differently, so from there the path keeps the hero wavelength alone.
- **Thin film** (the left sphere): a film of index 1.8 and the set thickness on aluminium, under GGX microfacets of roughness 0.12. Its reflection is the sum of the reflection from its top and that from its bottom, delayed by the optical path through the film; their Mueller matrices mix as γ M(r₁ + r₂) + (1 − γ)(M(r₁) + M(r₂)). The fringe visibility γ = exp(−½ (s / σ)²) is the coherence at the lateral shear s = 2 t tan θ_f cos θ_o between the two beams, for film thickness *t* and refracted angle θ_f (Steinberg & Yan 2021, Eq. 18).

**What it shows.** With a small, distant lamp the disc shows thin radial rainbows beside the lamp's reflection. As the lamp grows or comes closer they widen and wash out, and so does the lamp's reflection in the disc and the spheres. Film colours fade as the film thickens: across the visible range a thick film's fringes are so close together in wavelength that they average out. The film's shear stays well below a micrometre for films that show colours under white light, so its colours outlast even a large, close lamp. Under a polariser the metals' and the film's edges brighten and darken as the polariser turns, opposite at edges a quarter turn apart.

**Light.** The scene lights fall off with the inverse square of the distance. The lamp's sphere has the light's radiance, *I* / (π r²), and shadow rays towards a light end at its radius. Emissive surfaces count where the camera sees them directly. The lamp's intensity follows its distance squared, so the disc is lit equally from any distance. Behind a diffuse bounce, glossy lobes widen to a roughness of at least 0.3 and the grating's to the indirect source size, and the sky is read from a blurred level of the prefiltered environment. This blurs the caustics and the sky's sun that a diffuse surface would otherwise only reach by chance.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera; moving starts the average over |
| **1** / **2** | The image / the coherence width of the lamp's light |
| **R** | Start the average over |
| **F2** | The panel on or off |
| **P** | Screenshot into `wave_optics_captures/` |

The panel sets the lamp's radius and distance, the film's thickness, the grooves' spacing, the polariser (the **Polariser** button turns it on and off; the slider sets its angle) and the exposure, and switches between the views. **Coherence** shows, for each surface the camera sees, σ at 550 nm of the lamp's light there on a log scale: dark blue 1 µm, green 16 µm, dark red 256 µm.

## Options

| Option | Default | |
|---|---|---|
| `--lamp-radius R` | 0.02 | The lamp's radius in metres, 0.002 to 0.4 |
| `--lamp-distance D` | 2.0 | From the disc's centre to the lamp in metres, 0.6 to 8 |
| `--film T` | 0.45 | The film's thickness in micrometres |
| `--pitch P` | 1.6 | The grooves' spacing in micrometres |
| `--polariser DEG` | off | The polariser's angle from the image's horizontal, counter-clockwise |
| `--view 0` / `1` | 0 | The image / the coherence width |
| `--indirect DEG` | 5 | Angular radius of the sources of light that does not come from a scene light |
| `--bounces N` | 6 | Bounces after the first hit, 1 to 12 |
| `--sky X` | 0.15 | Intensity of the sky's light |
| `--exposure X` | 1.0 | Multiplies the image before the tone curve |
| `--samples N` | 0 | Stop averaging at N paths per pixel (0: never) |
| `--screenshot FILE` | | Save once `--samples` paths are in, then quit |
| `--width`, `--height` | 1600, 900 | |

## The pipeline

| Pass | Shader | Work |
|---|---|---|
| `WaveTrace` | `wave.comp` (with `spectral.glsl`, `optics.glsl`, `metals.glsl`) | One path per pixel, averaged into `accumulation` (rgb the mean, a the count) |
| `Display` | `display.frag` | Exposure and tone curve (AgX by default) |

Settings are `scene.custom.wo.*` values in the `Settings` uniform block. The `Materials` block holds eight material slots of three vec4s each, `scene.custom.wo.slot[0]` to `slot[23]`:

| vec4 | x | y | z | w |
|---|---|---|---|---|
| 0 | entity id | type: 1 conductor, 2 grating, 3 film | metal + 1: 1 gold, 2 silver, 3 copper, 4 aluminium | roughness |
| 1 | groove spacing, µm | groove depth, µm | film index | film thickness, µm |
| 2 | grating centre x | y | z | substrate index (without a metal) |

The shapes (a sphere and a ring for the disc) are written as glTF into a temporary directory at start. Shaders recompile with `sk.shaders.compile_dir("pipeline/shaders")`, which the example runs at start.

## References

- S. Steinberg, R. Ramamoorthi, B. Bitterli, E. d'Eon, L.-Q. Yan, M. Pharr. *A Generalized Ray Formulation For Wave-Optics Rendering.* arXiv:2303.15762, 2023; published as *A Generalized Ray Formulation For Wave-Optical Light Transport*.
- S. Steinberg, P. Sen, L.-Q. Yan. *Towards Practical Physical-Optics Rendering.* ACM TOG 41(4), 2022.
- S. Steinberg, L.-Q. Yan. *Physical Light-Matter Interaction in Hermite-Gauss Space.* ACM TOG 40(6), 2021.
- S. Steinberg, L.-Q. Yan. *A Generic Framework for Physical Light Transport.* ACM TOG 40(4), 2021.
- A. Wilkie, S. Nawaz, M. Droske, A. Weidlich, J. Hanika. *Hero Wavelength Spectral Sampling.* EGSR 2014.
- C. Wyman, P.-P. Sloan, P. Shirley. *Simple Analytic Approximations to the CIE XYZ Color Matching Functions.* JCGT 2(2), 2013.
- M. Born, E. Wolf. *Principles of Optics*, sections 1.5 (Fresnel) and 1.6 (thin films).
