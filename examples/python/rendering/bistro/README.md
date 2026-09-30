# Bistro

A tech demo of the [default pipeline](../../../../python/shoonyakasha/pipelines/default/README.md) on a production-sized scene: Amazon Lumberyard's [Bistro](https://developer.nvidia.com/orca/amazon-lumberyard-bistro), a Paris street corner with cafés, a bakery and a round plaza, 2.8 million triangles. It opens at dusk. The sun leaves the rooftops, the street lamps and wall lanterns come on one by one, then the strings of coloured bulbs over the café terraces. The camera walks from the north lane past the terraces and through the arcade into the plaza, and back.

```
python tools/fetch_assets.py bistro     # once, from the repository root: ~2 GB, then a few minutes converting
python bistro.py
```

Converting needs Pillow and numpy (`pip install pillow numpy`). Textures come out at most 2k; `--resolution 1k` or `4k` on the fetch changes that. The download is kept in `assets/bistro/source/`, so converting at another resolution doesn't download again.

## What it shows

| In the scene | Pipeline feature |
|---|---|
| 96 point lights: street lamps, lanterns, café lights, the strings of bulbs | Clustered lighting, every light live. **V** to *lights per cluster* shows how they spread |
| Lamp posts and bollards throwing shadows across the cobbles | The shadow atlas: each frame it picks the lamps nearest the camera that deserve a point shadow |
| The last of the sun on the facades | Sun shadows: ray traced where the GPU has ray queries (**R** switches to the cascades), cascaded otherwise |
| Coloured bulbs tinting the plaster around them | Emissive materials and bloom |
| Dusk to night | Automatic exposure, held back at night by a raised `exposureMinEV` |
| Under the awnings, between the chairs | Ambient occlusion and contact shadows |
| Hedges, ivy, flower boxes | Alpha-tested materials |

**Keys:** **N** day or night · **R** ray-traced or cascaded sun shadows · **1**–**3** quality presets · **Space** stop or resume the walk (then WASD/Q/E and the right mouse button fly the camera) · **V** debug views · **P** screenshot.

## How it is put together

- **[tools/bistro.py](../../../../tools/bistro.py)** downloads NVIDIA's glTF edit of the Bistro and converts what the engine's loader doesn't read:
  - Its textures are BC7 DDS files (`MSFT_texture_dds`). They are decoded and saved as JPEG, or PNG where the alpha matters.
  - Its materials are specular-glossiness (`KHR_materials_pbrSpecularGlossiness`). They are turned into metallic-roughness per pixel, with the conversion the Khronos glTF tools use.
  - Its normal maps are CryEngine *ddna* maps, green pointing down. The green is flipped, and the gloss in their alpha goes into the roughness.
  - Its glass (`KHR_materials_transmission`) becomes a blended, mostly clear material.
  - Its mesh nodes are all called `subset_N`. They are renamed after their material, which is how the demo finds what glows.
- **[gltf_lights.py](gltf_lights.py)** reads the file's `KHR_lights_punctual` lights and places them in world space; the loader doesn't import lights. The demo creates an engine light for each, scaled from candela, with the pure white street lamps warmed to incandescent.
- **[bistro.py](bistro.py)** runs the dusk: one number from 0 to 1 that sets the sun, the sky light and the exposure floor, and switches each light and each glowing material on at its own point of it.

## Credits

Amazon Lumberyard Bistro, Open Research Content Archive (ORCA), <https://developer.nvidia.com/orca/amazon-lumberyard-bistro>, licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The glTF edit is from NVIDIA's [RTXDI assets](https://github.com/NVIDIAGameWorks/rtxdi-assets) (MIT), fetched from [zeux/niagara_bistro](https://github.com/zeux/niagara_bistro), which keeps it outside Git LFS. The sky: Poly Haven's Farm Sunset, CC0.

Anything that shows or redistributes the scene, screenshots and videos included, needs the Bistro credit above.
