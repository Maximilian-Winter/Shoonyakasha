# Constants

Import native constants from `shoonyakasha` after installing the extension. Pure-Python key constants live in `shoonyakasha.keys` and can be imported without it.

| Group | Names and values |
|---|---|
| Entity sentinel | `NULL_ENTITY = 4294967295` |
| Camera | `CAMERA_PERSPECTIVE = 0`, `CAMERA_ORTHOGRAPHIC = 1` |
| Light | `LIGHT_DIRECTIONAL = 0`, `LIGHT_POINT = 1`, `LIGHT_SPOT = 2` |
| Rigid body | `RIGIDBODY_STATIC = 0`, `RIGIDBODY_KINEMATIC = 1`, `RIGIDBODY_DYNAMIC = 2` |
| Collider | `COLLIDER_BOX = 0`, `COLLIDER_SPHERE = 1`, `COLLIDER_CAPSULE = 2`, `COLLIDER_MESH = 3`, `COLLIDER_PLANE = 4` |
| UI anchors | `UI_ANCHOR_TOP_LEFT`, `TOP_CENTER`, `TOP_RIGHT`, `MIDDLE_LEFT`, `MIDDLE_CENTER`, `MIDDLE_RIGHT`, `BOTTOM_LEFT`, `BOTTOM_CENTER`, `BOTTOM_RIGHT`: values 0–8 respectively, each with the `UI_ANCHOR_` prefix |
| Text alignment | `TEXT_ALIGN_LEFT = 0`, `TEXT_ALIGN_CENTER = 1`, `TEXT_ALIGN_RIGHT = 2` |

Enum existence is not implementation support: Mesh colliders fall back to a box, and Python has no collider-shape setter. See [physics](../../guides/physics.md).

`sk.keys` provides GLFW-style named key/button/action constants and `name(code)`. Inspect [keys.py](../../../python/shoonyakasha/keys.py) for the complete set. C++ facade enums are in [FacadeTypes.h](../../../include/Facade/FacadeTypes.h).
