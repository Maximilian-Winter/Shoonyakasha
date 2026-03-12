# Scene

> `shoonyakasha.Scene` -- Entity and component management.

The `Scene` class is the primary interface for creating, querying, and
manipulating entities and their components. Accessed via `engine.scene` -- do
not construct directly.

Entity handles are plain `int` values (uint32). Vectors are tuples of floats,
and matrices are tuple-of-tuples (4x4, column-major).

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_init():
    cam = engine.create_camera((0, 5, 15))
    scene = engine.scene

    # Create an entity and set its transform
    entity = scene.create_entity("my_cube")
    scene.set_position(entity, (1.0, 2.0, 3.0))
    scene.set_scale(entity, (0.5, 0.5, 0.5))

engine.set_on_init(on_init)
engine.run()
```

---

## Entity Lifecycle

### create_entity(name)

Create a new entity with an optional name.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `str` | `""` | Optional human-readable name for the entity. |

**Returns:** `int` -- Entity handle.

**Example:**

```python
scene = engine.scene
entity = scene.create_entity("player")
unnamed = scene.create_entity()
```

---

### destroy_entity(entity)

Destroy an entity and remove all its components.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle to destroy. |

**Returns:** `None`

**Example:**

```python
scene.destroy_entity(entity)
```

---

### is_valid(entity)

Check whether an entity handle refers to a living entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle to check. |

**Returns:** `bool` -- `True` if the entity exists and has not been destroyed.

**Example:**

```python
if scene.is_valid(entity):
    scene.set_position(entity, (0.0, 0.0, 0.0))
```

---

### entity_count

Read-only property returning the total number of living entities.

**Type:** `int` (read-only property)

**Example:**

```python
print(f"Active entities: {scene.entity_count}")
```

---

## Entity Queries

### find_entity_by_name(name)

Find an entity by its name string.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `str` | -- | Name to search for. |

**Returns:** `int` -- Entity handle, or `NULL_ENTITY` if not found.

**Example:**

```python
player = scene.find_entity_by_name("player")
if player != sk.NULL_ENTITY:
    pos = scene.get_position(player)
```

---

### find_entities_with_tag(tag)

Find all entities that have a given tag.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `tag` | `str` | -- | Tag string to match. |

**Returns:** `list[int]` -- List of entity handles (empty if none found).

**Example:**

```python
enemies = scene.find_entities_with_tag("enemy")
for e in enemies:
    scene.set_active(e, False)
```

---

### get_main_camera()

Get the entity handle of the current main camera.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `int` -- Main camera entity handle.

**Example:**

```python
cam = scene.get_main_camera()
scene.set_position(cam, (0.0, 10.0, 20.0))
```

---

### get_all_entities()

Get a list of all living entity handles.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `list[int]` -- All entity handles.

**Example:**

```python
for entity in scene.get_all_entities():
    name = scene.get_name(entity)
    if name:
        print(f"  {entity}: {name}")
```

---

## Component Management

Components are managed by string name. The following component types are
registered by default:

`Tag`, `Name`, `Hierarchy`, `Transform`, `Camera`, `Light`, `RigidBody`,
`Collider`, `Active`

### add_component(entity, component_name)

Add a component to an entity by type name.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `component_name` | `str` | -- | Component type name (e.g., `"Transform"`, `"Light"`). |

**Returns:** `bool` -- `True` if the component was added successfully.

**Example:**

```python
scene.add_component(entity, "Camera")
scene.add_component(entity, "Light")
```

---

### remove_component(entity, component_name)

Remove a component from an entity by type name.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `component_name` | `str` | -- | Component type name to remove. |

**Returns:** `bool` -- `True` if the component was removed successfully.

**Example:**

```python
scene.remove_component(entity, "Light")
```

---

### has_component(entity, component_name)

Check whether an entity has a specific component type.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `component_name` | `str` | -- | Component type name to check for. |

**Returns:** `bool` -- `True` if the entity has the component.

**Example:**

```python
if scene.has_component(entity, "Transform"):
    pos = scene.get_position(entity)
