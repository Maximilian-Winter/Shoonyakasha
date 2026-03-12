# PhysicsAPI

`Shoonyakasha::Facade::PhysicsAPI` -- physics world control, forces, velocity, and body management.

> **Facade layer** -- this is the same API that Python wraps via Cython. All Bullet3 internals are hidden behind PIMPL. For the inheritance-based alternative, see [ApplicationBase](application-base.md).

**Header:** `#include "Facade/PhysicsAPI.h"`
**Namespace:** `Shoonyakasha::Facade`

**See also:** [Python Physics API](../python/physics.md)

---

## Obtaining a PhysicsAPI

You do not construct `PhysicsAPI` directly. Access it through `EngineAPI::getPhysics()`:

```cpp
auto& physics = engine.getPhysics();
physics.setGravity(glm::vec3(0.f, -9.81f, 0.f));
physics.setEnabled(true);
```

The `PhysicsAPI` is valid for the lifetime of the `EngineAPI`.

---

## Enable / Disable

### isEnabled

```cpp
bool isEnabled() const;
```

Check if the physics simulation is currently enabled.

**Returns:** `bool` -- `true` if physics is running.

### setEnabled

```cpp
void setEnabled(bool enabled);
```

Enable or disable the physics simulation. When disabled, no physics stepping occurs and forces/velocities are not applied.

| Parameter | Type | Description |
|-----------|------|-------------|
| `enabled` | `bool` | `true` to enable, `false` to disable. |

---

## World Configuration

### setGravity

```cpp
void setGravity(const glm::vec3& gravity);
```

Set the gravity vector for the physics world.

| Parameter | Type | Description |
|-----------|------|-------------|
| `gravity` | `const glm::vec3&` | Gravity vector (e.g. `glm::vec3(0, -9.81, 0)` for Earth gravity). |

### getGravity

```cpp
glm::vec3 getGravity() const;
```

Get the current gravity vector.

**Returns:** `glm::vec3` -- the gravity vector.

### setFixedTimeStep

```cpp
void setFixedTimeStep(float timeStep);
```

Set the fixed time step for physics simulation. Bullet3 uses fixed-step integration for deterministic behavior.

| Parameter | Type | Description |
|-----------|------|-------------|
| `timeStep` | `float` | Fixed step duration in seconds (default is typically `1/60`). |

### getFixedTimeStep

```cpp
float getFixedTimeStep() const;
```

Get the current fixed time step.

**Returns:** `float` -- time step in seconds.

### setMaxSubSteps

```cpp
void setMaxSubSteps(int maxSubSteps);
```

Set the maximum number of sub-steps per simulation tick. Higher values prevent "tunneling" at low frame rates but cost more CPU time.

| Parameter | Type | Description |
|-----------|------|-------------|
| `maxSubSteps` | `int` | Maximum sub-steps per tick. |

### getMaxSubSteps

```cpp
int getMaxSubSteps() const;
```

Get the current maximum sub-step count.

**Returns:** `int` -- max sub-steps.

---

## Forces / Impulses

All force and impulse methods require the entity to have a `RigidBody` component with type `Dynamic`.

### addForce

```cpp
void addForce(EntityHandle entity, const glm::vec3& force);
```

Apply a continuous force to an entity. The force is accumulated over physics sub-steps and cleared after each tick. Suitable for sustained effects like thrust or wind.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a dynamic rigid body. |
| `force` | `const glm::vec3&` | Force vector in world space (Newtons). |

### addImpulse

```cpp
void addImpulse(EntityHandle entity, const glm::vec3& impulse);
```

Apply an instantaneous impulse (immediate velocity change). Suitable for one-time effects like jumps or explosions.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a dynamic rigid body. |
| `impulse` | `const glm::vec3&` | Impulse vector in world space (Newton-seconds). |

### addTorqueImpulse

```cpp
void addTorqueImpulse(EntityHandle entity, const glm::vec3& torque);
```

Apply an instantaneous torque impulse (immediate angular velocity change).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a dynamic rigid body. |
| `torque` | `const glm::vec3&` | Torque impulse vector in world space. |

---

## Velocity

### setLinearVelocity

```cpp
void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity);
```

Set the linear velocity of a physics body directly.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a dynamic rigid body. |
| `velocity` | `const glm::vec3&` | Velocity vector in world space (units/second). |

### getLinearVelocity

```cpp
glm::vec3 getLinearVelocity(EntityHandle entity) const;
```

Get the current linear velocity of a physics body.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a rigid body. |

**Returns:** `glm::vec3` -- velocity in units/second.

### setAngularVelocity

```cpp
void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity);
```

Set the angular velocity of a physics body directly.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a dynamic rigid body. |
| `velocity` | `const glm::vec3&` | Angular velocity vector (radians/second). |

### getAngularVelocity

```cpp
glm::vec3 getAngularVelocity(EntityHandle entity) const;
```

Get the current angular velocity of a physics body.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a rigid body. |

**Returns:** `glm::vec3` -- angular velocity in radians/second.

---

## Body Management

### rebuildBody

```cpp
void rebuildBody(EntityHandle entity);
```

Rebuild the physics body for an entity. Call this after changing the collider shape at runtime to apply the new shape to the Bullet3 simulation.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Entity with a rigid body component. |

### getBodyCount

```cpp
uint32_t getBodyCount() const;
```

Get the total number of tracked physics bodies in the simulation.

**Returns:** `uint32_t` -- body count.

---

## Related Types

### RigidBodyType

```cpp
enum class RigidBodyType : uint8_t {
    Static    = 0,
    Kinematic = 1,
    Dynamic   = 2
};
```

| Value | Description |
|-------|-------------|
| `Static` | Immovable body (walls, floors). Zero mass. |
| `Kinematic` | Controlled by code, not physics. Affects dynamic bodies but is not affected by forces. |
| `Dynamic` | Fully simulated. Affected by gravity, forces, and collisions. |

### ColliderShape

```cpp
enum class ColliderShape : uint8_t {
    Box     = 0,
    Sphere  = 1,
    Capsule = 2,
    Mesh    = 3,
    Plane   = 4
};
```

| Value | Description |
|-------|-------------|
| `Box` | Axis-aligned box collider. |
| `Sphere` | Sphere collider. |
| `Capsule` | Capsule collider (cylinder with hemispherical caps). |
| `Mesh` | Triangle mesh collider (static bodies only). |
| `Plane` | Infinite plane collider. |

These enums are defined in `Facade/FacadeTypes.h` and are used when adding physics components via [SceneAPI](scene-api.md).
