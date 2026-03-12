# Entities and Components

How Shoonyakasha's Entity Component System (ECS) works and how to create,
configure, and query game objects. Python examples are shown first, with C++
equivalents below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a running
> engine window.

---

## ECS Overview

Shoonyakasha uses an **Entity Component System** built on
[EnTT](https://github.com/skypjack/entt). The core idea has three parts:

| Concept | What it is | Example |
|---------|-----------|---------|
| **Entity** | A lightweight ID -- just an integer. It has no data or behavior on its own. | `42` (Python `int`), `entt::entity` (C++) |
| **Component** | A plain data struct attached to an entity. All state lives here. | `Transform`, `Camera`, `RigidBody` |
| **System** | Logic that runs each frame over entities that have specific components. | `CameraControllerSystem`, `PhysicsSystem` |

Entities are created through the Scene. Components are attached to entities by
name (Python) or by type (C++). Systems run automatically as part of the engine
update loop -- you do not need to call them yourself.

---

## Creating Entities

### Python

Use `scene.create_entity()` to create a new entity. The optional name argument
gives it a human-readable label you can search for later.

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_init():
    engine.create_camera((0, 5, 15))
    scene = engine.scene

    # Create a named entity
    entity = scene.create_entity("my_entity")

    # Create an unnamed entity
    unnamed = scene.create_entity()

engine.set_on_init(on_init)
engine.run()
```

Every new entity automatically receives a `Transform` and `Active` component
with default values (position at origin, scale 1, active true).

### C++

In C++ you have two options: the `EntityBuilder` fluent API (recommended) or
direct registry access.

```cpp
#include "ECS/Core.h"

// Option A: EntityBuilder (recommended)
auto entity = ECS::EntityBuilder(registry)
    .withName("my_entity")
    .build();

// Option B: Direct registry access
auto entity = registry.create();
registry.emplace<ECS::NameComponent>(entity, "my_entity");
registry.emplace<ECS::TransformComponent>(entity);
registry.emplace<ECS::ActiveComponent>(entity);
```

`EntityBuilder::build()` automatically adds `TransformComponent` and
`ActiveComponent` if you did not add them yourself, matching the Python
behavior.

---

## Adding Components

### Python -- String-Based API

In Python, components are added and removed by name using string identifiers:

```python
scene = engine.scene

entity = scene.create_entity("physics_box")
scene.add_component(entity, "RigidBody")
scene.add_component(entity, "Collider")
```

Check whether a component exists before reading its data:

```python
if scene.has_component(entity, "Camera"):
    fov = scene.get_camera_fov(entity)
```

Remove a component:

```python
scene.remove_component(entity, "Light")
```

### Registered Component Names

These component names are registered by default and can be used with
`add_component`, `has_component`, and `remove_component`:

| Name | Purpose |
|------|---------|
| `"Tag"` | Categorization string for group queries |
| `"Name"` | Human-readable name for identification |
| `"Hierarchy"` | Parent-child relationships |
| `"Transform"` | Position, rotation, and scale |
| `"Camera"` | Camera projection parameters |
| `"Light"` | Light source configuration |
| `"RigidBody"` | Physics simulation properties |
| `"Collider"` | Collision shape definition |
| `"Active"` | Enable/disable toggle |

List all available names at runtime:

```python
print(scene.get_component_names())
# ['Tag', 'Name', 'Hierarchy', 'Transform', 'Camera', 'Light', ...]
```

### C++ -- Type-Based API

In C++ you work directly with component types via the EnTT registry or through
EntityBuilder convenience methods:

```cpp
// EntityBuilder style
auto entity = ECS::EntityBuilder(registry)
    .withName("physics_box")
    .withTransform({0, 5, 0})
    .withRigidBody(ECS::RigidBodyComponent::Dynamic, 10.0f)
    .withCollider(ECS::ColliderComponent::Box, {1, 1, 1})
    .build();

// Direct registry style
registry.emplace<ECS::RigidBodyComponent>(entity);
registry.emplace<ECS::ColliderComponent>(entity);
```

---

## Transform

Every entity has a transform that defines its position, rotation, and scale in
3D space.

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| position | vec3 | `(0, 0, 0)` | Local position |
| rotation | vec3 | `(0, 0, 0)` | Euler angles in **radians** (pitch, yaw, roll) |
| scale | vec3 | `(1, 1, 1)` | Scale factor per axis |

**Rotation order:** Y (yaw), then X (pitch), then Z (roll). This is the
standard FPS camera convention that prevents the "tilted horizon" problem.

**Coordinate system:** -Z is forward, +X is right, +Y is up (Vulkan-style).

### Local vs. World Space

- `set_position` / `get_position` operate on the **local** transform relative
  to the entity's parent (or world origin if no parent).
- `get_world_position` returns the final **world-space** position after all
  parent transforms have been applied.

```python
scene = engine.scene

# Set local position
scene.set_position(entity, (1.0, 2.0, 3.0))

# Read it back
x, y, z = scene.get_position(entity)

# Get world position (accounts for parent hierarchy)
wx, wy, wz = scene.get_world_position(entity)
```

### Rotation

Rotations are Euler angles in radians. Use `math.radians()` to convert from
degrees:

```python
import math

# Rotate 90 degrees around the Y axis
scene.set_rotation(entity, (0.0, math.radians(90.0), 0.0))

# Read rotation
pitch, yaw, roll = scene.get_rotation(entity)
print(f"Yaw: {math.degrees(yaw):.1f} degrees")
```

### Scale

```python
scene.set_scale(entity, (2.0, 2.0, 2.0))  # double size
sx, sy, sz = scene.get_scale(entity)
```

### Direction Vectors

Get the entity's orientation-derived direction vectors:

```python
forward = scene.get_forward(entity)  # -Z direction
right   = scene.get_right(entity)    # +X direction
up      = scene.get_up(entity)       # +Y direction

# Move entity forward
pos = scene.get_position(entity)
speed = 5.0
new_pos = (pos[0] + forward[0] * speed * dt,
           pos[1] + forward[1] * speed * dt,
           pos[2] + forward[2] * speed * dt)
scene.set_position(entity, new_pos)
```

### C++ Transform

```cpp
auto& transform = registry.get<ECS::TransformComponent>(entity);

transform.position = glm::vec3(1.0f, 2.0f, 3.0f);
transform.rotation = glm::vec3(0.0f, glm::radians(90.0f), 0.0f);
transform.scale    = glm::vec3(2.0f);
transform.isDirty  = true;  // tell the transform system to recompute matrices

// Direction vectors
glm::vec3 fwd   = transform.getForward();
glm::vec3 right = transform.getRight();
glm::vec3 up    = transform.getUp();

// World position from the computed world matrix
glm::vec3 worldPos = glm::vec3(transform.worldMatrix[3]);
```

---

## Hierarchy

Entities can be organized into parent-child trees. When a parent moves, its
children move with it. Child transforms are expressed relative to their parent.

### Setting Up a Hierarchy

```python
scene = engine.scene

car   = scene.create_entity("car")
wheel = scene.create_entity("front_left_wheel")

# Make wheel a child of car
scene.set_parent(wheel, car)

# Now moving the car also moves the wheel
scene.set_position(car, (10.0, 0.0, 0.0))

# The wheel's local position is relative to the car
scene.set_position(wheel, (-1.0, 0.0, 1.5))
```

### Querying the Hierarchy

```python
# Get the parent of an entity
parent = scene.get_parent(wheel)
if parent != sk.NULL_ENTITY:
    print(f"Parent: {scene.get_name(parent)}")

# Get all direct children
children = scene.get_children(car)
for child in children:
    print(f"  Child: {scene.get_name(child)}")
```

### Unparenting

Pass `NULL_ENTITY` to detach an entity from its parent:

```python
scene.set_parent(wheel, sk.NULL_ENTITY)
```

### World Matrix Propagation

The transform system computes world matrices each frame by multiplying parent
and child local matrices:

```
child.worldMatrix = parent.worldMatrix * child.localMatrix
```

This means `get_world_position(child)` gives the true position in world space
even when the child has a local offset and its parent has been moved and
rotated.

### C++ Hierarchy

```cpp
auto& hierarchy = registry.get_or_emplace<ECS::HierarchyComponent>(wheel);
hierarchy.parent = car;

auto& parentHierarchy = registry.get_or_emplace<ECS::HierarchyComponent>(car);
parentHierarchy.addChild(wheel);
```

Or use `EntityBuilder`:

```cpp
auto wheel = ECS::EntityBuilder(registry)
    .withName("front_left_wheel")
    .withTransform({-1, 0, 1.5f})
    .withParent(car)
    .build();
```

---

## Name, Tag, and Active

Three simple components for identification and state management.

### Name

Every entity can have a name. Names are not enforced to be unique, but
`find_entity_by_name` returns the first match, so unique names are recommended.

```python
scene = engine.scene

entity = scene.create_entity("player_spawn")

# Read the name
name = scene.get_name(entity)

# Change it later
scene.set_name(entity, "player_spawn_v2")

# Find by name
found = scene.find_entity_by_name("player_spawn_v2")
if found != sk.NULL_ENTITY:
    print(f"Found entity at {scene.get_position(found)}")
```

### Tag

Tags are for grouping. Multiple entities can share the same tag, making it easy
to find all entities of a certain category.

```python
# Tag some entities
scene.set_tag(enemy1, "enemy")
scene.set_tag(enemy2, "enemy")
scene.set_tag(enemy3, "enemy")
scene.set_tag(player, "player")

# Find all enemies
enemies = scene.find_entities_with_tag("enemy")
print(f"Found {len(enemies)} enemies")

# Disable all enemies
for e in enemies:
    scene.set_active(e, False)
```

### Active

The `Active` component enables or disables an entity. Inactive entities are
skipped by all systems -- rendering, physics, and animation all ignore them.
This is a lightweight way to "turn off" an entity without destroying it.

```python
# Disable
scene.set_active(entity, False)

# Check
if scene.is_active(entity):
    print("Entity is active")

# Re-enable
scene.set_active(entity, True)
```

For rendering-only visibility control (entity still participates in physics and
other systems), use `set_visible` instead:

```python
scene.set_visible(entity, False)   # hidden from rendering only
scene.set_active(entity, False)    # skipped by ALL systems
```

---

## Querying Entities

The Scene API provides several ways to find entities at runtime.

### find_entity_by_name

Look up a single entity by its name string. Returns `NULL_ENTITY` if not found.

```python
camera = scene.find_entity_by_name("MainCamera")
player = scene.find_entity_by_name("player")
```

### find_entities_with_tag

Find all entities sharing a tag. Returns an empty list if none match.

```python
enemies = scene.find_entities_with_tag("enemy")
pickups = scene.find_entities_with_tag("pickup")
```

### get_all_entities

Iterate over every living entity in the scene.

```python
for entity in scene.get_all_entities():
    name = scene.get_name(entity)
    if name:
        print(f"  {entity}: {name}")
```

### get_main_camera

Get the entity handle of the current main camera (the one used for rendering).

```python
cam = scene.get_main_camera()
scene.set_position(cam, (0.0, 10.0, 20.0))
```

### entity_count

A read-only property for the total number of living entities.

```python
print(f"Scene has {scene.entity_count} entities")
```

---

## Destroying Entities

Remove an entity and all its components:

```python
scene.destroy_entity(entity)
```

In C++, `EntityHelper::destroyEntity` recursively destroys all children in the
hierarchy before destroying the entity itself, and removes it from its parent's
children list:

```cpp
ECS::EntityHelper::destroyEntity(registry, entity);
```

Check validity before operating on an entity handle that may have been
destroyed:

```python
if scene.is_valid(entity):
    scene.set_position(entity, (0, 0, 0))
```

---

## C++ EntityBuilder

The `EntityBuilder` provides a fluent API for assembling entities with multiple
components in a single expression. It is defined in `include/ECS/Core.h`.

```cpp
#include "ECS/Core.h"
#include "ECS/CameraController.h"

// Camera with controller
auto camera = ECS::EntityBuilder(registry)
    .withName("MainCamera")
    .withTag("Camera")
    .withTransform({0, 2, 10})
    .withCamera(true)
    .withFreeCameraController(8.0f)
    .build();

// Directional light
auto sun = ECS::EntityBuilder(registry)
    .withName("Sun")
    .withTransform({0, 50, 0}, {-0.8f, 0, 0})
    .withLight(ECS::LightComponent::Directional, {1, 0.95f, 0.9f}, 2.0f)
    .build();

// Physics object with lifetime
auto projectile = ECS::EntityBuilder(registry)
    .withName("Bullet")
    .withTransform({0, 1, 0})
    .withRigidBody(ECS::RigidBodyComponent::Dynamic, 0.1f)
    .withCollider(ECS::ColliderComponent::Sphere, {0.05f, 0, 0})
    .withLifetime(5.0f)
    .build();

// Child entity in a hierarchy
auto wheel = ECS::EntityBuilder(registry)
    .withName("FrontLeftWheel")
    .withTransform({-1, 0, 1.5f})
    .withParent(carEntity)
    .build();
```

### Available Builder Methods

| Method | Component Added |
|--------|----------------|
| `.withName(name)` | `NameComponent` |
| `.withTag(tag)` | `TagComponent` |
| `.withTransform(pos, rot, scale)` | `TransformComponent` |
| `.withParent(entity)` | `HierarchyComponent` (sets up parent-child link) |
| `.withCamera(isMain)` | `CameraComponent` |
| `.withLight(type, color, intensity)` | `LightComponent` |
| `.withRigidBody(type, mass)` | `RigidBodyComponent` |
| `.withCollider(shape, size)` | `ColliderComponent` |
| `.withLifetime(seconds)` | `LifetimeComponent` |
| `.withFreeCameraController(speed)` | `CameraControllerComponent` (Free mode) |
| `.withOrbitCameraController(target, dist, autoRotate)` | `CameraControllerComponent` (Orbit mode) |
| `.withFirstPersonController(eyeHeight)` | `CameraControllerComponent` (FirstPerson mode) |
| `.withThirdPersonController(target, offset)` | `CameraControllerComponent` (ThirdPerson mode) |
| `.with<T>(args...)` | Any custom component type |

### Attaching Custom Components

Use the generic `with<T>()` method to attach any component type, including
render components or your own structs:

```cpp
auto entity = ECS::EntityBuilder(registry)
    .withName("SkinnedCharacter")
    .with<MeshComponent>()
    .with<MaterialComponentV5>()
    .with<RenderableTagComponent>()
    .with<SkeletonComponent>()
    .build();
```

---

## C++ ComponentRegistry

The `ComponentRegistry` (defined in `include/ECS/Core.h`) provides runtime
component manipulation by string name. This is the mechanism the Python binding
layer uses internally when you call `scene.add_component(entity, "RigidBody")`.

```cpp
#include "ECS/Core.h"

ECS::ComponentRegistry compRegistry;
ECS::registerAllComponents(compRegistry);  // registers the core set

// Dynamic component creation
compRegistry.createComponent("Transform", registry, entity);
compRegistry.createComponent("Camera", registry, entity);

// Check existence
if (compRegistry.hasComponent("Camera", registry, entity)) {
    // entity has a CameraComponent
}

// Remove
compRegistry.removeComponent("Camera", registry, entity);

// List all registered names
for (const auto& name : compRegistry.getAllComponentNames()) {
    std::cout << name << "\n";
}
```

You can register your own component types:

```cpp
struct HealthComponent {
    float health = 100.0f;
};

compRegistry.registerComponent<HealthComponent>("Health");
compRegistry.createComponent("Health", registry, entity);
```

---

## Common Patterns

### Create an Entity and Position It

```python
def on_init():
    engine.create_camera((0, 5, 15))
    scene = engine.scene

    marker = scene.create_entity("spawn_point")
    scene.set_position(marker, (10.0, 0.0, -5.0))
    scene.set_tag(marker, "spawn")
```

### Build a Hierarchy of Entities

```python
def on_init():
    scene = engine.scene

    # Parent
    tank = scene.create_entity("tank")
    scene.set_position(tank, (0, 0, 0))

    # Children
    turret = scene.create_entity("turret")
    scene.set_parent(turret, tank)
    scene.set_position(turret, (0, 1.5, 0))  # local offset above tank body

    barrel = scene.create_entity("barrel")
    scene.set_parent(barrel, turret)
    scene.set_position(barrel, (0, 0, -2.0))  # local offset in front of turret

    # Moving the tank moves everything
    scene.set_position(tank, (10, 0, 0))

    # Rotating the turret rotates the barrel too
    import math
    scene.set_rotation(turret, (0, math.radians(45), 0))
```

### Find and Modify Entities After Loading

After loading a glTF scene, entities are live in the ECS and can be found by
name:

```python
def on_init():
    engine.create_camera((0, 5, 15))
    result = engine.load_gltf_scene("models/Scene.glb")

    scene = engine.scene

    # Find a specific mesh by name
    door = scene.find_entity_by_name("Door")
    if door != sk.NULL_ENTITY:
        scene.set_position(door, (0, 0, 5))
        scene.set_tag(door, "interactable")
```

### Disable and Re-enable Groups of Entities

```python
def toggle_enemies(scene, active):
    enemies = scene.find_entities_with_tag("enemy")
    for e in enemies:
        scene.set_active(e, active)

# In your update callback:
def on_update(dt):
    if engine.input.is_key_pressed(sk.KEY_P):
        toggle_enemies(engine.scene, False)  # pause all enemies
```

---

## See Also

- [ECS Components Reference](../api/cpp/ecs-components.md) -- Full struct
  definitions for all component types
- [Python Scene API](../api/python/scene.md) -- Complete Scene method reference
- [Cameras and Controllers](cameras-and-controllers.md) -- Camera setup and
  input control modes
- [Physics](physics.md) -- RigidBody and Collider usage
- [Loading Scenes](loading-scenes.md) -- Loading glTF models into the ECS
