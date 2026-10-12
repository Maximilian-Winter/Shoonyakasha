# Volume Tiled Forward Shading

Jeremiah van Oosten's *Volume Tiled Forward Shading* (2015), with up to a million point lights moving through Crytek Sponza, the scene the paper measured in. It runs on its own [pipeline](pipeline/README.md), a copy of the default pipeline in which forward shading replaces the deferred lighting.

```
python volume_tiled.py                          # 16,384 lights
python volume_tiled.py --lights 262144
python volume_tiled.py --benchmark results.md   # the paper's measurement
```

**Sponza:** the scene is Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it the example builds a colonnade of boxes. `--scene any.gltf` lights another scene: the lights fill its bounds, or the volume given with `--box x0 y0 z0 x1 y1 z1`.

## The technique

Clustered shading cuts the view frustum into small volumes and lists, for each one, the lights that reach it, so each pixel only loops over its own volume's lights. Volume Tiled Forward Shading adds three things:
- **The grid.** Screen tiles of 64 × 64 pixels are cut in depth into slices whose thickness grows with distance, so every volume tile is about a cube (the paper's equations 8.1 and 8.2). The grid follows the resolution and the camera's planes and field of view.
- **Active clusters only.** A depth prepass comes first, then only the clusters that visible pixels (and transparent fragments) lie in are gathered into a list. Lights are assigned to those clusters alone, a small fraction of the grid.
- **A light BVH.** The lights get Morton codes from their view positions, are sorted by them and built into a 32-ary bounding volume hierarchy. Each active cluster walks the tree instead of testing every light, so the cost of assigning grows with the logarithm of the light count instead of with the count itself.

Then the scene is shaded forward: each pixel finds its cluster and loops over that cluster's lights.

## Using it

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **+** / **-** | Twice or half the lights (256 to 1,048,576) |
| **B** | Light assignment through the BVH, or testing every light (the paper's method without the BVH) |
| **V** | Image, lights per cluster, clusters, reference |
| **M** | Lights moving or still |
| **C** | The active clusters' boxes over the image, as lines |
| **F2** | The panel on or off |
| **P** | Screenshot into `volume_tiled_captures/` |

The panel at the left does the same and shows each stage's GPU time, from the engine's [render stats](../../../../docs/guides/render-stats.md). *Range for about 8 lights per point* sets the range at which, on average, 8 lights reach any point of the volume they fill. That is the range the example starts with, and the one the benchmark uses.

The views:
- **Lights per cluster:** blue for none, green and red for more. Magenta is a cluster whose list is full: more than 1,024 lights reach it.
- **Clusters:** each cluster in its own colour.
- **Active clusters' boxes** (**C**): each active cluster's view-space box, drawn with one instance per cluster through an indirect draw whose instance count the GPU writes.
- **Reference:** every light at every pixel, without clusters. It is as slow as forward shading without culling, so use it with a few thousand lights. It should look exactly like the image, which is how the culling was checked (below).

## Benchmark

`--benchmark FILE` measures each light count at the opening view, with the BVH and, up to `--all-up-to` lights (65,536 by default), without it:
- **Density:** the range is set for about 8 lights per point, so the lights get smaller as there are more of them, like the paper's fixed-density runs.
- **Timing:** each run settles for 1.5 seconds, then the render stats of 3 seconds are averaged.
- **Output:** a Markdown table of GPU milliseconds per stage.

One run of it, on an NVIDIA GeForce RTX 4060 Ti at 1920 × 1080 (selected columns; frame is the whole GPU frame, shadows, ambient occlusion, TAA and bloom included):

| Lights | Assignment | Range (m) | Sort | BVH build | Assign lights | Forward shading | Frame |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1,024 | BVH | 1.851 | 0.154 | 0.026 | 0.028 | 0.780 | 3.79 |
| 1,024 | every light | 1.851 | 0.025 | 0.010 | 0.027 | 0.772 | 3.62 |
| 16,384 | BVH | 0.734 | 0.173 | 0.039 | 0.047 | 0.915 | 4.06 |
| 16,384 | every light | 0.734 | 0.023 | 0.007 | 0.127 | 0.938 | 3.94 |
| 65,536 | BVH | 0.463 | 0.216 | 0.064 | 0.064 | 1.287 | 4.50 |
| 65,536 | every light | 0.463 | 0.025 | 0.010 | 0.463 | 1.237 | 4.61 |
| 262,144 | BVH | 0.291 | 0.554 | 0.050 | 0.098 | 2.393 | 5.99 |
| 1,048,576 | BVH | 0.184 | 1.824 | 0.109 | 0.140 | 4.891 | 10.07 |

What it shows:
- **Assignment with and without the BVH.** Through the BVH, assignment grows from 0.03 to 0.14 ms while the lights grow a thousandfold. Testing every light grows with the count: 0.46 ms at 65,536 already, and the rest of the frame is the same.
- **Up to 16,384 lights,** testing every light is cheaper overall: the sort costs more than the BVH saves. By 65,536 the BVH is cheaper.
- **At a million lights, the sort is the largest part of building the tree,** as in the paper. There, VTFS with the BVH took 9.50 ms for a million lights on a GTX Titan X, here 10.07 ms; the numbers are not comparable beyond that, with other GPUs, shading and settings.
- **Forward shading grows even at a fixed density.** The clusters stay the size of a screen tile while the lights shrink, so each cluster lists more lights that reach only part of it.

In the "every light" rows, the sort and BVH build columns are those passes disabled: a disabled pass still runs its barriers.

## How it was checked

- **BVH against every light:** with the lights still and TAA off, the two assignments give the same image to within 1 of 255 (the order the lights are added in) at 16,384 lights.
- **Clusters against no clusters:** the image matches the reference view, every light at every pixel, exactly at 2,048 lights, and to within 1 of 255 at 32,768 lights looking up into the vaults. A light missing from a cluster, or a cluster box too small, would show there.
- **Validation layers:** with `SHOONYAKASHA_VALIDATION=1` the pipeline reports what the default pipeline reports: nothing beyond unused vertex attributes in the shadow passes.

## Compared with the paper and the earlier DirectX 12 version

The paper's algorithm, with these differences:
- **Marking opaque clusters.** The paper draws the opaque geometry a second time with early depth testing to mark clusters. Here one compute thread per depth texel marks the same clusters without drawing. Transparent geometry is drawn, as in the paper, with early depth testing against the prepass.
- **Assignment workgroups.** As in the paper, one workgroup per active cluster, dispatched indirectly (`dispatch_indirect`) from the count the cluster gathering wrote. Past 65,535 active clusters the workgroups continue in a second dimension, since Vulkan only promises 65,535 across one.
- **A thin prepass.** Besides depth it writes normals and motion vectors, so the default pipeline's sun shadow mask, ambient occlusion and TAA run unchanged.
- **Point lights.** The VTFS lights are point lights without shadows, made on the GPU from their index. The scene's own lights (a sun and up to 128 more) are shaded as in the default pipeline, without clusters.

Problems of the earlier DirectX 12 implementation that this one avoids:
- **Occluded fragments:** clusters are marked only by visible pixels, not by every rasterised fragment.
- **Merge passes:** a run without a partner is copied, so an odd number of runs ends up sorted too.
- **Empty BVH nodes:** a node bounds only children that bound lights, never empty nodes left at the origin.
- **Reductions:** they use barriers, not the 32 threads of a warp running in step.
- **Slice index:** it is clamped to the grid.
- **Light lists:** overflow of a cluster's list and of the global list is caught.
- **No CPU wait:** nothing waits for the GPU in the middle of a frame.

## Limits

- **Capacities:** fixed in `pipeline/shaders/vtfs.glsl` and the buffer sizes in `pipeline.json`. They are 1,048,576 lights; 524,288 clusters (3840 × 2160 at 256 slices); 1,024 lights per cluster; 8,388,608 entries in the light index list.
  - The light view shows a full cluster in magenta.
  - A full index list leaves later clusters without lights. Python cannot read the overflow flags back.
- **Fixed pass counts:** the sort always runs its 12 merge passes, and the BVH build its 3 upper levels. Those the light count does not need copy or return at once.
- **Brute force at high counts:** without the BVH, assignment slows down with the light count. Hundreds of thousands of lights take seconds per frame.
