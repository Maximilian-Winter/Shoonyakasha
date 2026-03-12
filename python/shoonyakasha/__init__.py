#
# shoonyakasha — Python bindings for the Shoonyakasha Vulkan engine
#
# शून्याकाश — Void-Space Engine
#
# Usage:
#     import shoonyakasha as sk
#     engine = sk.Engine(title="My Game", pipeline_json_path="pipeline.json")
#     engine.set_on_init(lambda: print("Hello from Python!"))
#     engine.run()
#

from ._shoonyakasha import (
    # Main classes
    Engine,
    Scene,
    Input,
    Physics,
    GltfResult,

    # Entity handle sentinel
    NULL_ENTITY,

    # Camera types
    CAMERA_PERSPECTIVE,
    CAMERA_ORTHOGRAPHIC,

    # Light types
    LIGHT_DIRECTIONAL,
    LIGHT_POINT,
    LIGHT_SPOT,

    # Rigid body types
    RIGIDBODY_STATIC,
    RIGIDBODY_KINEMATIC,
    RIGIDBODY_DYNAMIC,

    # Collider shapes
    COLLIDER_BOX,
    COLLIDER_SPHERE,
    COLLIDER_CAPSULE,
    COLLIDER_MESH,
    COLLIDER_PLANE,
)

__version__ = "1.0.0"
__all__ = [
    "Engine", "Scene", "Input", "Physics", "GltfResult",
    "NULL_ENTITY",
    "CAMERA_PERSPECTIVE", "CAMERA_ORTHOGRAPHIC",
    "LIGHT_DIRECTIONAL", "LIGHT_POINT", "LIGHT_SPOT",
    "RIGIDBODY_STATIC", "RIGIDBODY_KINEMATIC", "RIGIDBODY_DYNAMIC",
    "COLLIDER_BOX", "COLLIDER_SPHERE", "COLLIDER_CAPSULE",
    "COLLIDER_MESH", "COLLIDER_PLANE",
]
