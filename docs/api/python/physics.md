# Physics

> `shoonyakasha.Physics` -- Rigid body physics simulation.

The `Physics` class controls the Bullet3 physics simulation. Accessed via
`engine.physics` -- do not construct directly.

For physics to affect an entity, the entity must have both a **RigidBody** and
a **Collider** component. Use `scene.add_component(entity, "RigidBody")` and
`scene.add_component(entity, "Collider")` to attach them, or load entities from
a glTF file that already includes physics metadata.

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_init():
    engine.create_camera((0, 5, 15))
    engine.physics.enabled = True
    engine.physics.gravity = (0.0, -9.81, 0.0)

engine.set_on_init(on_init)
engine.run()
```

> **Prerequisite:** Entities need both `RigidBody` and `Collider` components to
> participate in the physics simulation. Without both, the entity will not be
> tracked by the physics world.

---

## Properties

### enabled

Enable or disable the physics simulation. When disabled, the physics world
is not stepped and no forces are applied.

**Type:** `bool` (read-write property)

**Example:**

```python
engine.physics.enabled = True   # start simulating
engine.physics.enabled = False  # freeze physics
```

---

### gravity

Get or set the world gravity vector.

**Type:** `tuple[float, float, float]` (read-write property)

**Default:** `(0.0, -9.81, 0.0)`

**Example:**

```python
# Standard Earth gravity
engine.physics.gravity = (0.0, -9.81, 0.0)

# Low gravity (Moon-like)
engine.physics.gravity = (0.0, -1.62, 0.0)

# Zero gravity
engine.physics.gravity = (0.0, 0.0, 0.0)

# Read current gravity
gx, gy, gz = engine.physics.gravity
```

---

### fixed_time_step

Get or set the fixed physics time step. The physics engine advances in
fixed increments of this size, independent of the rendering frame rate.

**Type:** `float` (read-write property)

**Default:** `1/60` (0.01667 seconds)

**Example:**

```python
# Higher precision physics (120 Hz)
engine.physics.fixed_time_step = 1.0 / 120.0

# Default (60 Hz)
engine.physics.fixed_time_step = 1.0 / 60.0
```

---

### max_sub_steps

Get or set the maximum number of simulation sub-steps per frame. If a
frame takes longer than `fixed_time_step`, the physics engine catches up by
running multiple sub-steps, up to this limit.

**Type:** `int` (read-write property)

**Example:**

```python
# Allow more sub-steps for accuracy during frame drops
engine.physics.max_sub_steps = 10
```

---

### body_count

Read-only property returning the total number of physics bodies currently
tracked by the physics world.

**Type:** `int` (read-only property)

**Example:**

```python
print(f"Physics bodies: {engine.physics.body_count}")
```

---

## Force Methods

Forces and impulses are applied to entities that have `RigidBody` (dynamic
type) and `Collider` components. Static and kinematic bodies ignore applied
forces.

### add_force(entity, force)

Apply a continuous force to an entity. Forces accumulate over the frame and
are cleared after each physics step. For persistent forces, call this every
frame in `on_update`.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle (must have dynamic RigidBody). |
| `force` | `tuple[float, float, float]` | -- | Force vector as `(x, y, z)` in Newtons. |

**Returns:** `None`

**Example:**

```python
def on_update(dt):
    # Apply a constant upward thrust while SPACE is held
    if engine.input.is_key_down(32):
        engine.physics.add_force(rocket, (0.0, 50.0, 0.0))
