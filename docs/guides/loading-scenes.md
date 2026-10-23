# Loading scenes

Load `.gltf` or `.glb` through `engine.load_gltf_scene` / `EngineAPI::loadGltfScene` during init. The native loader is based on cgltf and uploads geometry/textures, creates materials, and optionally creates ECS entities, skins, and animation data.

```python
result = engine.load_gltf_scene("models/Box.gltf", name_prefix="box_")
if result.success:
    print(result.entities, result.total_vertices)
else:
    print(result.error)
```

```cpp
Shoonyakasha::Facade::GltfOptions options;
options.namePrefix = "box_";
auto result = engine.loadGltfScene("models/Box.gltf", options);
if (!result.success) {
    // Report result.error to the user.
}
```

These are callback fragments, not complete programs. See the [quickstarts](../index.md) for pipeline and shader setup.

## Assets and paths

Paths first resolve as given, then against the shared asset root. The bundled box is a reliable first scene; Sponza is optional. Use `sk.assets.exists(path)` or C++ `assetExists(path)` before selecting an optional asset. See [assets](../../assets/README.md) for downloads and licenses. Shader and pipeline paths use the working directory instead.

## Options and hierarchy

All boolean loading options default to true: textures, materials, entities, skins, animations, mipmaps, and sRGB albedo preference. `load_lights`, `flatten_hierarchy` and `compress_textures` default to false, `max_texture_size` to 0, and `name_prefix` to empty (the loader then uses the file stem). The [result/options reference](../api/python/gltf-result.md) lists names in both languages.

Default loading preserves the node tree and shares mesh-space GPU buffers among repeated mesh references in that load. Renderable primitive entities are children of node entities. Move an appropriate node ancestor to move the whole object. The facade result lists renderable entities; native `GltfLoadResult` additionally exposes node and root lists.

`flatten_hierarchy=True` bakes node transforms into vertices and emits flattened geometry, losing that sharing. See [instancing](instancing.md) before choosing it.

Vertex attributes, morph targets and animation samplers stored as integers or normalized integers (`KHR_mesh_quantization`) are read as floats while loading. Buffer views compressed with `EXT_meshopt_compression` or `KHR_meshopt_compression` are decoded with meshoptimizer while loading, filters included; a file whose compressed data cannot be decoded fails to load with the buffer view named in `error`. Primitives compressed with `KHR_draco_mesh_compression` are decoded with Google's Draco library: each attribute keeps its accessor's component type, and the indices become 32-bit; one that fails to decode fails the load, named in `error`.

## Lights

`load_lights=True` (C++ `GltfOptions::loadLights`) creates a light entity for each node with a `KHR_lights_punctual` light, listed in the result's `lights`. It is a child of the node's entity and shines along the node's -Z (with `flatten_hierarchy`, an unparented entity at the node's world position and direction). Directional, point and spot lights keep the file's colour and intensity as they are: candela for point and spot lights, lux for directional ones, with inverse-square falloff. A light with a `range` fades to nothing there; one without reaches as far as its intensity times its brightest colour channel, over the distance squared, stays above 0.01. Spot cone angles become the light's inner and outer half angles. The lights cast no shadows until `scene.set_light_cast_shadows(light, True)`; the first directional light with shadows is the sun in the default pipeline.

## Texture size and compression

Textures load through stb_image (PNG, JPEG and the other formats it reads) as 8-bit RGBA. `max_texture_size=N` halves a texture wider or taller than N until it fits, averaging 2 x 2 texels; colour textures average in linear light. `srgb_albedo` is exposed as an option, but the loader selects formats by texture slot rather than consulting that flag.

`compress_textures=True` stores the textures block-compressed, where the GPU samples BC formats (otherwise it has no effect):

| Texture | Format | Bytes per texel |
|---|---|---|
| Base colour, emissive, sheen colour, specular colour | BC1 sRGB; BC3 sRGB when any texel is transparent | 0.5; 1 |
| Normal map | BC5: x and y | 1 |
| Metallic-roughness, occlusion, clear coat, iridescence, transmission, thickness, anisotropy | BC1 | 0.5 |
| Sheen roughness, specular strength (in alpha) | BC1; BC3 when any texel's alpha is below 1 | 0.5; 1 |

