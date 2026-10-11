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
| Text alignment | `TEXT_ALIGN_LEFT = 0`, `TEXT_ALIGN_CENTER = 1`, `TEXT_ALIGN_RIGHT = 2`, `TEXT_ALIGN_START = 3`, `TEXT_ALIGN_END = 4` (start and end of each canvas UI paragraph's direction; left and right for 2D text labels) |
| Vertical text alignment (canvas UI) | `TEXT_ALIGN_TOP = 0`, `TEXT_ALIGN_MIDDLE = 1`, `TEXT_ALIGN_BOTTOM = 2` |
| Paragraph direction (canvas UI) | `TEXT_DIRECTION_AUTO = 0`, `TEXT_DIRECTION_LTR = 1`, `TEXT_DIRECTION_RTL = 2`, `TEXT_DIRECTION_LAYOUT = 3` (the element's layout direction) |
| Layout direction (canvas UI) | `LAYOUT_DIRECTION_INHERIT = 0`, `LAYOUT_DIRECTION_LTR = 1`, `LAYOUT_DIRECTION_RTL = 2` |
| Canvas scale mode | `CANVAS_CONSTANT_PIXEL = 0`, `CANVAS_SCALE_WITH_SCREEN = 1` |
| Size to content (canvas UI) | `SIZE_FIT_UNCONSTRAINED = 0`, `SIZE_FIT_MIN = 1`, `SIZE_FIT_PREFERRED = 2` |

Enum existence is not implementation support: Mesh colliders fall back to a box, and Python has no collider-shape setter. See [physics](../../guides/physics.md).

`sk.keys` provides GLFW-style named key/button/action constants and `name(code)`. Inspect [keys.py](../../../python/shoonyakasha/keys.py) for the complete set. C++ facade enums are in [FacadeTypes.h](../../../include/Facade/FacadeTypes.h).
