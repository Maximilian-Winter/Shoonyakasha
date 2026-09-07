# ECS design

A Scene owns an EnTT registry and SystemManager. Entities identify objects; native components hold typed state. Systems update views of matching components, ordered by integer priority (lower first).

TransformSystem maintains local/world matrices and hierarchy relationships. Camera/controller systems update views and input-driven movement. PhysicsSystem synchronizes kinematic/dynamic body state. Sprite/UI/text services create renderable quads and glyphs. The facade wires skeletal animation before rendering.

## Access surfaces

Native C++ can access registry components through ApplicationBase. ComponentRegistry supplies string-name creation/removal/presence checks for registered types; it does not provide general reflection over all component fields. SceneAPI presents concrete typed operations suitable for Python.

EcsAPI adds ScriptComponentBag (named shared opaque objects) and callback systems. Python object ownership is retained through a bridge; arbitrary payloads are not native component layouts, automatic shader sources, or serialized state.

## Lifetime and persistence

Entity handles are temporary registry identifiers. Hierarchy queries do not provide persistent IDs or automatic game-save reconstruction. Partial scene deserialization clears the registry and remaps IDs; it must not be used as an overlay onto loaded geometry.

GPU mesh references can outlive one entity while another shares them. Text glyph entities have a separate lifetime caveat: hiding a label is supported, but destroying only the label can leave glyphs visible.

References: [component map](../api/cpp/ecs-components.md), [script ECS](../guides/script-ecs.md), [serialization](../guides/scene-serialization.md), [entities](../guides/entities-and-components.md).
