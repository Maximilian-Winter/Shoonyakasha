# Scene lights

Up to 4096 point lights in Crytek Sponza, each one an ordinary scene entity, plus four spot lights with shadows sweeping the arcades, all on the default pipeline. The pipeline reads the scene's lights from a storage buffer the engine refills every frame (`SceneLights`, up to 8192 lights), so a scene is no longer held to 128.

```
python scene_lights.py                       # 2048 point lights
python scene_lights.py --lights 4096
python scene_lights.py --benchmark results.md
```

**Sponza:** Khronos's glTF version of Crytek Sponza. Clone [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets) into the repository root, where git ignores it. Without it the example builds a colonnade of boxes.

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **+** / **-** | Twice or half the point lights (128 to 4096) |
| **V** | Debug views: final image, cascades, shadow mask, normals, ambient occlusion, lights per cluster, motion vectors |
| **P** | Screenshot into `scene_lights_captures/` |

The lights are made with `engine.create_point_lights`, one call for all of them; their range is set so about 4 reach each point, and they get brighter as they get smaller. The panel shows how long making them took, the frame rate, and the GPU time of the passes the lights cost.

## Benchmark

`--benchmark FILE` runs each light count at the opening view for 1.5 seconds, then averages 3 seconds of render stats into a Markdown table. One run, on an NVIDIA GeForce RTX 4060 Ti at 1920 × 1080 (milliseconds):

| Point lights | Made in | Frame | GPU | CPU record | LightClusters | Lighting | SpotShadow | PointShadow |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 128 | 0.3 | 5.09 | 4.63 | 0.90 | 0.070 | 0.697 | 0.587 | 0.140 |
| 256 | 0.5 | 5.15 | 4.68 | 0.93 | 0.135 | 0.724 | 0.635 | 0.125 |
| 512 | 0.9 | 5.33 | 4.85 | 0.92 | 0.195 | 0.796 | 0.637 | 0.126 |
| 1024 | 1.9 | 5.46 | 4.98 | 0.90 | 0.379 | 0.818 | 0.583 | 0.139 |
| 2048 | 3.6 | 6.03 | 5.53 | 0.89 | 0.697 | 0.844 | 0.676 | 0.126 |
| 4096 | 7.2 | 6.72 | 6.20 | 0.90 | 1.556 | 0.901 | 0.554 | 0.153 |

What it shows:
- **On the CPU, the lights cost nothing that shows.** Every frame the engine packs every light and copies them into the buffer; the frame stays about 0.5 ms longer than the GPU's work at every count, and recording takes 0.9 ms throughout.
- **On the GPU, the clustering grows with the light count.** `LightClusters` tests every light against every one of its 3456 clusters, so it grows from 0.07 to 1.56 ms. The shading (`Lighting`) barely grows: a pixel shades its own cluster's lights, at most 63.
- **The spot lights' shadows cost the same at any count**, about 0.6 ms for the four.
- **Making the lights:** 4096 in 7.2 ms in one call. 4096 calls of `create_point_light` take about as long; what made many lights slow to create was a log line per light, which the engine no longer writes.

## Limits

The default pipeline's clustering is unchanged:
- **63 lights per cluster:** a cluster keeps at most 63 lights, and its clusters are large (16 × 9 tiles across the screen, 24 slices in depth), so where thousands of lights crowd, a cluster leaves some out. The lights-per-cluster view shows how full each one is.
- **Every light against every cluster:** the culling pass grows with the light count.

The [Volume Tiled Forward Shading example](../volume_tiled/README.md) shows a way to cull many more lights.
