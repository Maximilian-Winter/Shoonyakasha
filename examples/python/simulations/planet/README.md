# Planet

A planet of radius 100 km with procedural terrain (continents, oceans, ridged mountains, hills) on a cube-sphere with quadtree level of detail, flown around from a few metres above its ground to 5,000 km away. Its chunks are built on the GPU by a compute pass and drawn with indirect draws. It runs on its own [pipeline](pipeline/pipeline.json) with reverse-Z depth, a wireframe pass switched on with **F**, and a canvas overlay. Needs numpy and a GPU with 64-bit float shaders (`shaderFloat64`).

```
python planet.py
python planet.py --capture DIR     # screenshots at the presets, then exit
python planet.py --verify          # check the GPU's chunks against cube_sphere.chunk_mesh, then exit
```

| Key | |
|---|---|
| Right mouse + move | Look around; the horizon stays level |
| **W/A/S/D** | Fly forward, left, back, right |
| **E / Q** | Up / down |
| **Shift / Ctrl** | 4× faster / 4× slower |
| Mouse wheel | Speed factor |
| **1 – 5** | 5 m above the blocks, 1 km above them, then 50 km, 1,000 km, 5,000 km above the sphere |
| **6** | 8 km above the corner where the +X, +Y and +Z faces meet |
| **N** | New terrain seed |
| **M / J** | Mountains 25 % higher / lower |
| **H / G** | Hills 25 % higher / lower |
| **F** | Wireframe |
| **C** | Colour: terrain, one per cube face, one per quadtree level |
| **K** | Freeze the level of detail, to fly around the chunks it chose |
| **[ / ]** | Split factor down / up |
| **P** | Screenshot into `planet_captures/` |

Flight speed is half the height above the ground per second (at least 1 m/s), times the speed factor; the camera stays at least 0.3 m above the ground. The overlay shows the heights above the ground and above sea level (the sphere), speed and frame time; the chunks drawn, built and missing, the slots of the chunk pool in use, the deepest level drawn and the split factor; and the seed and the mountain and hill heights.

## How it works

- **Positions** are Python floats (64-bit), with the planet's centre at the origin. The engine's camera stays at its own origin and only turns. Every frame each chunk and entity is placed at its position minus the camera's, so the GPU only sees numbers that are small near the camera.
- **The projection** is reverse-Z with an infinite far plane (`scene.set_camera_reverse_z`): depth 1 at the 5 cm near plane and 0 at infinity. The passes compare with `greater_or_equal`, and the terrain pass clears depth to 0.
- **The sphere** ([`cube_sphere.py`](cube_sphere.py)) has six cube faces. Each face has a frame (normal, u axis, v axis) with u × v = normal, and a point normal + u·u_axis + v·v_axis, with u and v in [−1, 1], is mapped onto the sphere by `spherify`. A chunk is a square of a face at a quadtree level: 16 × 16 quads, its vertices relative to its own centre.
- **The terrain** ([`terrain.glsl`](pipeline/shaders/terrain.glsl)) is a height in metres above the sphere for each direction from the planet's centre: domain-warped fBm continents (below zero is sea floor, down to 1,500 m), ridged mountains in bands where a low-frequency mask allows them (3,000 m), and fBm hills (150 m), all from 3D gradient noise that hashes integer lattice points. Its parameters are custom values (`planet.terrain.*`) read by the `TerrainParams` uniform buffer. Around the blocks' site the ground is flat within 150 m, at the site's natural height, easing back into the natural terrain by 600 m.
- **Skirts:** each chunk's edge vertices have copies 5 % of the chunk's edge length lower, joined to the edge by walls drawn from both sides. They hide the cracks where chunks of different levels meet.
- **Building chunks on the GPU** ([`chunk_build.comp`](pipeline/shaders/chunk_build.comp)): `ChunkPool` is a storage buffer of 2048 slots of 357 vertices (the 17 × 17 grid, then 4 × 17 skirt vertices), each three `vec4`s: position and u, normal and v, direction and height. A chunk the quadtree wants takes a free slot and a job. Each frame Python sets up to 16 jobs (face, level, x, y, slot, skirt depth, stamp) as custom values, and the `BuildChunks` pass builds one job per workgroup of 64 threads; workgroups past the job count return at once.
  - Face coordinates, `spherify`, and the position direction × (radius + height) minus the chunk's centre are worked out in 64-bit floats and stored as 32-bit. Heights are 32-bit.
  - A vertex's normal comes from the terrain at four points a short arc away, along two tangents that depend on its direction alone, so a vertex shared by chunks has the same normal in each.
  - Each slot's lowest and highest grid heights go to `ChunkHeights` with the job's stamp, read back every frame. Python gives them to the chunk's quadtree node.
  - A 17th workgroup evaluates the terrain at the probe directions into `ProbeHeights`, read back every frame: the ground under the camera, and the site's natural height. Each result carries the terrain version it was made with; results of an older version are ignored.
  - A chunk is drawn in the frame its job runs, since `BuildChunks` comes first. Releasing a chunk frees its slot.
