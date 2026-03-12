# Physics

How to use Bullet3 rigid body physics in the Shoonyakasha engine. Python
examples are shown first, with C++ equivalents below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and have a working
> engine setup with a pipeline JSON and camera.

---

## Bullet3 Overview

Shoonyakasha integrates [Bullet3](https://github.com/bulletphysics/bullet3) for
rigid body physics, collision detection, and dynamics simulation. The engine
wraps Bullet3 behind a PIMPL layer so you never need to include Bullet headers
or manage the physics world directly. The integration provides:

- **Rigid body dynamics** -- objects respond to gravity, forces, and collisions
- **Collision detection** -- broadphase and narrowphase with several shape types
- **Fixed time-step simulation** -- deterministic physics independent of frame
  rate

All physics state lives in the ECS. An entity participates in the simulation
when it has both a `RigidBody` and a `Collider` component.

---

## Enabling Physics

Physics is disabled by default. Enable it in your `on_init` callback.

### Python

```python
engine.physics.enabled = True
```

### C++

```cpp
auto& physics = engine.getPhysics();
physics.setEnabled(true);
```

When disabled, the physics world is not stepped and no forces or velocities are
applied. You can toggle physics on and off at any time (e.g. to freeze the
simulation during a pause menu).

---

## World Settings

Configure the physics world after enabling it.

### Gravity

The gravity vector determines the direction and strength of gravitational pull
on dynamic bodies.

**Default:** `(0, -9.81, 0)` (Earth gravity, Y-up)

```python
# Standard Earth gravity
engine.physics.gravity = (0.0, -9.81, 0.0)

# Low gravity (Moon-like)
engine.physics.gravity = (0.0, -1.62, 0.0)

# Zero gravity (space)
engine.physics.gravity = (0.0, 0.0, 0.0)
```

### Fixed Time Step

Bullet3 uses fixed-step integration for deterministic behavior. The physics
world advances in fixed increments of this duration, regardless of the rendering
frame rate.

**Default:** `1/60` (0.01667 seconds)

```python
# Higher precision physics (120 Hz)
engine.physics.fixed_time_step = 1.0 / 120.0

# Default (60 Hz)
engine.physics.fixed_time_step = 1.0 / 60.0
```

### Max Sub-Steps

When a frame takes longer than `fixed_time_step`, the physics engine catches up
by running multiple sub-steps, up to this limit. Higher values prevent objects
from tunneling through walls during frame drops but cost more CPU time.

```python
engine.physics.max_sub_steps = 10
```

### C++ World Settings

```cpp
auto& physics = engine.getPhysics();
physics.setGravity(glm::vec3(0.f, -9.81f, 0.f));
physics.setFixedTimeStep(1.f / 60.f);
physics.setMaxSubSteps(10);
```

---

## Rigid Body Types

Every entity with a `RigidBody` component has a type that determines how the
physics simulation treats it.

| Type | Mass | Behavior | Use case |
|------|------|----------|----------|
| **Static** | 0 (immovable) | Does not move. Other objects collide with it but it never responds to forces. | Floors, walls, terrain |
| **Kinematic** | N/A | Moved programmatically (by setting position or velocity). Pushes dynamic objects but is not affected by forces or collisions. | Moving platforms, doors, animated obstacles |
| **Dynamic** | > 0 | Fully simulated. Responds to gravity, forces, impulses, and collisions. | Balls, crates, projectiles, ragdolls |

### Python Constants

```python
import shoonyakasha as sk

sk.RIGIDBODY_STATIC     # 0
sk.RIGIDBODY_KINEMATIC   # 1
sk.RIGIDBODY_DYNAMIC     # 2
```

### C++ Enum

```cpp
enum class RigidBodyType : uint8_t {
    Static    = 0,
    Kinematic = 1,
    Dynamic   = 2
};
```

---

## Collider Shapes

Every entity with a `Collider` component has a shape that defines its collision
volume. The shape's `size` field is interpreted differently depending on the
shape type.

| Shape | size interpretation | Description |
|-------|--------------------|-------------|
| **Box** | `(half_x, half_y, half_z)` -- half-extents per axis | Axis-aligned box. A size of `(1, 1, 1)` creates a 2x2x2 box. |
| **Sphere** | `(radius, -, -)` -- only `size.x` is used | Sphere centered at the entity's position. |
| **Capsule** | `(radius, height, -)` -- `size.x` = radius, `size.y` = total height | Cylinder with hemispherical caps. Good for characters. |
| **Mesh** | N/A (derived from geometry) | Triangle mesh collider. Accurate but expensive. Best for static bodies only. |
| **Plane** | N/A | Infinite plane. Useful for ground planes and world boundaries. |

### Python Constants

```python
import shoonyakasha as sk

sk.COLLIDER_BOX       # 0
sk.COLLIDER_SPHERE    # 1
sk.COLLIDER_CAPSULE   # 2
sk.COLLIDER_MESH      # 3
sk.COLLIDER_PLANE     # 4
```

### C++ Enum

```cpp
enum class ColliderShape : uint8_t {
    Box     = 0,
    Sphere  = 1,
    Capsule = 2,
    Mesh    = 3,
    Plane   = 4
};
```

---

## Adding Physics to Entities

An entity needs both a `RigidBody` and a `Collider` component to participate in
the physics simulation. Without both, the entity will not be tracked by the
physics world.

### Python

```python
scene = engine.scene

# Create an entity
entity = scene.create_entity("ball")

# Add physics components
scene.add_component(entity, "RigidBody")
scene.add_component(entity, "Collider")

# Position it above the ground
scene.set_position(entity, (0, 5, 0))

# Rebuild the physics body so Bullet3 picks it up
engine.physics.rebuild_body(entity)
```

### C++

The `EntityBuilder` provides a fluent API for assembling physics entities:

```cpp
auto ball = ECS::EntityBuilder(registry)
    .withName("ball")
    .withTransform({0, 5, 0}, {0, 0, 0}, {1, 1, 1})
    .withRigidBody(ECS::RigidBodyComponent::Dynamic, 1.0f)
    .withCollider(ECS::ColliderComponent::Sphere, {0.5f, 0, 0})
    .build();
```

The builder's `withRigidBody` takes a type and a mass, and `withCollider` takes
a shape and a size vector:

```cpp
// Static floor with a box collider (10x0.1x10 half-extents)
auto floor = ECS::EntityBuilder(registry)
    .withName("floor")
    .withTransform({0, 0, 0})
    .withRigidBody(ECS::RigidBodyComponent::Static, 0.0f)
    .withCollider(ECS::ColliderComponent::Box, {10, 0.1f, 10})
    .build();

// Dynamic capsule character (radius=0.4, height=1.8)
auto character = ECS::EntityBuilder(registry)
    .withName("character")
    .withTransform({0, 3, 0})
    .withRigidBody(ECS::RigidBodyComponent::Dynamic, 70.0f)
    .withCollider(ECS::ColliderComponent::Capsule, {0.4f, 1.8f, 0})
    .build();
```

---

## Forces and Impulses

Forces and impulses are applied through the Physics API and only affect dynamic
bodies. Static and kinematic bodies ignore them.

### add_force -- Continuous Force

Apply a force every frame in `on_update` for sustained effects like thrust,
wind, or spring forces. Forces accumulate over the frame and are cleared after
each physics step.

```python
def on_update(dt):
    # Constant upward thrust while SPACE is held
    if engine.input.is_key_down(32):  # SPACE
        engine.physics.add_force(rocket, (0.0, 50.0, 0.0))
```

### add_impulse -- Instant Impulse

Apply a one-shot velocity change for events like jumps, explosions, or hits.
Call once, not every frame.

```python
# Launch a ball upward
engine.physics.add_impulse(ball, (0.0, 10.0, 0.0))
```

### add_torque_impulse -- Rotational Impulse

Apply an instant angular velocity change to spin an object.

```python
# Spin the entity around the Y axis
engine.physics.add_torque_impulse(entity, (0.0, 5.0, 0.0))
```

### C++ Equivalents

```cpp
auto& physics = engine.getPhysics();

// Continuous force (call every frame)
physics.addForce(rocket, glm::vec3(0.f, 50.f, 0.f));

// One-shot impulse
physics.addImpulse(ball, glm::vec3(0.f, 10.f, 0.f));

// Torque impulse
physics.addTorqueImpulse(entity, glm::vec3(0.f, 5.f, 0.f));
```

---

## Velocity Control

You can read and write linear and angular velocity directly. This is useful for
character controllers, speed limits, or teleportation.

### Linear Velocity

```python
# Get current velocity
vx, vy, vz = engine.physics.get_linear_velocity(entity)
speed = (vx**2 + vy**2 + vz**2) ** 0.5
print(f"Speed: {speed:.2f} units/s")

# Set velocity directly (e.g. stop all movement)
engine.physics.set_linear_velocity(entity, (0.0, 0.0, 0.0))
```

### Angular Velocity

```python
# Get angular velocity (radians/second per axis)
wx, wy, wz = engine.physics.get_angular_velocity(entity)

# Set angular velocity (spin at 2 rad/s around Y)
engine.physics.set_angular_velocity(entity, (0.0, 2.0, 0.0))
```

### C++

```cpp
auto& physics = engine.getPhysics();

glm::vec3 vel = physics.getLinearVelocity(entity);
physics.setLinearVelocity(entity, glm::vec3(0.f));

glm::vec3 angVel = physics.getAngularVelocity(entity);
physics.setAngularVelocity(entity, glm::vec3(0.f, 2.f, 0.f));
```

---

## Rebuilding Bodies

The engine does not automatically detect when you change a collider's shape or
size at runtime. After modifying physics components, call `rebuild_body` to
update the Bullet3 body so the new settings take effect.

```python
# Change collider shape at runtime
scene.add_component(entity, "Collider")   # re-add with new params
scene.add_component(entity, "RigidBody")  # re-add with new params
engine.physics.rebuild_body(entity)
```

In C++:

```cpp
physics.rebuildBody(entity);
```

You only need to rebuild when the shape type or size changes. Normal simulation
(forces, collisions, position updates) does not require rebuilding.

---

## Example: Spawning Physics Objects

A common pattern is to spawn physics objects at runtime -- for example, shooting
projectiles on key press. Create the entity, attach mesh + physics components,
and apply an initial velocity.

```python
import shoonyakasha as sk

engine = sk.Engine(
    title="Physics Spawner",
    pipeline_json_path="pbr_ibl_pipeline_v3.json",
    hdr_environment_path="environment.hdr",
)

# Preloaded projectile mesh (loaded once in on_init)
projectile_mesh = None

def on_init():
    global projectile_mesh
    engine.create_camera(pos=(0, 5, 15))
    engine.create_directional_light((-0.5, -1.0, -0.3), intensity=3.0)

    # Enable physics
    engine.physics.enabled = True
    engine.physics.gravity = (0.0, -9.81, 0.0)

    # Load a sphere mesh to use as projectiles
    projectile_mesh = engine.load_gltf_scene("models/sphere.glb")

    # Create a static floor
    scene = engine.scene
    floor = scene.create_entity("floor")
    scene.add_component(floor, "RigidBody")
    scene.add_component(floor, "Collider")
    engine.physics.rebuild_body(floor)

spawn_count = 0

def on_key_pressed(key):
    global spawn_count
    scene = engine.scene

    if key == 32:  # SPACE -- spawn a projectile
        spawn_count += 1

        # Load a new instance of the sphere
        result = engine.load_gltf_scene(
            "models/sphere.glb",
            name_prefix=f"proj_{spawn_count}_",
        )

        if result.success and result.entities:
            entity = result.entities[0]

            # Get camera forward direction for launch
            cam = engine.camera_entity
            fwd = scene.get_forward(cam)
            cam_pos = scene.get_position(cam)

            # Position at camera
            scene.set_position(entity, cam_pos)

            # Add physics
            scene.add_component(entity, "RigidBody")
            scene.add_component(entity, "Collider")
            engine.physics.rebuild_body(entity)

            # Launch forward
            launch_speed = 20.0
            engine.physics.add_impulse(entity, (
                fwd[0] * launch_speed,
                fwd[1] * launch_speed,
                fwd[2] * launch_speed,
            ))

            print(f"Spawned projectile #{spawn_count}")

engine.set_on_init(on_init)
engine.set_on_key_pressed(on_key_pressed)
engine.run()
```

---

## Complete Setup Checklist

Here is the minimum setup to get physics working:

1. **Enable the simulation:** `engine.physics.enabled = True`
2. **Set gravity:** `engine.physics.gravity = (0, -9.81, 0)`
3. **Add both components** to each physics entity: `"RigidBody"` and
   `"Collider"`
4. **Rebuild the body:** `engine.physics.rebuild_body(entity)`
5. **Apply forces/impulses** in callbacks, not every frame (unless using
   `add_force` for continuous effects)

---

## See Also

- [Python Physics API](../api/python/physics.md) -- complete `engine.physics`
  method reference
- [C++ PhysicsAPI](../api/cpp/physics-api.md) -- C++ `PhysicsAPI` class
  reference
- [Python Scene API](../api/python/scene.md) -- entity and component management
  (`add_component`, `set_position`)
- [ECS Components](../api/cpp/ecs-components.md) -- `RigidBodyComponent` and
  `ColliderComponent` struct definitions
- [Constants](../api/python/constants.md) -- `RIGIDBODY_STATIC`,
  `RIGIDBODY_DYNAMIC`, `COLLIDER_BOX`, `COLLIDER_SPHERE`, etc.
- [Entities and Components](entities-and-components.md) -- how to create
  entities and attach components
