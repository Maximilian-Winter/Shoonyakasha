# Materials

How surface appearance is defined in Shoonyakasha using PBR materials, texture
maps, and alpha modes. Python examples are shown first, with C++ equivalents
below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a scene with
> loaded geometry.

---

## PBR Material Model

Shoonyakasha uses the **metallic-roughness** PBR workflow, the same model used
by glTF 2.0. Every renderable entity has a `MaterialComponentV5` that stores
scalar/vector parameters and texture maps.

The key parameters:

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `baseColorFactor` | vec4 | `[0, 1]` per channel | Base color and alpha. Multiplied with the albedo texture if present. Default `(1, 1, 1, 1)`. |
| `metallicFactor` | float | `0.0` -- `1.0` | How metallic the surface is. `0` = dielectric (plastic, wood), `1` = metal (gold, chrome). Default `1.0`. |
| `roughnessFactor` | float | `0.0` -- `1.0` | Surface roughness. `0` = perfectly smooth (mirror), `1` = completely rough (matte). Default `1.0`. |
| `emissiveFactor` | vec3 | `[0, +inf)` per channel | Light emitted by the surface. `(0, 0, 0)` = no emission. Values above 1 produce bloom in HDR pipelines. |
| `alphaCutoff` | float | `0.0` -- `1.0` | Threshold for alpha mask mode. Fragments with alpha below this value are discarded. Default `0.5`. |

These values interact with lighting. `baseColorFactor` defines what color light
is reflected (diffuse for dielectrics, specular tint for metals). `roughnessFactor`
controls how sharp or blurry reflections appear. `metallicFactor` determines the
balance between diffuse and specular response.

---

## Material Textures

Textures provide per-pixel detail that scalar parameters alone cannot capture.
Each texture slot has a name used as a key in the material's texture map.

| Slot Name | Content | Format Notes |
|-----------|---------|-------------|
| `albedoMap` | Base color (RGB) + optional alpha (A) | sRGB color space. Multiplied by `baseColorFactor`. |
| `normalMap` | Tangent-space surface normals | Linear, RGB. `(0.5, 0.5, 1.0)` = flat surface. Adds detail like bumps, scratches, pores without extra geometry. |
| `metallicRoughnessMap` | Packed metallic + roughness | Linear, G channel = roughness, B channel = metallic. Matches the glTF convention. |
| `aoMap` | Ambient occlusion | Linear, single channel (R). Darkens crevices and contact areas. `1.0` = fully lit, `0.0` = fully occluded. |
| `emissiveMap` | Emission color | sRGB. Multiplied by `emissiveFactor`. Defines which parts of the surface glow. |

The shader checks whether each texture actually exists using the
`entity.material.textures.<name>.exists` dot-path. If a texture slot holds a
fallback/default texture, `exists` is `false` and the shader skips the
corresponding sampling logic (for example, normal mapping is disabled when no
normal map is present).

---

## Alpha Modes

Materials support three transparency modes, controlled by the `alphaMode` field
on `MaterialComponentV5`:

| Mode | Enum Value | Behavior | Performance |
|------|-----------|----------|-------------|
| **Opaque** | `AlphaMode::Opaque` (0) | Alpha channel is ignored. The surface is fully solid. | Fastest. Rendered in the GBuffer pass with depth write. Front-to-back sorting. |
| **Mask** | `AlphaMode::Mask` (1) | Binary transparency. Fragments with alpha below `alphaCutoff` are discarded entirely. | Fast. Rendered in the opaque pass with depth write. Good for foliage, fences, hair cards. |
| **Blend** | `AlphaMode::Blend` (2) | True semi-transparency with alpha blending. | Slowest. Rendered in a separate forward transparency pass, sorted back-to-front. |

In the default JSON pipeline:

- **Opaque and Mask** materials are rendered by the `GBufferPass` (execution
  type `opaque_geometry`). The material's `alphaCutoff` is passed to the shader
  via push constants.
- **Blend** materials are rendered by the `ForwardTransparencyPass` (execution
  type `transparent_geometry`, sorted back-to-front with alpha blending enabled).

The `MaterialComponentV5` provides helper methods to query alpha mode:

```cpp
mat.isOpaque();         // alphaMode == Opaque
mat.isMasked();         // alphaMode == Mask
mat.isTransparent();    // alphaMode == Blend
mat.isOpaqueOrMasked(); // alphaMode != Blend
```

---

## Setting Material Parameters (Python)

Use the typed `Scene` methods to set or get material parameters on any entity
that has a `MaterialComponentV5`:

```python
scene = engine.scene

# Set scalar parameter
scene.set_material_float(entity, "roughness", 0.3)
scene.set_material_float(entity, "metallicFactor", 0.0)

# Set vector parameters
scene.set_material_vec3(entity, "emissiveFactor", (1.0, 0.5, 0.0))
scene.set_material_vec4(entity, "baseColorFactor", (1.0, 0.0, 0.0, 1.0))
```

