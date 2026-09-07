# GltfResult and loading options

`engine.load_gltf_scene(path, **kwargs)` returns `GltfResult`. Always inspect `success` and `error`; an entity list alone is not a success indicator.

| Python result field | C++ facade field | Value |
|---|---|---|
| `success` | `success` | bool |
| `error` | `error` | Error string |
| `entities` | `entities` | Renderable entity handles |
| `total_vertices` | `totalVertices` | Vertex count |
| `total_indices` | `totalIndices` | Index count |
| `total_textures` | `totalTextures` | Texture count |
| `total_materials` | `totalMaterials` | Material count |
| `animation_clips` | `animationClips` | Python list of `(name, duration_seconds)` tuples; C++ `ClipInfo` structs |
| `skeleton_count` | `skeletonCount` | Skeleton count |

Native `GltfLoadResult` also exposes primitive, root/node, skeleton, and clip objects. Those native collections are not included in the facade result.

## Options

| Python keyword | C++ GltfOptions member | Default |
|---|---|---|
| `load_textures` | `loadTextures` | true |
| `load_materials` | `loadMaterials` | true |
| `create_entities` | `createEntities` | true |
| `load_skins` | `loadSkins` | true |
| `load_animations` | `loadAnimations` | true |
| `flatten_hierarchy` | `flattenHierarchy` | false |
| `max_texture_size` | `maxTextureSize` | 0 |
| `generate_mipmaps` | `generateMipmaps` | true |
| `srgb_albedo` | `srgbAlbedo` | true |
| `name_prefix` | `namePrefix` | empty string |

The current wrapper reads known keywords from `**kwargs`; unknown keywords are ignored rather than rejected. Check spelling. An empty name prefix becomes the input file stem in the native loader. `max_texture_size` only warns about oversized textures; `srgb_albedo` is not consulted by the current slot-based texture-format selection. See [loading scenes](../../guides/loading-scenes.md) for hierarchy and texture behavior.

Source: [FacadeTypes.h](../../../include/Facade/FacadeTypes.h), [Cython wrapper](../../../python/shoonyakasha/_shoonyakasha.pyx).
