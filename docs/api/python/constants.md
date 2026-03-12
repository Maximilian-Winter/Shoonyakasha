# Constants

> Module-level constants exported by `shoonyakasha`.

All constants are plain Python `int` values. Import them from the top-level
package:

```python
import shoonyakasha as sk

print(sk.NULL_ENTITY)         # 4294967295
print(sk.CAMERA_PERSPECTIVE)  # 0
print(sk.LIGHT_DIRECTIONAL)   # 0
```

---

## Entity Handle Sentinel

### NULL_ENTITY

| Name | Value | Description |
|------|-------|-------------|
| `NULL_ENTITY` | `4294967295` (`UINT32_MAX`) | Sentinel value representing an invalid or nonexistent entity handle. Returned by lookup methods when no entity matches. |

**Example:**

```python
import shoonyakasha as sk

entity = engine.scene.find_entity_by_name("player")
if entity == sk.NULL_ENTITY:
    print("Entity not found")
```

---

## Camera Types

Used with `Scene.get_camera_type()` and `Scene.set_camera_type()`.

| Name | Value | Description |
|------|-------|-------------|
| `CAMERA_PERSPECTIVE` | `0` | Standard perspective projection with field of view. |
| `CAMERA_ORTHOGRAPHIC` | `1` | Orthographic projection with fixed size (no perspective foreshortening). |

**Example:**

```python
import shoonyakasha as sk

scene = engine.scene
cam = engine.camera_entity

# Switch to orthographic
scene.set_camera_type(cam, sk.CAMERA_ORTHOGRAPHIC)
scene.set_camera_ortho_size(cam, 20.0)

# Switch back to perspective
scene.set_camera_type(cam, sk.CAMERA_PERSPECTIVE)
scene.set_camera_fov(cam, 60.0)
```

---

## Light Types

Used with `Scene.get_light_type()` and `Scene.set_light_type()`.

| Name | Value | Description |
|------|-------|-------------|
| `LIGHT_DIRECTIONAL` | `0` | Parallel rays from an infinitely distant source (e.g., sunlight). Has direction but no position. |
| `LIGHT_POINT` | `1` | Omnidirectional light emanating from a single point. Has position and range. |
| `LIGHT_SPOT` | `2` | Cone-shaped light from a single point. Has position, direction, and cone angle. |

**Example:**

```python
import shoonyakasha as sk

scene = engine.scene
light = scene.create_entity("my_light")
scene.add_component(light, "Light")
scene.set_light_type(light, sk.LIGHT_POINT)
scene.set_light_color(light, (1.0, 0.9, 0.7))
scene.set_light_intensity(light, 5.0)
scene.set_light_range(light, 20.0)
```

---

## Rigid Body Types

Used when configuring physics bodies on entities.

| Name | Value | Description |
|------|-------|-------------|
| `RIGIDBODY_STATIC` | `0` | Immovable body. Does not respond to forces. Used for floors, walls, and terrain. |
| `RIGIDBODY_KINEMATIC` | `1` | Moved programmatically (via position/velocity), not by forces. Affects dynamic bodies but is not affected by them. |
| `RIGIDBODY_DYNAMIC` | `2` | Fully simulated body affected by gravity, forces, and collisions. |

**Example:**

```python
import shoonyakasha as sk

# These constants are typically used when setting up physics components
# on entities loaded from glTF or created manually.
print(f"Static = {sk.RIGIDBODY_STATIC}")      # 0
print(f"Kinematic = {sk.RIGIDBODY_KINEMATIC}") # 1
print(f"Dynamic = {sk.RIGIDBODY_DYNAMIC}")     # 2
```

---

## Collider Shapes

Used when configuring collision shapes for physics bodies.

| Name | Value | Description |
|------|-------|-------------|
| `COLLIDER_BOX` | `0` | Axis-aligned box collider defined by half-extents. |
| `COLLIDER_SPHERE` | `1` | Sphere collider defined by radius. |
| `COLLIDER_CAPSULE` | `2` | Capsule (cylinder with hemispherical caps) defined by radius and height. |
| `COLLIDER_MESH` | `3` | Triangle mesh collider. Accurate but expensive; best for static geometry. |
| `COLLIDER_PLANE` | `4` | Infinite plane collider. Useful for ground planes and boundaries. |

**Example:**

```python
import shoonyakasha as sk

# Collider shapes are used when setting up physics on entities.
print(f"Box = {sk.COLLIDER_BOX}")         # 0
print(f"Sphere = {sk.COLLIDER_SPHERE}")   # 1
print(f"Capsule = {sk.COLLIDER_CAPSULE}") # 2
print(f"Mesh = {sk.COLLIDER_MESH}")       # 3
print(f"Plane = {sk.COLLIDER_PLANE}")     # 4
```

---

## Complete Reference Table

| Constant | Value | Category |
|----------|-------|----------|
| `NULL_ENTITY` | `4294967295` | Entity handle |
| `CAMERA_PERSPECTIVE` | `0` | Camera type |
| `CAMERA_ORTHOGRAPHIC` | `1` | Camera type |
| `LIGHT_DIRECTIONAL` | `0` | Light type |
| `LIGHT_POINT` | `1` | Light type |
| `LIGHT_SPOT` | `2` | Light type |
| `RIGIDBODY_STATIC` | `0` | Rigid body type |
| `RIGIDBODY_KINEMATIC` | `1` | Rigid body type |
| `RIGIDBODY_DYNAMIC` | `2` | Rigid body type |
| `COLLIDER_BOX` | `0` | Collider shape |
| `COLLIDER_SPHERE` | `1` | Collider shape |
| `COLLIDER_CAPSULE` | `2` | Collider shape |
| `COLLIDER_MESH` | `3` | Collider shape |
| `COLLIDER_PLANE` | `4` | Collider shape |

---

## See Also

- [Engine](engine.md) -- Main engine class
- [Scene](scene.md) -- Entity and component management where these constants are used
- [Physics](physics.md) -- Physics API using rigid body and collider constants
