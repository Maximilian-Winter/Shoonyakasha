# Materials

The loader creates `MaterialComponentV5` values and textures from glTF. Material parameters become shader inputs through the pipeline's per-draw layout and material descriptor bindings. The material/shader names must agree; setting an unused parameter has no visible effect.

Inside init or update, for a loaded renderable entity:

```python
engine.scene.set_material_vec4(entity, "baseColorFactor", (1.0, 0.2, 0.1, 1.0))
engine.scene.set_material_float(entity, "metallicFactor", 0.0)
engine.scene.set_material_float(entity, "roughnessFactor", 0.7)
```

```cpp
// Include Facade/SceneAPI.h.
engine.getScene().setMaterialFloat(entity, "roughnessFactor", 0.7f);
```

## Binding contract

Use `entity.material.params.<name>` for typed parameter fields and `entity.material.textures.<slot>` for textures. The `.exists` suffix is available for texture-presence fields, and `.slot` gives the texture's slot in a [`sceneTextures`](../reference/pipeline-json.md#descriptors-and-samplers) array. Typical glTF slots include `albedoMap`, `normalMap`, `metallicRoughnessMap`, `aoMap`, and `emissiveMap`; each example uses only the slots its shaders declare.

The facade supports float, vec3, and vec4 material parameters, presence checks, and named texture assignment. Texture assignment returns a success boolean and requires the running engine's texture manager. It does not create a missing JSON descriptor binding.

The glTF loader sets these parameters: `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor` (times `KHR_materials_emissive_strength`), `normalScale` (`normalTexture.scale`), `occlusionStrength` (`occlusionTexture.strength`), `transmissionFactor`, `ior`, `thicknessFactor`, `attenuationDistance` (0 for an infinite distance), `attenuationColor` (`KHR_materials_volume`), and with their extensions the clear coat and iridescence factors. Each texture is sampled with its glTF sampler's filters (nearest or linear, with or without mipmaps) and wrap modes (repeat, clamp or mirrored repeat). The default pipeline reads `normalScale` and `occlusionStrength` from its per-draw constants (1 when a material has neither), and shades clear coat from `clearcoatFactor`, `clearcoatRoughnessFactor` and the `clearcoatMap` (r) and `clearcoatRoughnessMap` (g) textures.

A texture read from `TEXCOORD_1` or through `KHR_texture_transform` gives its material two `vec4` parameters, `<slot>UvRow0` and `<slot>UvRow1` (`albedoMapUvRow0`, for instance): rows that map `(u, v, 1)` to the uv sampled, the transform's offset, rotation and scale, with the uv set (0 or 1) in `UvRow0.w`. Textures that use `TEXCOORD_0` as it is get none. The default pipeline reads them for every texture it samples, through its `MaterialUV` material buffer; a normal map read through them takes its tangent frame from that uv's screen-space derivatives.

The default pipeline draws a static entity whose `transmissionFactor` is above 0 in its transparent pass, whatever its alpha mode, and leaves it out of the shadow maps and the ray-traced scene. It lets through the lit opaque scene behind it in place of its diffuse light: straight behind for a thin surface, or, with a `thicknessFactor`, where the ray refracted by `ior` leaves a volume that thick (in mesh units, times the entity's scale). Rougher surfaces blur it more. The base colour tints it, and with an `attenuationDistance` light turns `attenuationColor` over that distance. The `transmissionMap` (r) and `thicknessMap` (g) textures scale the factors. Blended and transmissive surfaces behind it are not seen through it.

GLTF alpha mode/cutoff and double-sided data are imported. Correct transparent rendering also needs a transparent pass, blend/depth state, and ordering appropriate to the shader. The existence of material properties is not a guarantee that every supplied shader implements them.

References: [pipeline walkthrough](json-render-pipeline.md), [Scene API](../api/python/scene.md), [lighting and IBL](lighting-and-ibl.md), [native render components](../../include/ECS/RenderComponents.h).
