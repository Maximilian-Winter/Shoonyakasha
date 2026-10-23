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

The glTF loader sets these parameters: `baseColorFactor`, `metallicFactor`, `roughnessFactor`, `emissiveFactor` (times `KHR_materials_emissive_strength`), `normalScale` (`normalTexture.scale`), `occlusionStrength` (`occlusionTexture.strength`), `transmissionFactor`, `ior`, and with their extensions the clear coat and iridescence factors. Each texture is sampled with its glTF sampler's filters (nearest or linear, with or without mipmaps) and wrap modes (repeat, clamp or mirrored repeat). The default pipeline reads `normalScale` and `occlusionStrength` from its per-draw constants (1 when a material has neither).

GLTF alpha mode/cutoff and double-sided data are imported. Correct transparent rendering also needs a transparent pass, blend/depth state, and ordering appropriate to the shader. The existence of material properties is not a guarantee that every supplied shader implements them.

References: [pipeline walkthrough](json-render-pipeline.md), [Scene API](../api/python/scene.md), [lighting and IBL](lighting-and-ibl.md), [native render components](../../include/ECS/RenderComponents.h).
