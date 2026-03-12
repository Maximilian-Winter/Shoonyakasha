# GltfResult

> `shoonyakasha.GltfResult` -- Result object returned by
> [`Engine.load_gltf_scene()`](engine.md#load_gltf_scenepath-kwargs).

`GltfResult` is a plain Python class (not a Cython extension type) that holds
the outcome of a glTF load operation: whether it succeeded, the entity handles
created, and aggregate statistics about vertices, textures, materials,
skeletons, and animation clips.

```python
result = engine.load_gltf_scene("models/Sponza.gltf")
if result.success:
    print(f"Loaded {len(result.entities)} entities")
else:
    print(f"Error: {result.error}")
```

---

## Properties

### success

Whether the glTF file was loaded successfully.

**Type:** `bool`

If `False`, check `error` for a diagnostic message. The remaining fields may
be partially populated (e.g., some entities created before the failure).

---

### error

Error message describing why the load failed. Empty string on success.

**Type:** `str`

---

### entities

List of entity handles created from the glTF scene. Each handle is an unsigned
32-bit integer that can be passed to [`Scene`](scene.md) methods for transform,
material, and component manipulation.

**Type:** `list[int]`

---

### total_vertices

Total number of vertices across all meshes loaded from the file.

**Type:** `int`

---

### total_indices

Total number of indices across all meshes loaded from the file.

**Type:** `int`

---

### total_textures

Number of textures uploaded to the GPU from this file.

**Type:** `int`

---

### total_materials

Number of PBR materials parsed from the file.

**Type:** `int`

---

### animation_clips

List of animation clips found in the file. Each entry is a tuple of
`(name, duration)` where `name` is a `str` and `duration` is a `float`
in seconds.

**Type:** `list[tuple[str, float]]`

Only populated when `load_animations=True` (the default) is passed to
`load_gltf_scene()`.

---

### skeleton_count

Number of skeletons (skins) loaded from the file.

**Type:** `int`

Only populated when `load_skins=True` (the default) is passed to
`load_gltf_scene()`.

---

## Methods

### \_\_repr\_\_()

Returns a human-readable summary string. The format differs based on success:

- **On success:** `GltfResult(success=True, entities=42, vertices=128000, textures=16)`
- **On success with skeletons:** `GltfResult(success=True, entities=42, vertices=128000, textures=16, skeletons=1, clips=3)`
- **On failure:** `GltfResult(success=False, error='File not found')`

---

## Examples

### Basic scene loading

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_init():
    engine.create_camera((0, 5, 15))
    engine.create_directional_light((-0.5, -1.0, -0.3))

    result = engine.load_gltf_scene("scene.gltf")
    if not result.success:
        print(f"Failed to load scene: {result.error}")
        return

    print(f"Loaded {len(result.entities)} entities")
    print(f"  Vertices:  {result.total_vertices}")
    print(f"  Indices:   {result.total_indices}")
    print(f"  Textures:  {result.total_textures}")
    print(f"  Materials: {result.total_materials}")

engine.set_on_init(on_init)
engine.run()
```

### Iterating over created entities

```python
result = engine.load_gltf_scene("scene.gltf")
if result.success:
    scene = engine.scene
    for entity in result.entities:
        name = scene.get_name(entity)
        pos = scene.get_position(entity)
        print(f"  {name}: position={pos}")
```

### Loading with custom options

```python
result = engine.load_gltf_scene(
    "large_scene.glb",
    max_texture_size=2048,
    flatten_hierarchy=False,
    name_prefix="level1_",
)
print(result)  # GltfResult(success=True, entities=120, ...)
```

### Loading an animated model

```python
result = engine.load_gltf_scene("character.glb", load_skins=True, load_animations=True)
if result.success:
    print(f"Skeletons: {result.skeleton_count}")
    for name, duration in result.animation_clips:
        print(f"  Clip '{name}': {duration:.2f}s")

    # Play the first animation on the first entity
    if result.animation_clips and result.entities:
        scene = engine.scene
        entity = result.entities[0]
        scene.play_animation(entity, 0)
        scene.set_animation_looping(entity, True)
```

### Checking for failure

```python
result = engine.load_gltf_scene("nonexistent.gltf")
if not result.success:
    print(f"Load failed: {result.error}")
    # result.entities may be empty or partially filled
    print(f"Entities created before failure: {len(result.entities)}")
```

---

## See Also

- [Engine.load_gltf_scene()](engine.md#load_gltf_scenepath-kwargs) -- The method that returns `GltfResult`
- [Scene](scene.md) -- Manipulate entities returned in `result.entities`
- [Constants](constants.md) -- `NULL_ENTITY` sentinel for invalid entity handles
