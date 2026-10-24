# Planet

A cube-sphere of radius 100 km, flown around from a few metres above its surface to 5,000 km away. It runs on its own [pipeline](pipeline/pipeline.json): an opaque pass with reverse-Z depth, a wireframe pass switched on with **F**, and a canvas overlay showing altitude and speed.

```
python planet.py
python planet.py --capture DIR     # screenshots at the presets, with and without wireframe, then exit
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
| **C** | A colour per cube face |
| **P** | Screenshot into `planet_captures/` |

Flight speed is half the altitude per second (at least 1 m/s), times the speed factor.

## How it works

- **Positions** are Python floats (64-bit), with the planet's centre at the origin. The engine's camera stays at its own origin and only turns. Every frame each entity is placed at its position minus the camera's, so the GPU only sees numbers that are small near the camera.
- **The projection** is reverse-Z with an infinite far plane (`scene.set_camera_reverse_z`): depth 1 at the 5 cm near plane and 0 at infinity. The passes compare with `greater_or_equal`, and the opaque pass clears depth to 0.
- **The sphere** ([`cube_sphere.py`](cube_sphere.py)) has six cube faces. Each face has a frame (normal, u axis, v axis) with u × v = normal, and a point normal + u·u_axis + v·v_axis, with u and v in [−1, 1], is mapped onto the sphere by `spherify`. Each face is split into 8 × 8 chunks of 16 × 16 quads. Each chunk stores its vertices relative to its own centre. Normals point away from the planet's centre. The texture coordinates are the face's (u, v).
- **Grid lines** come from those coordinates, at every power-of-two subdivision of a face: the face's edges, then 2, 4, … up to 65,536 squares across (2.4 m). Each level fades in once its lines are a few pixels apart. Only entities with the `gridStrength` material parameter set to 1 draw them.
- **The wireframe pass** draws the same entities in line mode, with a slope-scaled depth bias (`"depthBias": {"slope": 1.5}`; positive is nearer with reverse-Z) so the lines pass the depth test against their own surface.
- **The blocks** stand on the +Z face, one quad from its centre along u and v, on a mesh vertex, so they sit on the true sphere. A 20 m pad lies 5 cm above the ground, with cubes of 0.5, 1 and 4 m and a 30 m mast.

`tests/python/test_planet_cube_sphere.py` checks the face frames, `spherify`, the winding, and that neighbouring chunks share their edge vertices exactly.
