# Planet

A cube-sphere of radius 100 km with quadtree level of detail, flown around from a few metres above its surface to 5,000 km away. It runs on its own [pipeline](pipeline/pipeline.json): an opaque pass with reverse-Z depth, a wireframe pass switched on with **F**, and a canvas overlay. Needs numpy.

```
python planet.py
python planet.py --capture DIR     # screenshots at the presets, then exit
```

| Key | |
|---|---|
| Right mouse + move | Look around; the horizon stays level |
| **W/A/S/D** | Fly forward, left, back, right |
| **E / Q** | Up / down |
| **Shift / Ctrl** | 4× faster / 4× slower |
| Mouse wheel | Speed factor |
| **1 – 5** | 5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km |
| **6** | 3 km above the corner where the +X, +Y and +Z faces meet |
| **F** | Wireframe |
| **C** | Colour: ground, one per cube face, one per quadtree level |
| **K** | Freeze the level of detail, to fly around the chunks it chose |
| **[ / ]** | Split factor down / up |
| **P** | Screenshot into `planet_captures/` |

Flight speed is half the altitude per second (at least 1 m/s), times the speed factor. The overlay shows altitude, speed and frame time, and the chunks drawn, built and missing, the deepest level drawn, and the split factor.

## How it works

- **Positions** are Python floats (64-bit), with the planet's centre at the origin. The engine's camera stays at its own origin and only turns. Every frame each drawn entity is placed at its position minus the camera's, so the GPU only sees numbers that are small near the camera.
- **The projection** is reverse-Z with an infinite far plane (`scene.set_camera_reverse_z`): depth 1 at the 5 cm near plane and 0 at infinity. The passes compare with `greater_or_equal`, and the opaque pass clears depth to 0.
- **The sphere** ([`cube_sphere.py`](cube_sphere.py)) has six cube faces. Each face has a frame (normal, u axis, v axis) with u × v = normal, and a point normal + u·u_axis + v·v_axis, with u and v in [−1, 1], is mapped onto the sphere by `spherify`. A chunk is a square of a face at a quadtree level: 16 × 16 quads, built with numpy, its vertices relative to its own centre. Normals point away from the planet's centre. The texture coordinates are the face's (u, v).
- **Skirts:** each chunk's edge vertices have copies 5 % of the chunk's edge length nearer the planet's centre, joined to the edge by walls drawn from both sides. They hide the cracks where chunks of different levels meet.
- **Level of detail** ([`quadtree.py`](quadtree.py)): each face is the root of a quadtree, down to level 12 (chunks 38 m across, quads 2.4 m). Every frame:
  - A node splits into its four children when the camera is within the split factor (1.25) times its edge length of its bounding sphere. A split node merges again only beyond 1.25 times that.
  - A node stays drawn until all four of its children are built, so there are no holes.
  - At most 8 missing chunks are built per frame: those in view first, nearest first.
  - Chunks no frame has used for 120 frames are destroyed.
  - Drawn chunks are shown with `scene.set_visible`; the engine culls those outside the view.
- **Grid lines** come from the face coordinates, at every power-of-two subdivision of a face: the face's edges, then 2, 4, … up to 65,536 squares across (2.4 m). Each level fades in once its lines are a few pixels apart. Only entities with the `gridStrength` material parameter set to 1 draw them.
- **The wireframe pass** draws the same entities in line mode, with a slope-scaled depth bias (`"depthBias": {"slope": 1.5}`; positive is nearer with reverse-Z) so the lines pass the depth test against their own surface.
- **The blocks** stand on the +Z face at u = v = 1/64, a vertex of every chunk from level 3 down, so they sit on the true sphere. A 20 m pad lies 5 cm above the ground, with cubes of 0.5, 1 and 4 m and a 30 m mast.

`tests/python/test_planet_cube_sphere.py` checks the face frames, `spherify`, the winding, the skirts, that neighbouring chunks share their edge vertices exactly, and the quadtree's selection, hysteresis, build order and eviction.