The parameter name is a string key into the component's `params` map. You can
use any name -- the standard PBR names are listed in the
[Common Material Parameters](#common-material-parameters) table below.

---

## Reading Material Parameters (Python)

```python
scene = engine.scene

# Read with a default fallback value
roughness = scene.get_material_float(entity, "roughness", 0.5)
base_color = scene.get_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
emissive = scene.get_material_vec3(entity, "emissiveFactor", (0.0, 0.0, 0.0))

# Check if a parameter exists
has_roughness = scene.has_material_param(entity, "roughness")
```

The getter methods accept a default value that is returned if the parameter has
not been set on the entity. This is useful for reading materials that may or may
not have been loaded from a glTF file.

---

## Setting Material Parameters (C++)

### Facade API

```cpp
auto& scene = engine.getScene();

scene.setMaterialFloat(entity, "roughness", 0.3f);
scene.setMaterialVec3(entity, "emissiveFactor", glm::vec3(1.0f, 0.5f, 0.0f));
scene.setMaterialVec4(entity, "baseColorFactor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

float roughness = scene.getMaterialFloat(entity, "roughness", 0.5f);
bool exists = scene.hasMaterialParam(entity, "roughness");
```

### Direct ECS Access

```cpp
auto& mat = registry.get<MaterialComponentV5>(entity);

// Using the generic param map
mat.params["baseColorFactor"] = MaterialParam::from(glm::vec4(1, 0, 0, 1));
mat.params["metallicFactor"]  = MaterialParam::from(0.0f);
mat.params["roughnessFactor"] = MaterialParam::from(0.3f);
mat.params["emissiveFactor"]  = MaterialParam::from(glm::vec3(1.0f, 0.5f, 0.0f));

// Reading
glm::vec4 color = mat.getParam<glm::vec4>("baseColorFactor", glm::vec4(1));
float metallic  = mat.getParam<float>("metallicFactor", 1.0f);

// Check existence
bool has = mat.hasParam("roughnessFactor");

// Alpha mode
mat.alphaMode = AlphaMode::Mask;
mat.alphaCutoff = 0.5f;
mat.doubleSided = true;
```

---

## glTF Material Import

When you load a glTF file, materials are automatically extracted and mapped to
`MaterialComponentV5` instances:

```python
result = engine.load_gltf_scene("model.gltf")
print(f"Loaded {result.total_materials} materials")
```

The loader performs the following mapping:

| glTF PBR Field | MaterialComponentV5 Param / Texture |
|----------------|--------------------------------------|
| `pbrMetallicRoughness.baseColorFactor` | `params["baseColorFactor"]` (vec4) |
| `pbrMetallicRoughness.metallicFactor` | `params["metallicFactor"]` (float) |
| `pbrMetallicRoughness.roughnessFactor` | `params["roughnessFactor"]` (float) |
| `pbrMetallicRoughness.baseColorTexture` | `textures["albedoMap"]` |
| `pbrMetallicRoughness.metallicRoughnessTexture` | `textures["metallicRoughnessMap"]` |
| `normalTexture` | `textures["normalMap"]` |
| `occlusionTexture` | `textures["aoMap"]` |
| `emissiveTexture` | `textures["emissiveMap"]` |
| `emissiveFactor` | `params["emissiveFactor"]` (vec3) |
| `alphaMode` | `alphaMode` (`Opaque`, `Mask`, or `Blend`) |
| `alphaCutoff` | `alphaCutoff` (float) |
| `doubleSided` | `doubleSided` (bool) |

Textures are loaded into GPU memory and stored in the component's `textures`
map. The `GPUTexture.exists` flag is set to `true` for textures that were
actually present in the glTF file, and `false` for default fallback textures.

You can control glTF loading behavior with options:

```python
result = engine.load_gltf_scene("model.gltf",
    load_textures=True,      # Load texture images (default True)
    load_materials=True,     # Parse material parameters (default True)
    srgb_albedo=True,        # Treat albedo as sRGB (default True)
    generate_mipmaps=True,   # Generate mipmaps for textures (default True)
    max_texture_size=2048,   # Cap texture dimensions (0 = no limit)
)
```

---

## How Materials Flow to Shaders

Material parameters reach the GPU through the JSON pipeline's dot-path system.
The default pipeline uses push constants for per-entity material data:

```json
"MaterialPushConstants": {
  "usage": "push_constant",
  "packing": "scalar",
  "fields": [
    { "name": "model",              "type": "mat4",  "source": "entity.transform.worldMatrix" },
    { "name": "baseColorFactor",    "type": "vec4",  "source": "entity.material.params.baseColorFactor" },
    { "name": "metallicFactor",     "type": "float", "source": "entity.material.params.metallicFactor" },
    { "name": "roughnessFactor",    "type": "float", "source": "entity.material.params.roughnessFactor" },
    { "name": "hasNormalMap",       "type": "float", "source": "entity.material.textures.normalMap.exists" },
    { "name": "hasMetalRoughMap",   "type": "float", "source": "entity.material.textures.metallicRoughnessMap.exists" },
    { "name": "alphaCutoff",        "type": "float", "source": "entity.material.alphaCutoff" }
  ]
}
```

The dot-path prefixes and what they resolve to:

| Dot-Path Prefix | Resolves To |
|-----------------|-------------|
| `entity.material.params.<name>` | Value from `MaterialComponentV5::params[name]` |
| `entity.material.textures.<name>.exists` | `1.0` if the texture was loaded from real data, `0.0` if it is a fallback |
| `entity.material.alphaCutoff` | `MaterialComponentV5::alphaCutoff` |
| `entity.transform.worldMatrix` | The entity's computed world-space transform |

Material textures (albedo, normal, metallic-roughness) are bound through
descriptor sets declared in the pipeline JSON:

```json
"materialSet": {
  "bindings": [
    { "binding": 0, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "albedoMap" },
    { "binding": 1, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "normalMap" },
    { "binding": 2, "type": "combined_image_sampler", "stages": ["fragment"],
      "name": "metallicRoughnessMap" }
  ]
}
```

The frame graph automatically binds the correct texture from the entity's
`MaterialComponentV5::textures` map based on the binding name.

---

## Per-Entity Overrides

Each entity has its own `MaterialComponentV5` instance. Modifying one entity's
material does not affect other entities, even if they were loaded from the same
glTF file or share the same model.

```python
scene = engine.scene

# Find two entities loaded from the same model
pillar_a = scene.find_entity_by_name("pillar_01")
pillar_b = scene.find_entity_by_name("pillar_02")

# Make pillar_a red -- pillar_b stays unchanged
scene.set_material_vec4(pillar_a, "baseColorFactor", (1.0, 0.0, 0.0, 1.0))

# Make pillar_b emissive -- pillar_a stays unchanged
scene.set_material_vec3(pillar_b, "emissiveFactor", (0.0, 2.0, 1.0))
```

This works because the glTF loader creates separate material component instances
for each entity, even when the source glTF references the same material index.

---

## Common Material Parameters

The standard PBR parameter names recognized by the default pipeline and glTF
loader:

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `baseColorFactor` | vec4 | `(1, 1, 1, 1)` | Base color multiplier. RGB = color, A = opacity. Multiplied with `albedoMap` if present. |
| `metallicFactor` | float | `1.0` | Metallic weight. `0` = dielectric, `1` = metallic. Multiplied with the B channel of `metallicRoughnessMap`. |
| `roughnessFactor` | float | `1.0` | Roughness weight. `0` = smooth mirror, `1` = fully diffuse. Multiplied with the G channel of `metallicRoughnessMap`. |
| `emissiveFactor` | vec3 | `(0, 0, 0)` | Emission color and intensity. Multiplied with `emissiveMap` if present. Values above 1 produce HDR bloom. |

Additional parameters can be added to the `params` map with arbitrary names.
They become accessible in the JSON pipeline via `entity.material.params.<name>`
dot-paths, provided you also add corresponding fields to the push constant or
UBO layout in your pipeline JSON.

---

## Complete Example

Loading a scene and tweaking materials at runtime:

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Materials Demo",
    width=1920, height=1080,
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="cubemaps_hdrs/kloofendal_28d_misty_8k.hdr",
)

