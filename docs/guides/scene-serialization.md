# Scene Serialization

How to save and load scene state to JSON files. This enables save/load
functionality, level editors, and debugging workflows where you need to
capture and restore an exact scene configuration.

> **Prerequisite:** This guide assumes you have a scene with entities. See
> [Loading Scenes](loading-scenes.md) and
> [Entities and Components](entities-and-components.md) for setup.

---

## Save and Load API

### Python

```python
scene = engine.scene

# Save current scene state to a file
scene.save_to_file("my_scene.json")

# Load scene state from a file (replaces all current entities)
if scene.load_from_file("my_scene.json"):
    print(f"Loaded {scene.entity_count} entities")
```

### C++

```cpp
auto& scene = engine.scene();

// Save
scene.saveToFile("my_scene.json");

// Load
if (scene.loadFromFile("my_scene.json")) {
    // Scene restored
}
```

Both methods return `bool` -- `true` on success, `false` if the file could not
be written or parsed.

---

## What Gets Saved

The serializer walks every entity in the scene and writes out all recognized
component data as JSON:

| Component | Saved Data |
|-----------|------------|
| **Transform** | Position, rotation, scale (vec3 each) |
| **Name** | Entity name string |
| **Tag** | Entity tag string |
| **Camera** | Type (perspective/orthographic), FOV, near/far planes, aspect ratio, ortho size, main camera flag |
| **Light** | Type (directional/point/spot), color, intensity, range, inner/outer cone angles, shadow casting flag |
| **Hierarchy** | Parent entity ID, children entity IDs |

The scene file also stores the scene name and frame count at the time of saving.

### Example Output

A saved scene file looks like this:

```json
{
    "name": "default",
    "frameCount": 1024,
    "entities": [
        {
            "id": 0,
            "components": {
                "Transform": {
                    "position": [0.0, 5.0, 0.0],
                    "rotation": [0.0, 0.0, 0.0],
                    "scale": [1.0, 1.0, 1.0]
                },
                "Name": { "name": "Sun" },
                "Light": {
                    "type": "Directional",
                    "color": [1.0, 0.95, 0.9],
                    "intensity": 3.0,
                    "range": 100.0,
                    "innerCone": 0.0,
                    "outerCone": 0.0,
                    "castShadows": true
                }
            }
        }
    ]
}
```

---

## What Does NOT Get Saved

GPU resources are **not** included in the scene file:

- **Mesh vertex/index buffers** -- the actual geometry data uploaded to the GPU
- **Textures** -- image data and Vulkan texture handles
- **Materials** -- PBR parameters are part of the GPU material system, not the
  serialized ECS components
- **Shader programs and pipelines** -- these are managed by the render graph
- **Physics rigid bodies** -- Bullet3 simulation state is runtime-only

This means loading a scene file alone does **not** restore a fully renderable
scene. You must also reload the glTF models that provide the GPU data.

---

## Typical Workflow

The standard pattern is: load models first (to create GPU resources), then load
the scene file to restore entity positions, settings, and hierarchy.

### Saving

```python
def on_key_pressed(key_code):
    if key_code == ord('S'):
        if engine.scene.save_to_file("checkpoint.json"):
            print("Scene saved")
```

### Loading (Restore)

```python
def on_init():
    # Step 1: Load models to create GPU resources (meshes, textures)
    engine.load_gltf_scene("assets/sponza/Sponza.gltf")

    # Step 2: Load saved state to restore positions, lights, cameras
    if engine.scene.load_from_file("checkpoint.json"):
        print("Restored saved scene state")
```

The glTF loader creates entities with mesh data, materials, and textures. The
scene file load then overwrites the transform, light, camera, and hierarchy
data for the matching entity IDs, restoring them to their saved state.

---

## Use Cases

### Save Game State

Capture player progress, object positions, and scene configuration:

```python
def save_game():
    engine.scene.save_to_file(f"saves/slot_{slot}.json")

def load_game(slot):
    # Reload world geometry
    engine.load_gltf_scene("assets/world.gltf")
    # Restore saved state
    engine.scene.load_from_file(f"saves/slot_{slot}.json")
```

### Level Editors

Save hand-placed entity positions after arranging them in code:

```python
# Place entities programmatically
scene = engine.scene
for i in range(10):
    e = scene.create_entity(f"pillar_{i}")
    scene.set_position(e, (i * 3.0, 0.0, 0.0))

# Save the layout
scene.save_to_file("levels/hall.json")
```

### Debugging

Capture the exact scene state when a bug occurs, then reload it to reproduce:

```python
# When something looks wrong
engine.scene.save_to_file("debug_snapshot.json")

# Later, in a fresh run
engine.load_gltf_scene("assets/scene.gltf")
engine.scene.load_from_file("debug_snapshot.json")
# Now inspect the exact state that caused the issue
```

---

## Notes

- **`load_from_file` clears the scene first.** All existing entities are
  destroyed before the file contents are loaded. Make sure GPU resources
  (models) are loaded before or after accordingly.

- **Entity IDs may not match across runs.** The serialized IDs are the raw
  EnTT entity values from the session that saved the file. After loading, the
  entities exist with the same data but the underlying registry IDs depend on
  creation order.

- **File format is plain JSON.** You can edit scene files by hand or generate
  them with external tools. The structure is simple and stable.

---

## See Also

- [Scene API (Python)](../api/python/scene.md#serialization) -- full method
  reference for `save_to_file` and `load_from_file`
- [Scene API (C++)](../api/cpp/scene-api.md) -- C++ equivalents
- [Entities and Components](entities-and-components.md) -- creating and
  managing entities that get serialized
- [Loading Scenes](loading-scenes.md) -- loading glTF models to restore GPU
  data alongside scene files
