# Entities and components

Create scene content in the engine's init callback. `EngineAPI::getScene()` and Python `engine.scene` expose entity lifecycle and typed native component access; [script ECS](script-ecs.md) handles custom Python objects.

```python
# Inside on_init, with engine already constructed:
scene = engine.scene
parent = scene.create_entity("parent")
child = scene.create_entity("child")
scene.set_position(parent, (2.0, 0.0, 0.0))
scene.set_parent(child, parent)
scene.set_position(child, (0.0, 1.0, 0.0))
```

```cpp
// Include Facade/SceneAPI.h; inside the init callback:
auto& scene = engine.getScene();
auto parent = scene.createEntity("parent");
auto child = scene.createEntity("child");
scene.setParent(child, parent);
scene.setPosition(child, glm::vec3(0.f, 1.f, 0.f));
```

Creation adds Transform and Active. Positions/scales are local to the parent, rotations use Euler radians, and world transforms are refreshed by the ECS transform system. Parent changes do not promise preservation of the child's previous world transform.

## Built-in versus script components

Call `scene.get_component_names()` / `getComponentNames()` to discover names available for native add/remove/has operations. Core registrations include Tag, Name, Hierarchy, Transform, Camera, Light, RigidBody, Collider, and Active; camera-controller registrations add InputState and CameraController. A native component existing in C++ does not mean it is registered for string access.

Python typed setters cover transforms, camera/light values, material parameters, sprites/text, visibility, hierarchy, and animation. Adding a component by name constructs defaults; it does not expose every C++ field. In particular, body mass and collider shape/dimensions require native C++ access.

Native C++ applications using `ApplicationBase` can access `getRegistry()` and EnTT components directly. See the [component map](../api/cpp/ecs-components.md). Use `EntityBuilder` when assembling native components fluently.

## Lifetime and queries

Use validity checks before operating on retained handles. Name lookup can return `NULL_ENTITY` / `NullEntity`; names are not guaranteed unique. Destroying an entity invalidates its handle, and scene loading replaces entities. Parent and children are explicit relationships, not persistent IDs.

Set render visibility to hide geometry. Set text visibility with `set_text_visible` / `setTextVisible`: text is represented by generated glyph entities, and destroying only the label can leave glyphs on screen.

References: [Python Scene](../api/python/scene.md), [C++ SceneAPI](../api/cpp/scene-api.md), [serialization](scene-serialization.md).