def on_init():
    engine.create_camera(pos=(0, 5, 15), fov=60.0)
    engine.create_directional_light((-0.5, -1.0, -0.3), (1, 1, 1), 3.0)
    engine.load_gltf_scene("scene.gltf")

    scene = engine.scene

    # Make a specific entity look like polished gold
    statue = scene.find_entity_by_name("statue_01")
    if statue != sk.NULL_ENTITY:
        scene.set_material_vec4(statue, "baseColorFactor", (1.0, 0.84, 0.0, 1.0))
        scene.set_material_float(statue, "metallicFactor", 1.0)
        scene.set_material_float(statue, "roughnessFactor", 0.15)

    # Make a window entity semi-transparent
    glass = scene.find_entity_by_name("window_glass")
    if glass != sk.NULL_ENTITY:
        scene.set_material_vec4(glass, "baseColorFactor", (0.8, 0.9, 1.0, 0.3))

engine.set_on_init(on_init)
engine.run()
```

---

## See Also

- [Scene API (Python)](../api/python/scene.md) -- `set_material_float`, `set_material_vec3`, `set_material_vec4`, `has_material_param`
- [Scene API (C++)](../api/cpp/scene-api.md) -- `setMaterialFloat`, `setMaterialVec3`, `setMaterialVec4`, `hasMaterialParam`
- [ECS Components Reference](../api/cpp/ecs-components.md) -- `MaterialComponentV5`, `AlphaMode`
- [GPU Types Reference](../api/cpp/gpu-types.md) -- `MaterialParam`, `GPUTexture`, `AlphaMode`
- [JSON Render Pipeline Guide](json-render-pipeline.md) -- push constants, descriptor sets, dot-path resolution
- [Lighting and IBL Guide](lighting-and-ibl.md) -- how lighting interacts with material properties
- [Loading Scenes Guide](loading-scenes.md) -- glTF loading options that affect material import
