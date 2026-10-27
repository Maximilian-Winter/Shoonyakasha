# Gray-Scott reaction-diffusion

Two chemicals on a 2D grid: A is fed in, B consumes A to make more of itself,
and both diffuse, B at half the rate of A. Two numbers, the feed rate f and the
kill rate k, decide whether the grid grows coral, dividing cells, fingerprints,
mazes or spots.

The scripts run without a window, on `sk.ComputeContext`, with the installed
`shoonyakasha` package. `gray_scott.comp` is compiled with glslc from the
Vulkan SDK on first use. They need NumPy, matplotlib and Pillow, and write
their results to `output/`.

```sh
python one_shot.py                      # one pattern at 1024 x 1024
python one_shot.py --preset maze --size 2048 --steps 20000
python animate.py                       # a pattern growing, as a GIF
python catalog.py                       # the whole (k, f) parameter space in one image
```

## The model

Each step updates every cell from its 3 x 3 neighbourhood:

```text
A' = A + (dA * laplacian(A) - A*B*B + f*(1 - A)) * dt
B' = B + (dB * laplacian(B) + A*B*B - (k + f)*B) * dt
```

with dA = 1, dB = 0.5, dt = 1, and a Laplacian weighting the cell -1, its four
edge neighbours 0.2 and its four corner neighbours 0.05. The grid wraps at its
edges. The shader, `gray_scott.comp`, takes f and k as push constants, each
as a pair of values: f runs from the first to the second over the rows, k over
the columns. Equal pairs give one (f, k) for the whole grid.

`gray_scott.py` holds the grid in two `GpuBuffer`s of (A, B) float pairs.
Every step reads one and writes the other, and the two swap roles, so the
grid stays on the GPU between steps; `state()` copies it back as a
`(height, width, 2)` array. `PRESETS` names eight (f, k) pairs by the
pattern each grows into: coral, mitosis, fingerprints, maze, spots, worms,
holes and chaos.

## The scripts

**`one_shot.py`** runs one preset for `--steps` steps and copies the grid back
once, at the end. It saves it as `output/<preset>.npy` and B, in a colour map,
as `output/<preset>.png`. The array opens in a notebook as it is:

```python
import numpy as np
import matplotlib.pyplot as plt

cells = np.load("output/coral.npy")    # (height, width, 2): A and B
plt.imshow(cells[..., 1], cmap="magma")
```

**`animate.py`** copies the grid back every `--every` steps, to make a frame of
`output/<preset>.gif`, and saves the last grid as `output/<preset>.exr`: A in
the red channel and B in the green, as 32-bit floats.

**`catalog.py`** runs one grid whose k grows from 0.045 on the left to 0.07 on
the right and whose f grows from 0.01 at the top to 0.09 at the bottom, seeded
everywhere, so each region settles into the pattern of its own rates. It saves
`output/catalog.png`, labelled with the rates and the presets, and B as
`output/catalog.exr`. The plot leaves out a thin border, where the wrapping
grid meets the rates of its opposite edge. Near the edge of a pattern's region
the map can differ from a grid with one (f, k): the worms preset lies where
the map has died out, but grows worms on its own.

See [compute shaders from Python](../../../../docs/guides/compute-and-data-flow.md#running-compute-shaders-directly).
