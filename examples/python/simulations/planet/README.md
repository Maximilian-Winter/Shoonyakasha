# Planet

A sphere of radius 100 km, flown around from a few metres above its surface to 5,000 km away. It runs on its own [pipeline](pipeline/pipeline.json): one opaque pass with reverse-Z depth and a canvas overlay showing altitude and speed.

```
python planet.py
python planet.py --capture DIR     # a screenshot at each of the five presets, then exit
```

| Key | |
|---|---|
| Right mouse + move | Look around; the horizon stays level |
| **W/A/S/D** | Fly forward, left, back, right |
| **E / Q** | Up / down |
| **Shift / Ctrl** | 4× faster / 4× slower |
| Mouse wheel | Speed factor |
| **1 – 5** | 5 m above the blocks, 1 km, 50 km, 1,000 km, 5,000 km |
| **P** | Screenshot into `planet_captures/` |

Flight speed is half the altitude per second (at least 1 m/s), times the speed factor.

## How it works

- **Positions** are Python floats (64-bit), with the planet's centre at the origin. The engine's camera stays at its own origin and only turns. Every frame each entity is placed at its position minus the camera's, so the GPU only sees numbers that are small near the camera.
- **The projection** is reverse-Z with an infinite far plane (`scene.set_camera_reverse_z`): depth 1 at the 5 cm near plane and 0 at infinity. The pass compares with `greater_or_equal` and clears depth to 0.
- **The sphere** is 32 × 16 patches of 16 × 16 quads, 0.703125° per quad. Each patch stores its vertices relative to its own centre. Normals point away from the planet's centre. The texture coordinates are longitude and latitude in degrees.
- **Grid lines** come from those texture coordinates, every 10°, 1°, 0.1°, 0.01° and 0.001° (1.75 m). Each spacing fades in once its lines are a few pixels apart. Only entities with the `gridStrength` material parameter set to 1 draw them.
- **The blocks** stand at 0.703125° north and east, on a mesh vertex, so they sit on the true sphere. A 20 m pad lies 5 cm above the ground, with cubes of 0.5, 1 and 4 m and a 30 m mast.
