# Physics

Shoonyakasha's `ECS::PhysicsSystem` integrates Bullet with entity transforms. The facade registers it disabled by default. Native C++ applications can register it through their scene; see [PhysicsTest](../../examples/cpp/physics/physics_test).

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

Configure after initialization; pre-run setters do not retain pending values. Fixed substeps separate the simulation step size from rendering, subject to the maximum substep count. This is not a guarantee of deterministic simulation across machines or arbitrary frame rates.

Once a body exists, apply forces/impulses and set or query velocity. A force and an instantaneous impulse have different semantics. Calls for missing bodies have no effect. Kinematic transforms feed Bullet; dynamic bodies feed their simulated transforms back to the ECS.

## Create and configure bodies

Native C++ has `RigidBodyComponent` fields for type, mass, drag, gravity, and rotation control, and `ColliderComponent` fields for shape, size, trigger state, friction, and restitution. Body creation requires a RigidBody and Transform. A missing Collider uses a unit box. Add/configure an explicit collider before adding the rigid body so the body-construction signal sees its settings. Rebuild the body after changing those settings.

Python can add `Collider` and `RigidBody` by registered name, producing defaults, and then control the resulting body through `engine.physics`. It currently lacks typed setters for mass, shape, and dimensions. There is no documented glTF physics-metadata import.

| Shape | Current implementation |
|---|---|
| Box | `size` is full extents; converted to Bullet half-extents |
| Sphere | `size.x` is radius |
| Capsule | `size.x` is radius; `size.y` is height |
| Plane | Infinite up-facing plane |
| Mesh | Not implemented; emits a warning and uses a unit box |

The current public physics interfaces do not expose raycasts, constraints, or collision callbacks. Do not infer support from Bullet's broader feature set or enum names.

References: [Python Physics](../api/python/physics.md), [C++ PhysicsAPI](../api/cpp/physics-api.md), [native component definitions](../../include/ECS/Core.h), [native system](../../include/ECS/PhysicsSystem.h).