- **Drawing** (`Terrain`): one `draw_indexed_indirect` call with a count. Every chunk shares the 2304 indices of `ChunkIndices`, set once at start. Each frame Python writes `ChunkDraws`, the number of chunks to draw and then one command per chunk with `vertexOffset` at its slot's first vertex and `firstInstance` its slot, and `ChunkInstances`, each slot's centre minus the camera's position (subtracted in double) and its colour. [`terrain.vert`](pipeline/shaders/terrain.vert) reads the vertices from `ChunkPool` as its vertex buffer and adds `ChunkInstances[gl_InstanceIndex]`. The blocks are entities, drawn after it by `Blocks`.
- **Colours** ([`terrain.frag`](pipeline/shaders/terrain.frag)): sea floor darker with depth; sand below 40 m; dry ground, grass or forest by a moisture noise; rock where the slope is steep; snow above a snow line that falls from 2,500 m at the equator to 200 m at the poles. The other colour modes show one colour per cube face or per quadtree level, with lines at every power-of-two subdivision of a face ([`shading.glsl`](pipeline/shaders/shading.glsl)).
- **Level of detail** ([`quadtree.py`](quadtree.py)): each face is the root of a quadtree, down to level 12 (chunks 38 m across, quads 2.4 m). Every frame:
  - A node splits into its four children when the camera is within the split factor (1.25) times its edge length of its bounding sphere. A split node merges again only beyond 1.25 times that.
  - A node's bounding sphere holds its patch of the sphere raised and lowered through its height range, plus 5 % of its edge length. The range is the one its chunk's heights gave, or its parent's until they come back; the roots start with −2,000 to 6,000 m.
  - A node stays drawn until all four of its children are built, so there are no holes.
  - At most 16 missing chunks are built per frame: those in view first, nearest first. None are built while the pool is full.
  - Chunks no frame has used for 120 frames are released.
  - Every chunk the tree selects is drawn; none are culled.
- **Starting and changing the terrain:** the quadtree is made once a probe has returned the site's natural height, which becomes the pad's height. Changing a parameter (**N**, **M**, **J**, **H**, **G**) releases every chunk, measures the site again and builds the planet again.
- **The wireframe pass** draws the chunks again in line mode, with a slope-scaled depth bias (`"depthBias": {"slope": 1.5}`; positive is nearer with reverse-Z) so the lines pass the depth test against their own surface.
- **The blocks** stand on the pad on the +Z face at u = v = 1/64: a 20 m slab 5 cm above the ground, cubes of 0.5, 1 and 4 m, and a 30 m mast.

`tests/python/test_planet_cube_sphere.py` checks the face frames, `spherify`, the winding, the skirts, that neighbouring chunks share their edge vertices exactly, that the pipeline's chunk buffers fit the chunk layout, and the quadtree's selection, hysteresis, build order, bounds through a height range, and eviction. `--verify` reads `ChunkPool` back and checks every built chunk: directions and face coordinates equal to `cube_sphere.chunk_mesh`'s; positions equal to direction × (radius + height − skirt) − centre from its directions and the GPU's heights, to within one unit in the last place of the chunk's largest coordinate; normals of unit length on the outer side; and the lowest and highest heights in `ChunkHeights` equal to those in the pool. It exits with status 1 if any of these fails.