Uncompressed textures take 4 bytes per texel, so this is a quarter to an eighth of the memory, at some loss of quality: BC1 stores each 4 x 4 block as two 16-bit colours and blends between them, which bands smooth gradients. Compression runs on the CPU while loading (stb_dxt), and so do the mipmaps, so loading takes longer. A BC5 normal map holds only x and y; the default pipeline and `sk/scene_hits.glsl` compute z from them, and a pipeline's own shaders need to do the same: `z = sqrt(max(1 - dot(xy, xy), 0))`.

A texture with a `KHR_texture_basisu` source is read from its KTX2 file with libktx (ETC1S and UASTC, with or without supercompression). A KTX2 file with mip levels is transcoded level by level to BC7, sRGB for colour textures, where the GPU samples BC formats, and to 8-bit RGBA otherwise; `max_texture_size` skips the levels larger than it. A KTX2 file with one level is transcoded to 8-bit RGBA and then treated like a PNG: resized, mipmapped and, with `compress_textures`, compressed as above. When the KTX2 file cannot be read, the texture's own `source` image is used if it has one.

## Levels of detail

`lods=True` (C++ `GltfOptions::generateLods`) gives each static triangle mesh of at least 64 triangles up to four coarser levels of detail, simplified with meshoptimizer to about half the triangles of the level before. They share the mesh's vertices and follow its full index list in the same index buffer (`MeshComponent::lods`), and the mesh's open borders stay in place. Each level records how far its surface may lie from the full mesh's, counting how far its normals stray. Skinned and morphed meshes get none.

Each frame, entity geometry passes draw every such entity at the coarsest level whose error, seen from the main camera at the nearest point of the entity's bounds, covers at most `set_lod_threshold(pixels)` pixels of the render height (default 1; 0 draws every mesh in full). Every pass that frame, shadow passes included, draws the level the main camera picked. Ray-traced shadows and reflections use the full mesh. `get_lod_counts()` reports how many entities each level drew. Generating the levels adds to loading time.

## Materials and animation

Use a pipeline matching the loaded geometry: PBR shaders and IBL resources for PBR/IBL scenes; skinned vertex formats, skeleton bindings, and execution for animated meshes. Loading an animation does not make an ordinary static-mesh pass perform skinning.

Vertices carry `TEXCOORD_0` and `TEXCOORD_1`. Every vertex carries a tangent for normal maps (the [vertex layouts](../reference/pipeline-json.md#vertex-formats)). The loader reads a primitive's `TANGENT` attribute, or generates MikkTSpace tangents from its `TEXCOORD_0` when it has none; a vertex whose triangles get different tangents, on a UV seam or where UVs are mirrored, becomes one vertex per tangent. Primitives without uvs get no tangent.

See [materials](materials.md), [lighting/IBL](lighting-and-ibl.md), and [animation](animation.md). glTF loading does not provide a documented physics-metadata import contract; configure physics separately.

## Meshes from arrays

`engine.create_mesh(positions, indices=None, normals=None, uvs=None, colors=None, name="", tangents=None)` makes an entity from triangles generated in code, without a file:

```python
import numpy as np

positions = np.array([(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)], dtype=np.float32)
quad = engine.create_mesh(positions, indices=[0, 3, 1, 1, 3, 2], uvs=[(0, 0), (0, 1), (1, 0), (1, 1)])
engine.scene.set_material_vec4(quad, "baseColorFactor", (0.8, 0.2, 0.2, 1.0))
```

Positions, normals and colours are (x, y, z) or (r, g, b) per vertex, uvs (u, v): lists of tuples, N x k arrays or flat lists. Every three indices make a triangle, counter-clockwise seen from its front; without indices every three vertices do. Without normals each vertex gets the area-weighted normal of the triangles it is in: smooth where triangles share vertices, flat where they do not. Without colours the vertices are white; the default pipeline multiplies its albedo by them, as it does a glTF file's `COLOR_0`. Tangents are (x, y, z, w) per vertex, as in a glTF file's `TANGENT`; without them a mesh with uvs gets MikkTSpace tangents, as a loaded one does.

The entity is like one primitive of a loaded glTF file: it has an identity transform, the default material (white, metallic 0, roughness 0.5) and casts and receives shadows; it is drawn by the same passes, culled by its bounds, and on a device with ray queries traced. `engine.set_mesh(entity, positions, ...)` replaces its triangles, keeping material and transform; the old geometry is freed once no frame in flight draws it. Both upload by waiting for the GPU, so a mesh that changes every frame costs a wait every frame. Arrays that do not fit, such as an index past the last vertex, raise `ValueError`. In C++, `EngineAPI::createMesh` and `setMesh` take the same arrays flat.