```

---

### add_impulse(entity, impulse)

Apply an instantaneous impulse to an entity. Unlike forces, impulses
immediately change velocity and are not accumulated. Use for one-shot
events like jumps, explosions, or hits.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle (must have dynamic RigidBody). |
| `impulse` | `tuple[float, float, float]` | -- | Impulse vector as `(x, y, z)` in Newton-seconds. |

**Returns:** `None`

**Example:**

```python
# Launch a ball upward
engine.physics.add_impulse(ball, (0.0, 10.0, 0.0))
```

---

### add_torque_impulse(entity, torque)

Apply an instantaneous rotational impulse to an entity. This changes
angular velocity immediately.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle (must have dynamic RigidBody). |
| `torque` | `tuple[float, float, float]` | -- | Torque impulse as `(x, y, z)`. |

**Returns:** `None`

**Example:**

```python
# Spin the entity around the Y axis
engine.physics.add_torque_impulse(entity, (0.0, 5.0, 0.0))
```

---

## Velocity

### get_linear_velocity(entity)

Get the linear velocity of a physics body.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Velocity as `(vx, vy, vz)` in units per second.

**Example:**

```python
vx, vy, vz = engine.physics.get_linear_velocity(entity)
speed = (vx**2 + vy**2 + vz**2) ** 0.5
print(f"Speed: {speed:.2f}")
```

---

### set_linear_velocity(entity, velocity)

Set the linear velocity of a physics body directly. This overrides any
existing velocity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `velocity` | `tuple[float, float, float]` | -- | New velocity as `(vx, vy, vz)`. |

**Returns:** `None`

**Example:**

```python
# Stop all movement
engine.physics.set_linear_velocity(entity, (0.0, 0.0, 0.0))
```

---

### get_angular_velocity(entity)

Get the angular velocity of a physics body.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Angular velocity as `(wx, wy, wz)` in radians per second.

**Example:**

```python
wx, wy, wz = engine.physics.get_angular_velocity(entity)
```

---

### set_angular_velocity(entity, velocity)

Set the angular velocity of a physics body directly.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `velocity` | `tuple[float, float, float]` | -- | New angular velocity as `(wx, wy, wz)` in radians per second. |

**Returns:** `None`

**Example:**

```python
# Spin at 2 radians/second around Y axis
engine.physics.set_angular_velocity(entity, (0.0, 2.0, 0.0))
```

---

## Body Management

### rebuild_body(entity)

Rebuild the physics body for an entity after changing its collider shape or
rigid body type at runtime. The engine does not automatically detect
component changes, so this must be called manually after modifying
`Collider` or `RigidBody` parameters.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `None`

**Example:**

```python
# Change collider type and rebuild
scene.add_component(entity, "Collider")
scene.add_component(entity, "RigidBody")
engine.physics.rebuild_body(entity)
```

---

## Patterns

### Applying Impulse on Key Press

Use an event callback so the impulse fires once per key press, not every
frame:

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")
ball = None

def on_init():
    global ball
    engine.create_camera((0, 5, 15))
    engine.physics.enabled = True
    engine.physics.gravity = (0.0, -9.81, 0.0)

    # Assume ball entity is loaded from a glTF scene with RigidBody + Collider
    result = engine.load_gltf_scene("ball.glb")
    if result.success and result.entities:
        ball = result.entities[0]

def on_key_event(key_code, pressed):
    if ball is not None and pressed:
        if key_code == 32:  # SPACE -- launch upward
            engine.physics.add_impulse(ball, (0.0, 10.0, 0.0))
        if key_code == 87:  # W -- push forward
            fwd = engine.scene.get_forward(engine.camera_entity)
            engine.physics.add_impulse(ball, (fwd[0] * 5, fwd[1] * 5, fwd[2] * 5))

engine.set_on_init(on_init)
engine.input.set_on_key_event(on_key_event)
engine.run()
```

### Configuring Gravity

```python
# Standard Earth gravity
engine.physics.gravity = (0.0, -9.81, 0.0)

# Side-scrolling platformer (2D feel, stronger gravity)
engine.physics.gravity = (0.0, -20.0, 0.0)

# Space game (no gravity)
engine.physics.gravity = (0.0, 0.0, 0.0)

# Underwater (weak downward, slow)
engine.physics.gravity = (0.0, -2.0, 0.0)
```

### Physics Setup Checklist

Entities require specific components to participate in the physics world:

```python
scene = engine.scene

# 1. Create or obtain an entity (e.g., from glTF load)
entity = scene.create_entity("physics_box")

# 2. Add required components
scene.add_component(entity, "RigidBody")
scene.add_component(entity, "Collider")

# 3. Rebuild the physics body so Bullet3 picks it up
engine.physics.rebuild_body(entity)

# 4. Enable the simulation
engine.physics.enabled = True
```

---

## See Also

- [Engine](engine.md) -- Main engine entry point (parent of `physics`)
- [Scene](scene.md) -- Entity and component management
- [Input](input.md) -- Keyboard and mouse input
- [Constants](constants.md) -- Module-level constants (`RIGIDBODY_STATIC`, `RIGIDBODY_DYNAMIC`, `COLLIDER_BOX`, etc.)
