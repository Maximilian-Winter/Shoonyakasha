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
