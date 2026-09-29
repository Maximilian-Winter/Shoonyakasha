# Alley

A tech demo of the [default pipeline](../../../../python/shoonyakasha/pipelines/default/README.md) on a real scene: a back alley between an apartment block and an old factory, built from [Poly Haven](https://polyhaven.com)'s CC0 models, with a fox wandering through. Press **N** and the alley goes from late afternoon to night over a few seconds: the sun reddens and sets, windows light up one by one, the lamps come on, and exposure follows.

```
python tools/fetch_assets.py alley      # once, from the repository root: ~120 MB
python alley.py
```

`--resolution 2k` on the fetch gets sharper textures (~300 MB).

## What it shows

| In the scene | Pipeline feature |
|---|---|
| Low sun raking across the factory's brick, the apartments' shadow climbing it | Sun shadows: ray traced where the GPU has ray queries (**R** switches to the cascades), soft penumbrae from the sun's disc |
| The chain-link gate's shadow on the street beyond it | Alpha-tested casters: the wire is `MASK`, so it casts through the cascades even in ray-traced mode |
| Wall lanterns, a security light, a street lamp, the stove's fire | Spot and point shadows in the atlas; bloom around the bulbs |
| Dusk to night | Automatic exposure, held back at night by a raised `exposureMinEV` so the street stays dark |
| The fox | Skinning, and TAA following it through motion vectors |
| Corners, door recesses, under the car | Ambient occlusion and contact shadows |
| Leaves and weeds | Alpha-tested materials |

**Keys:** **N** day or night · **R** ray-traced or cascaded sun shadows · **1**–**3** quality presets · **Space** stop or resume the camera's flight (then WASD/Q/E and the right mouse button fly it) · **V** debug views · **P** screenshot.

## How it is put together

- **[kit.py](kit.py)** writes new glTF files beside the downloaded ones:
  - *Building fronts:* Poly Haven's modular facades come as one file holding every module side by side. `Composer` places chosen modules on a 3 m grid, floor by floor, and every placement reuses the kit's own meshes and textures. The alley's two building fronts, the building beyond the gate and the gate itself are written this way.
  - *The ground:* a large quad textured with a Poly Haven asphalt set, repeating every 3 m.
- **Loader gaps, patched in the files:**
  - The fence's wire texture is scaled with `KHR_texture_transform`, which the engine's loader doesn't read, so `Composer` bakes the transform into the UVs.
  - Poly Haven's glTF downloads use JPEG colour maps, which lose the alpha of leaves, wire and glass. `tools/fetch_assets.py` fetches the PNG versions of those maps and points the files at them.
  - The facades' window glass is kept opaque, since there are no rooms behind it.
- **[alley.py](alley.py)** lays out the scene:
  - Props are placed with a small `Prop` wrapper that can hide one of a model's variants (several Poly Haven files hold a clean and a rusted version side by side).
  - Bulbs and windows glow through `emissiveFactor`. Poly Haven's lamps use `KHR_materials_emissive_strength`, which the loader doesn't read either.
  - The dusk is one number from 0 to 1 that sets the sun, the sky light, the lamps, the windows and the exposure floor every frame.

The composed files are written into the asset root beside the models they use (`assets/polyhaven/*/alley_*.gltf`), and rewritten on every start.

## Credits

Models and textures: [Poly Haven](https://polyhaven.com), CC0. That covers the modular urban apartments and factory facades, the modular fire escape and chain-link fence, the street lamps, the security light, the barrel stove, the covered car and the smaller props, and the asphalt texture. The fox: [Khronos glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets), CC0. The sky: Poly Haven's Farm Sunset, CC0.
