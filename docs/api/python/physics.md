# Physics

Access: `engine.physics`. [Other language reference](../cpp/physics-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Controls the Bullet-backed ECS physics system. Obtain it through the engine and configure it in init or later. The facade exists before initialization, but setters do not buffer pending configuration.

## World and body contract

Defaults are disabled, gravity `(0, -9.81, 0)`, fixed step `1/60` seconds, and maximum 10 substeps. The `enabled` switch controls stepping; do not assume it prevents every body mutation API from being called.

Forces, impulses, and velocity operations require an entity with a live physics body. Missing bodies produce no effect or zero-valued velocity results. Rebuild after changing native collider/body settings; body count is the number tracked by the native system.

C++ native ECS access is needed to configure body mass/type and collider dimensions. Python name-based component addition creates defaults but does not offer those typed fields. There is no facade raycast, constraint, or collision-event API. Native Mesh colliders currently fall back to a box.

See the [physics guide](../../guides/physics.md) for setup, supported shapes, and the native C++ example.

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `enabled (read/write property)` | bool | Calls native `isEnabled`; see the class contract above. |
| `gravity (read/write property)` | 3-tuple | Get gravity as (x, y, z) tuple. |
| `fixed_time_step (read/write property)` | float | Calls native `getFixedTimeStep`; see the class contract above. |
| `max_sub_steps (read/write property)` | int | Calls native `getMaxSubSteps`; see the class contract above. |
| `add_force(entity, force)` | None | Apply continuous force from (x, y, z) tuple. |
| `add_impulse(entity, impulse)` | None | Apply instantaneous impulse from (x, y, z) tuple. |
| `add_torque_impulse(entity, torque)` | None | Apply torque impulse from (x, y, z) tuple. |
| `get_linear_velocity(entity)` | 3-tuple | Get linear velocity as (x, y, z) tuple. |
| `set_linear_velocity(entity, velocity)` | None | Set linear velocity from (x, y, z) tuple. |
| `get_angular_velocity(entity)` | 3-tuple | Get angular velocity as (x, y, z) tuple. |
| `set_angular_velocity(entity, velocity)` | None | Set angular velocity from (x, y, z) tuple. |
| `rebuild_body(entity)` | None | Rebuild physics body after changing collider shape. |
| `body_count (read-only property)` | int | Total tracked physics body count. |

<!-- END SOURCE API -->