```

---

### get_component_names()

List all registered component type names.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| *(none)* | | | |

**Returns:** `list[str]` -- Names of all registered component types.

**Example:**

```python
print("Registered components:", scene.get_component_names())
# e.g., ['Tag', 'Name', 'Hierarchy', 'Transform', 'Camera', 'Light', ...]
```

---

## Name / Tag / Active

### get_name(entity)

Get the name of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `str` -- The entity's name (empty string if no name set).

**Example:**

```python
name = scene.get_name(entity)
```

---

### set_name(entity, name)

Set the name of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `name` | `str` | -- | New name for the entity. |

**Returns:** `None`

**Example:**

```python
scene.set_name(entity, "player_spawn")
```

---

### get_tag(entity)

Get the tag of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `str` -- The entity's tag (empty string if no tag set).

**Example:**

```python
tag = scene.get_tag(entity)
```

---

### set_tag(entity, tag)

Set the tag of an entity. Tags are used for group queries with
`find_entities_with_tag()`.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `tag` | `str` | -- | Tag string. |

**Returns:** `None`

**Example:**

```python
scene.set_tag(entity, "enemy")
```

---

### is_active(entity)

Check whether an entity is active.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `bool` -- `True` if the entity is active.

**Example:**

```python
if scene.is_active(entity):
    print("Entity is active")
```

---

### set_active(entity, active)

Enable or disable an entity. Inactive entities are skipped by rendering and
other systems.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `active` | `bool` | -- | `True` to enable, `False` to disable. |

**Returns:** `None`

**Example:**

```python
scene.set_active(entity, False)  # hide entity
```

---

## Transform

Transform methods operate on the entity's `Transform` component. Positions and
scales are `(x, y, z)` tuples. **Rotations are Euler angles in radians**,
applied in Y, X, Z order.

### get_position(entity)

Get the local position of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Position as `(x, y, z)`.

**Example:**

```python
x, y, z = scene.get_position(entity)
```

---

### set_position(entity, pos)

Set the local position of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `pos` | `tuple[float, float, float]` | -- | New position as `(x, y, z)`. |

**Returns:** `None`

**Example:**

```python
scene.set_position(entity, (1.0, 2.0, 3.0))
```

---

### get_rotation(entity)

Get the local rotation of an entity as Euler angles in radians.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Euler rotation as `(pitch, yaw, roll)` in radians, applied in Y, X, Z order.

**Example:**

```python
import math

rx, ry, rz = scene.get_rotation(entity)
print(f"Yaw: {math.degrees(ry):.1f} degrees")
```

---

### set_rotation(entity, rot)

Set the local rotation of an entity from Euler angles in radians.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `rot` | `tuple[float, float, float]` | -- | Euler rotation as `(pitch, yaw, roll)` in radians. Applied in Y, X, Z order. |

**Returns:** `None`

**Example:**

```python
import math

# Rotate 90 degrees around Y axis
scene.set_rotation(entity, (0.0, math.radians(90.0), 0.0))
```

---

### get_scale(entity)

Get the local scale of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Scale as `(sx, sy, sz)`.

**Example:**

```python
sx, sy, sz = scene.get_scale(entity)
```

---

### set_scale(entity, scale)

Set the local scale of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `scale` | `tuple[float, float, float]` | -- | New scale as `(sx, sy, sz)`. |

**Returns:** `None`

**Example:**

```python
scene.set_scale(entity, (2.0, 2.0, 2.0))  # double size
```

---

### get_world_position(entity)

Get the world-space position of an entity. For entities in a hierarchy, this
accounts for all parent transforms.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- World position as `(x, y, z)`.

**Example:**

```python
world_pos = scene.get_world_position(child_entity)
```

---

### get_world_matrix(entity)

Get the full 4x4 world transform matrix for an entity. Returned in
column-major order as a tuple of four row-tuples.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[tuple[float, ...], ...]` -- 4x4 matrix as a tuple of four 4-element tuples (column-major).

**Example:**

```python
matrix = scene.get_world_matrix(entity)
# matrix[col][row] — column-major layout
print(f"Translation: ({matrix[3][0]}, {matrix[3][1]}, {matrix[3][2]})")
```

