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

All boolean loading options default to true: textures, materials, entities, skins, animations, mipmaps, and sRGB albedo preference. `flatten_hierarchy` defaults to false, `max_texture_size` to 0, and `name_prefix` to empty (the loader then uses the file stem). The [result/options reference](../api/python/gltf-result.md) lists names in both languages.

Default loading preserves the node tree and shares mesh-space GPU buffers among repeated mesh references in that load. Renderable primitive entities are children of node entities. Move an appropriate node ancestor to move the whole object. The facade result lists renderable entities; native `GltfLoadResult` additionally exposes node and root lists.

`flatten_hierarchy=True` bakes node transforms into vertices and emits flattened geometry, losing that sharing. See [instancing](instancing.md) before choosing it.

`max_texture_size` currently reports oversized textures without resizing them. `srgb_albedo` is exposed as an option, but the loader selects formats by texture slot rather than consulting that flag. Treat these as implementation limitations, not working texture-conversion controls.

## Materials and animation

Use a pipeline matching the loaded geometry: PBR shaders and IBL resources for PBR/IBL scenes; skinned vertex formats, skeleton bindings, and execution for animated meshes. Loading an animation does not make an ordinary static-mesh pass perform skinning.

See [materials](materials.md), [lighting/IBL](lighting-and-ibl.md), and [animation](animation.md). glTF loading does not provide a documented physics-metadata import contract; configure physics separately.

## Meshes from arrays

`engine.create_mesh(positions, indices=None, normals=None, uvs=None, colors=None, name="")` makes an entity from triangles generated in code, without a file:

```python
import numpy as np

positions = np.array([(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)], dtype=np.float32)
quad = engine.create_mesh(positions, indices=[0, 3, 1, 1, 3, 2], uvs=[(0, 0), (0, 1), (1, 0), (1, 1)])
engine.scene.set_material_vec4(quad, "baseColorFactor", (0.8, 0.2, 0.2, 1.0))
```

Positions, normals and colours are (x, y, z) or (r, g, b) per vertex, uvs (u, v): lists of tuples, N x k arrays or flat lists. Every three indices make a triangle, counter-clockwise seen from its front; without indices every three vertices do. Without normals each vertex gets the area-weighted normal of the triangles it is in: smooth where triangles share vertices, flat where they do not. Without colours the vertices are white; the default pipeline multiplies its albedo by them, as it does a glTF file's `COLOR_0`.

The entity is like one primitive of a loaded glTF file: it has an identity transform, the default material (white, metallic 0, roughness 0.5) and casts and receives shadows; it is drawn by the same passes, culled by its bounds, and on a device with ray queries traced. `engine.set_mesh(entity, positions, ...)` replaces its triangles, keeping material and transform; the old geometry is freed once no frame in flight draws it. Both upload by waiting for the GPU, so a mesh that changes every frame costs a wait every frame. Arrays that do not fit, such as an index past the last vertex, raise `ValueError`. In C++, `EngineAPI::createMesh` and `setMesh` take the same arrays flat.
