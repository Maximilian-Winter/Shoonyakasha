# Scene serialization

Scene save/load is a **partial component snapshot** facility. It is not a complete renderable-scene or game-save format.

## API

```python
# During the running engine's lifetime:
saved = engine.scene.save_to_file("scene.json")
loaded = engine.scene.load_from_file("scene.json")
```

```cpp
// Include Facade/SceneAPI.h.
bool saved = engine.getScene().saveToFile("scene.json");
bool loaded = engine.getScene().loadFromFile("scene.json");
```

Loading clears existing entities, creates new ones, and remaps hierarchy IDs. It does **not** update matching existing glTF entities. Loading models first and then calling scene load destroys those model entities; it is not a restoration workflow.

## Current format and limitations

[Scene.h](../../include/ECS/Scene.h) implements serialization of Transform (position/Euler rotation/scale), Name, Tag, Camera, Light, and Hierarchy. It writes scene name and frame count; loading resets frame count rather than restoring it. Components absent from this explicit serializer are not saved, including native physics state, renderable mesh/material resources, animations, sprites/text, Active state, and script-defined payloads.

The save method returns true unless an exception is caught, but does not check stream open/write state. A true return is therefore not proof the file was written. Load returns false on parse/deserialization exceptions; a deserialization failure after clearing can leave a partially replaced scene. There is no transactional rollback.

## Application persistence

For a complete game save, define application-owned state: asset paths, stable object identifiers, transforms, and gameplay values. Recreate models through the loader and explicitly apply that state to the resulting entities. The engine does not currently supply this reconstruction/mapping layer.

Render-graph JSON export is separate from ECS serialization. Keep authored pipeline JSON as the source of truth; graph export is not a guaranteed lossless round trip of all declarative features. See the [pipeline reference](../reference/pipeline-json.md).

References: [Python Scene](../api/python/scene.md), [C++ SceneAPI](../api/cpp/scene-api.md).