---

### get_forward(entity)

Get the forward direction vector of an entity (derived from its rotation).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Unit forward direction as `(x, y, z)`.

**Example:**

```python
fwd = scene.get_forward(entity)
pos = scene.get_position(entity)
speed = 5.0
new_pos = (pos[0] + fwd[0] * speed * dt,
           pos[1] + fwd[1] * speed * dt,
           pos[2] + fwd[2] * speed * dt)
scene.set_position(entity, new_pos)
```

---

### get_right(entity)

Get the right direction vector of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Unit right direction as `(x, y, z)`.

**Example:**

```python
right = scene.get_right(entity)
```

---

### get_up(entity)

Get the up direction vector of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `tuple[float, float, float]` -- Unit up direction as `(x, y, z)`.

**Example:**

```python
up = scene.get_up(entity)
```

---

## Camera

Camera methods operate on the entity's `Camera` component. Use the module-level
constants `CAMERA_PERSPECTIVE` and `CAMERA_ORTHOGRAPHIC` for camera type values.

### get_camera_type(entity)

Get the camera projection type.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `int` -- `sk.CAMERA_PERSPECTIVE` or `sk.CAMERA_ORTHOGRAPHIC`.

**Example:**

```python
if scene.get_camera_type(cam) == sk.CAMERA_PERSPECTIVE:
    print("Perspective camera")
```

---

### set_camera_type(entity, camera_type)

Set the camera projection type.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `camera_type` | `int` | -- | `sk.CAMERA_PERSPECTIVE` or `sk.CAMERA_ORTHOGRAPHIC`. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_type(cam, sk.CAMERA_ORTHOGRAPHIC)
```

---

### get_camera_fov(entity)

Get the vertical field of view in degrees (perspective cameras only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `float` -- Field of view in degrees.

**Example:**

```python
fov = scene.get_camera_fov(cam)
```

---

### set_camera_fov(entity, fov)

Set the vertical field of view in degrees (perspective cameras only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `fov` | `float` | -- | Field of view in degrees. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_fov(cam, 90.0)  # wide FOV
```

---

### get_camera_near(entity)

Get the near clipping plane distance.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `float` -- Near plane distance.

**Example:**

```python
near = scene.get_camera_near(cam)
```

---

### set_camera_near(entity, near_plane)

Set the near clipping plane distance.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `near_plane` | `float` | -- | Near plane distance. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_near(cam, 0.01)
```

---

### get_camera_far(entity)

Get the far clipping plane distance.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `float` -- Far plane distance.

**Example:**

```python
far = scene.get_camera_far(cam)
```

---

### set_camera_far(entity, far_plane)

Set the far clipping plane distance.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `far_plane` | `float` | -- | Far plane distance. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_far(cam, 5000.0)
```

---

### get_camera_ortho_size(entity)

