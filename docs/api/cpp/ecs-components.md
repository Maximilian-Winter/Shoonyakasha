# Native ECS component map

Most native components live in [Core.h](../../../include/ECS/Core.h), [RenderComponents.h](../../../include/ECS/RenderComponents.h), [Sprite2DComponents.h](../../../include/ECS/Sprite2DComponents.h), and [SkeletonComponents.h](../../../include/ECS/SkeletonComponents.h). Use these declarations for field types and defaults.

| Group | Components and role |
|---|---|
| Identity/lifetime | Name, Tag, Active, Lifetime |
| Spatial | Transform (local/cached world), Hierarchy (parent/children) |
| View/lighting | Camera, Light, controller/input state |
| Physics | RigidBody and Collider, connected to Bullet by PhysicsSystem |
| Rendering | MeshComponent, MaterialComponentV5, RenderableTagComponent |
| 2D | Sprite/UI/text data, consumed by layout and glyph systems |
| Animation | Skeleton and playback data, including GPU bone buffers |
| Scripting | ScriptComponentBag containing named opaque payloads |

Native types are not all registered for string-based creation. Ask `SceneAPI::getComponentNames()` for that surface; [SceneAPI](scene-api.md) exposes typed operations for a subset of fields. Python script payloads use [EcsAPI](ecs-api.md), not the native component registry.

Transform rotation fields are Euler radians. Collider box sizes are full extents; sphere/capsule use radius in x. Mesh colliders fall back to boxes. GPU mesh buffers are reference-counted; do not manually destroy borrowed Vulkan handles. See [physics](../../guides/physics.md), [GPU types](gpu-types.md), and [serialization limits](../../guides/scene-serialization.md).
