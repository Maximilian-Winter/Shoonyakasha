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

# The utility modules are pure Python and deliberately imported first: compiling
# shaders or validating a pipeline should work before the extension is built, in
# CI, or on an interpreter the current .pyd was not built for.
from . import assets, pipeline, shaders

# The compiled extension may legitimately be absent or built for a different
# Python version. Failing the whole package import on that hides the utilities
# behind a message ("DLL load failed while importing _shoonyakasha") that does
# not say what to do, so record it and raise something actionable only when an
# engine symbol is actually touched.
_EXTENSION_ERROR = None

try:
    from ._shoonyakasha import (
        # Main classes
        Engine,
        Scene,
        Input,
        Physics,
        Ecs,
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

        # UI anchors (create_ui_panel / create_text)
        UI_ANCHOR_TOP_LEFT,
        UI_ANCHOR_TOP_CENTER,
        UI_ANCHOR_TOP_RIGHT,
        UI_ANCHOR_MIDDLE_LEFT,
        UI_ANCHOR_MIDDLE_CENTER,
        UI_ANCHOR_MIDDLE_RIGHT,
        UI_ANCHOR_BOTTOM_LEFT,
        UI_ANCHOR_BOTTOM_CENTER,
        UI_ANCHOR_BOTTOM_RIGHT,

        # Text alignment (create_text)
            TEXT_ALIGN_LEFT,
            TEXT_ALIGN_CENTER,
            TEXT_ALIGN_RIGHT,

        # Frame capture — usable before an Engine exists, so a script can
        # check for ffmpeg before deciding to record.
        video_recording_available,
        find_ffmpeg,
    )
except ImportError as exc:                       # pragma: no cover - environment
    _EXTENSION_ERROR = exc


_ENGINE_SYMBOLS = frozenset({
    "Engine", "Scene", "Input", "Physics", "Ecs", "GltfResult", "NULL_ENTITY",
    "CAMERA_PERSPECTIVE", "CAMERA_ORTHOGRAPHIC",
    "LIGHT_DIRECTIONAL", "LIGHT_POINT", "LIGHT_SPOT",
    "RIGIDBODY_STATIC", "RIGIDBODY_KINEMATIC", "RIGIDBODY_DYNAMIC",
    "COLLIDER_BOX", "COLLIDER_SPHERE", "COLLIDER_CAPSULE",
    "COLLIDER_MESH", "COLLIDER_PLANE",
    "UI_ANCHOR_TOP_LEFT", "UI_ANCHOR_TOP_CENTER", "UI_ANCHOR_TOP_RIGHT",
    "UI_ANCHOR_MIDDLE_LEFT", "UI_ANCHOR_MIDDLE_CENTER", "UI_ANCHOR_MIDDLE_RIGHT",
    "UI_ANCHOR_BOTTOM_LEFT", "UI_ANCHOR_BOTTOM_CENTER", "UI_ANCHOR_BOTTOM_RIGHT",
    "TEXT_ALIGN_LEFT", "TEXT_ALIGN_CENTER", "TEXT_ALIGN_RIGHT",
    "video_recording_available", "find_ffmpeg",
})


def __getattr__(name):
    """Explain a missing extension at the point of use rather than at import."""
    if name in _ENGINE_SYMBOLS and _EXTENSION_ERROR is not None:
        raise ImportError(
            "shoonyakasha.%s needs the compiled extension, which failed to "
            "load:\n    %s\n\n"
            "Usually one of:\n"
            "  - it has not been built yet      -> python -m pip install .\n"
            "  - it was built for another Python version\n"
            "  - vulkan-1.dll is not beside it or on PATH\n\n"
            "shoonyakasha.shaders, .assets and .pipeline do not need it."
            % (name, _EXTENSION_ERROR)) from _EXTENSION_ERROR
    raise AttributeError("module 'shoonyakasha' has no attribute '%s'" % name)


def extension_available():
    """Is the compiled engine extension loaded? The utilities work either way."""
    return _EXTENSION_ERROR is None


__version__ = "1.0.0"
__all__ = [
    "Engine", "Scene", "Input", "Physics", "Ecs", "GltfResult",
    "NULL_ENTITY",
    "CAMERA_PERSPECTIVE", "CAMERA_ORTHOGRAPHIC",
    "LIGHT_DIRECTIONAL", "LIGHT_POINT", "LIGHT_SPOT",
    "RIGIDBODY_STATIC", "RIGIDBODY_KINEMATIC", "RIGIDBODY_DYNAMIC",
    "COLLIDER_BOX", "COLLIDER_SPHERE", "COLLIDER_CAPSULE",
    "COLLIDER_MESH", "COLLIDER_PLANE",
    "UI_ANCHOR_TOP_LEFT", "UI_ANCHOR_TOP_CENTER", "UI_ANCHOR_TOP_RIGHT",
    "UI_ANCHOR_MIDDLE_LEFT", "UI_ANCHOR_MIDDLE_CENTER", "UI_ANCHOR_MIDDLE_RIGHT",
    "UI_ANCHOR_BOTTOM_LEFT", "UI_ANCHOR_BOTTOM_CENTER", "UI_ANCHOR_BOTTOM_RIGHT",
    "TEXT_ALIGN_LEFT", "TEXT_ALIGN_CENTER", "TEXT_ALIGN_RIGHT",
    "video_recording_available", "find_ffmpeg",
    # Pure-Python utilities
    "assets", "pipeline", "shaders", "extension_available",
]