Get the orthographic camera half-size (orthographic cameras only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `float` -- Orthographic half-size in world units.

**Example:**

```python
size = scene.get_camera_ortho_size(cam)
```

---

### set_camera_ortho_size(entity, size)

Set the orthographic camera half-size (orthographic cameras only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `size` | `float` | -- | Orthographic half-size in world units. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_type(cam, sk.CAMERA_ORTHOGRAPHIC)
scene.set_camera_ortho_size(cam, 10.0)
```

---

### is_camera_main(entity)

Check whether this camera is the active main camera.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |

**Returns:** `bool` -- `True` if this is the main camera.

**Example:**

```python
if scene.is_camera_main(cam):
    print("This is the active camera")
```

---

### set_camera_main(entity, is_main)

Set whether this camera is the active main camera.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Camera entity handle. |
| `is_main` | `bool` | -- | `True` to make this the main camera. |

**Returns:** `None`

**Example:**

```python
scene.set_camera_main(cam, True)
```

---

## Light

Light methods operate on the entity's `Light` component. Use the module-level
constants `LIGHT_DIRECTIONAL`, `LIGHT_POINT`, and `LIGHT_SPOT` for light type
values.

### get_light_type(entity)

Get the light type.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |

**Returns:** `int` -- `sk.LIGHT_DIRECTIONAL`, `sk.LIGHT_POINT`, or `sk.LIGHT_SPOT`.

**Example:**

```python
if scene.get_light_type(light) == sk.LIGHT_POINT:
    print("Point light")
```

---

### set_light_type(entity, light_type)

Set the light type.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |
| `light_type` | `int` | -- | `sk.LIGHT_DIRECTIONAL`, `sk.LIGHT_POINT`, or `sk.LIGHT_SPOT`. |

**Returns:** `None`

**Example:**

```python
scene.set_light_type(light, sk.LIGHT_SPOT)
```

---

### get_light_color(entity)

Get the light color.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |

**Returns:** `tuple[float, float, float]` -- Color as `(r, g, b)`.

**Example:**

```python
r, g, b = scene.get_light_color(light)
```

---

### set_light_color(entity, color)

Set the light color.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |
| `color` | `tuple[float, float, float]` | -- | Color as `(r, g, b)`, each in `[0, 1]`. |

**Returns:** `None`

**Example:**

```python
scene.set_light_color(light, (1.0, 0.9, 0.8))  # warm white
```

---

### get_light_intensity(entity)

Get the light intensity multiplier.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |

**Returns:** `float` -- Intensity value.

**Example:**

```python
intensity = scene.get_light_intensity(light)
```

---

### set_light_intensity(entity, intensity)

Set the light intensity multiplier.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |
| `intensity` | `float` | -- | Intensity value. |

**Returns:** `None`

**Example:**

```python
scene.set_light_intensity(light, 5.0)
```

---

### get_light_range(entity)

Get the light range (point and spot lights only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |

**Returns:** `float` -- Range in world units.

**Example:**

```python
range_val = scene.get_light_range(light)
```

---

### set_light_range(entity, range)

Set the light range (point and spot lights only).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |
| `range` | `float` | -- | Range in world units. |

**Returns:** `None`

**Example:**

```python
scene.set_light_range(light, 25.0)
```

---

### get_light_cast_shadows(entity)

Check whether the light casts shadows.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |

**Returns:** `bool` -- `True` if shadow casting is enabled.

**Example:**

```python
if scene.get_light_cast_shadows(light):
    print("Shadow-casting light")
```

---

### set_light_cast_shadows(entity, cast_shadows)

Enable or disable shadow casting for a light.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Light entity handle. |
| `cast_shadows` | `bool` | -- | `True` to enable shadows. |

**Returns:** `None`

**Example:**

```python
scene.set_light_cast_shadows(light, True)
```

---

## Material

Material methods read and write named parameters on the entity's material.
Parameter names are shader-defined strings (e.g., `"roughness"`, `"baseColor"`).

### set_material_float(entity, param, value)

Set a float material parameter.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `value` | `float` | -- | Float value. |

**Returns:** `None`

**Example:**

```python
scene.set_material_float(entity, "roughness", 0.3)
scene.set_material_float(entity, "metallic", 1.0)
```

---

### get_material_float(entity, param, default_val)

Get a float material parameter with an optional default.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `default_val` | `float` | `0.0` | Value returned if the parameter does not exist. |

**Returns:** `float` -- The parameter value, or `default_val` if not set.

**Example:**

```python
roughness = scene.get_material_float(entity, "roughness", 0.5)
```

---

### set_material_vec3(entity, param, value)

Set a vec3 material parameter (e.g., color, emission).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `value` | `tuple[float, float, float]` | -- | Three-component vector as `(x, y, z)`. |

**Returns:** `None`

**Example:**

```python
scene.set_material_vec3(entity, "emissiveColor", (1.0, 0.2, 0.0))
```

---

### get_material_vec3(entity, param, default_val)

Get a vec3 material parameter with an optional default.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `default_val` | `tuple[float, float, float]` | `(0.0, 0.0, 0.0)` | Value returned if the parameter does not exist. |

**Returns:** `tuple[float, float, float]` -- The parameter value.

**Example:**

```python
color = scene.get_material_vec3(entity, "emissiveColor")
```

---

### set_material_vec4(entity, param, value)

Set a vec4 material parameter.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `value` | `tuple[float, float, float, float]` | -- | Four-component vector as `(x, y, z, w)`. |

**Returns:** `None`

**Example:**

```python
scene.set_material_vec4(entity, "baseColorFactor", (0.8, 0.2, 0.1, 1.0))
```

---

### get_material_vec4(entity, param, default_val)

Get a vec4 material parameter with an optional default.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |
| `default_val` | `tuple[float, float, float, float]` | `(0.0, 0.0, 0.0, 0.0)` | Value returned if the parameter does not exist. |

**Returns:** `tuple[float, float, float, float]` -- The parameter value.

**Example:**

```python
base = scene.get_material_vec4(entity, "baseColorFactor", (1.0, 1.0, 1.0, 1.0))
```

---

### has_material_param(entity, param)

Check whether a named material parameter exists on an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `param` | `str` | -- | Material parameter name. |

**Returns:** `bool` -- `True` if the parameter exists.

**Example:**

```python
if scene.has_material_param(entity, "roughness"):
    r = scene.get_material_float(entity, "roughness")
```

---

## Renderable

### is_visible(entity)

Check whether an entity is visible for rendering.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `bool` -- `True` if the entity is visible.

**Example:**

```python
if scene.is_visible(entity):
    print("Entity will be rendered")
```

---

### set_visible(entity, visible)

Show or hide an entity for rendering.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `visible` | `bool` | -- | `True` to show, `False` to hide. |

**Returns:** `None`

**Example:**

```python
scene.set_visible(entity, False)  # hide from rendering
```

---

### get_cast_shadows(entity)

Check whether an entity casts shadows.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `bool` -- `True` if shadow casting is enabled.

**Example:**

```python
casts = scene.get_cast_shadows(entity)
```

---

### set_cast_shadows(entity, cast_shadows)

Enable or disable shadow casting for a renderable entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `cast_shadows` | `bool` | -- | `True` to enable shadow casting. |

**Returns:** `None`

**Example:**

```python
scene.set_cast_shadows(entity, True)
```

---

## Hierarchy

Parent-child relationships between entities. Setting a parent causes the child's
transform to be relative to the parent.

### get_parent(entity)

Get the parent of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `int` -- Parent entity handle, or `NULL_ENTITY` if no parent.

**Example:**

```python
parent = scene.get_parent(entity)
if parent != sk.NULL_ENTITY:
    print(f"Parent: {scene.get_name(parent)}")
```

---

### set_parent(child, parent)

Set the parent of an entity. Pass `NULL_ENTITY` to unparent.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `child` | `int` | -- | Child entity handle. |
| `parent` | `int` | -- | Parent entity handle, or `sk.NULL_ENTITY` to unparent. |

**Returns:** `None`

**Example:**

```python
scene.set_parent(wheel, car)  # wheel follows car
```

---

### get_children(entity)

Get all direct children of an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Parent entity handle. |

**Returns:** `list[int]` -- List of child entity handles (empty if no children).

**Example:**

```python
children = scene.get_children(car)
for child in children:
    print(f"  Child: {scene.get_name(child)}")
```

---

## Animation

Animation methods control skeletal and clip-based animations on entities.
Animation clips are loaded from glTF files via `engine.load_gltf_scene()` with
`load_animations=True` (the default).

### get_animation_clip_count(entity)

Get the number of animation clips available on an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `int` -- Number of clips. `0` if the entity has no animations.

**Example:**

```python
count = scene.get_animation_clip_count(entity)
print(f"Entity has {count} animation clips")
```

---

### get_animation_clip_name(entity, clip_index)

Get the name of an animation clip by index.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `clip_index` | `int` | -- | Zero-based clip index. |

**Returns:** `str` -- Clip name.

**Example:**

```python
for i in range(scene.get_animation_clip_count(entity)):
    name = scene.get_animation_clip_name(entity, i)
    print(f"  Clip {i}: {name}")
```

---

### get_animation_clip_duration(entity, clip_index)

Get the duration of an animation clip in seconds.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `clip_index` | `int` | -- | Zero-based clip index. |

**Returns:** `float` -- Duration in seconds.

**Example:**

```python
duration = scene.get_animation_clip_duration(entity, 0)
print(f"Clip duration: {duration:.2f}s")
```

---

### play_animation(entity, clip_index)

Start playing an animation clip by index. Resets the playback time to zero and
sets the animation to playing.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `clip_index` | `int` | -- | Zero-based clip index. |

**Returns:** `None`

**Example:**

```python
# Play the first animation clip
scene.play_animation(entity, 0)
scene.set_animation_looping(entity, True)
```

---

### stop_animation(entity)

Stop animation playback. Pauses playback and resets the time to zero.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `None`

**Example:**

```python
scene.stop_animation(entity)
```

---

### is_animation_playing(entity)

Check whether animation is currently playing on an entity.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `bool` -- `True` if animation is playing.

**Example:**

```python
if scene.is_animation_playing(entity):
    print("Animating...")
```

---

### get_animation_speed(entity)

Get the animation playback speed multiplier.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `float` -- Speed multiplier (default `1.0`).

**Example:**

```python
speed = scene.get_animation_speed(entity)
```

---

### set_animation_speed(entity, speed)

Set the animation playback speed multiplier. Use values greater than `1.0` for
faster playback, less than `1.0` for slower, and negative values for reverse.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `speed` | `float` | -- | Speed multiplier. |

**Returns:** `None`

**Example:**

```python
scene.set_animation_speed(entity, 2.0)   # double speed
scene.set_animation_speed(entity, 0.5)   # half speed
```

---

### get_animation_time(entity)

Get the current animation playback time in seconds.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `float` -- Current time in seconds.

**Example:**

```python
t = scene.get_animation_time(entity)
print(f"Animation at {t:.2f}s")
```

---

### set_animation_time(entity, time)

Set the current animation playback time. Use this to scrub through an animation
manually.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `time` | `float` | -- | Time in seconds. |

**Returns:** `None`

**Example:**

```python
# Jump to the 2-second mark
scene.set_animation_time(entity, 2.0)
```

---

### is_animation_looping(entity)

Check whether animation is set to loop.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `bool` -- `True` if looping is enabled.

**Example:**

```python
if scene.is_animation_looping(entity):
    print("Animation loops")
```

---

### set_animation_looping(entity, loop)

Enable or disable animation looping.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |
| `loop` | `bool` | -- | `True` to loop, `False` to play once. |

**Returns:** `None`

**Example:**

```python
scene.set_animation_looping(entity, True)
scene.play_animation(entity, 0)
```

---

### get_current_animation_clip(entity)

Get the index of the currently active animation clip.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `int` | -- | Entity handle. |

**Returns:** `int` -- Zero-based clip index, or `-1` if no clip is active.

**Example:**

```python
clip = scene.get_current_animation_clip(entity)
if clip >= 0:
    name = scene.get_animation_clip_name(entity, clip)
    print(f"Playing: {name}")
```

---

## Serialization

### save_to_file(path)

Save the entire scene to a file.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `str` | -- | Output file path. |

**Returns:** `bool` -- `True` if the file was saved successfully.

**Example:**

```python
scene.save_to_file("my_scene.json")
```

---

### load_from_file(path)

Load a scene from a file, replacing all current entities.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `str` | -- | Input file path. |

**Returns:** `bool` -- `True` if the file was loaded successfully.

**Example:**

```python
if scene.load_from_file("my_scene.json"):
    print(f"Loaded {scene.entity_count} entities")
```

---

## See Also

- [Constants](constants.md) -- Module-level constants (`NULL_ENTITY`, camera types, light types, etc.)
- [Engine](engine.md) -- Main engine entry point (parent of `scene`)
- [GltfResult](gltf-result.md) -- Result type for `engine.load_gltf_scene()`
- [Input](input.md) -- Keyboard and mouse input
- [Physics](physics.md) -- Rigid body physics
