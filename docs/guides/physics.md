# Physics

Shoonyakasha's `ECS::PhysicsSystem` integrates [Jolt Physics](https://github.com/jrouwe/JoltPhysics) with entity transforms. The facade registers it disabled by default. Native C++ applications can register it through their scene; see [PhysicsTest](../../examples/cpp/physics/physics_test).

## Enable and control simulation

Inside an init callback:

```python
engine.physics.enabled = True
engine.physics.gravity = (0.0, -9.81, 0.0)
engine.physics.fixed_time_step = 1.0 / 60.0
engine.physics.max_sub_steps = 10
```

```cpp
// Include Facade/PhysicsAPI.h.
auto& physics = engine.getPhysics();
physics.setEnabled(true);
physics.setGravity(glm::vec3(0.f, -9.81f, 0.f));
```

Configure after initialization; pre-run setters do not retain pending values. Each frame the simulation takes whole fixed steps of `fixed_time_step`, at most `max_sub_steps` of them; time beyond that is dropped and less than a step carries over to the next frame (with `max_sub_steps` 0, one step of the frame's time). Between steps, a dynamic body's transform is carried on by its velocities over the time carried over, so it moves every frame. This is not a guarantee of deterministic simulation across machines or arbitrary frame rates.

Once a body exists, apply forces/impulses and set or query velocity. A force and an instantaneous impulse have different semantics. Forces last for the frame they are given in (every step of it); impulses and velocities act at once. They apply to dynamic bodies; velocities can also be set on kinematic ones. Calls for missing bodies have no effect. A kinematic body moves to its transform over the frame's first step, carrying what rests on it; dynamic bodies feed their simulated transforms back to the ECS.

## Create and configure bodies

Native C++ has `RigidBodyComponent` fields for type, mass, drag, gravity, and rotation control, and `ColliderComponent` fields for shape, size, trigger state, friction, and restitution. `drag` and `angularDrag` are the body's damping; `useGravity` off leaves the body out of gravity; `freezeRotation` leaves it only its translations. A dynamic body of mass 0 or less, and any body with a plane collider, is static. A trigger is a sensor: bodies pass through it. Two bodies' friction and restitution are multiplied together. Body creation requires a RigidBody and Transform. A missing Collider uses a unit box with friction 0.5 and restitution 0. Add/configure an explicit collider before adding the rigid body so the body-construction signal sees its settings. Rebuild the body after changing those settings.

Python can add `Collider` and `RigidBody` by registered name, producing defaults, and then control the resulting body through `engine.physics`. It currently lacks typed setters for mass, shape, and dimensions. There is no documented glTF physics-metadata import.

| Shape | Current implementation |
|---|---|
| Box | `size` is full extents |
| Sphere | `size.x` is radius |
| Capsule | Along Y: `size.x` is radius; `size.y` is the length of its cylinder (the whole capsule is `size.y + 2 * size.x` tall) |
| Plane | Infinite up-facing plane through the origin, static |
| Mesh | Not implemented; emits a warning and uses a unit box |

The current public physics interfaces do not expose raycasts, constraints, or collision callbacks. Do not infer support from Jolt's broader feature set or enum names.

References: [Python Physics](../api/python/physics.md), [C++ PhysicsAPI](../api/cpp/physics-api.md), [native component definitions](../../include/ECS/Core.h), [native system](../../include/ECS/PhysicsSystem.h).
