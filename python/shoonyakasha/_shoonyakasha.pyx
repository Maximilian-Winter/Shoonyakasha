# distutils: language = c++
# cython: language_level = 3
#
# _shoonyakasha.pyx — Python bindings for the Shoonyakasha engine facade
#
# 碼道之現 — The manifestation of the Way of Code
#
# Single .pyx wrapping all facade classes to avoid circular cimport.
# All GLM types appear as Python tuples.
# All callbacks are GIL-safe via _callback_bridge.h.
#

from cpython.ref cimport PyObject
from libc.stdint cimport uint8_t, uint32_t, uint64_t, int64_t
from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.pair cimport pair
from libcpp cimport bool as cbool

# ── Import C++ declarations ───────────────────────────────────
from ._facade_types cimport (
    EntityHandle, NullEntity,
    CameraType, CameraType_Perspective, CameraType_Orthographic,
    LightType, LightType_Directional, LightType_Point, LightType_Spot,
    RigidBodyType, RigidBodyType_Static, RigidBodyType_Kinematic, RigidBodyType_Dynamic,
    ColliderShape, ColliderShape_Box, ColliderShape_Sphere, ColliderShape_Capsule,
    ColliderShape_Mesh, ColliderShape_Plane,
    UIAnchor, UIAnchor_TopLeft, UIAnchor_TopCenter, UIAnchor_TopRight,
    UIAnchor_MiddleLeft, UIAnchor_MiddleCenter, UIAnchor_MiddleRight,
    UIAnchor_BottomLeft, UIAnchor_BottomCenter, UIAnchor_BottomRight,
    TextHAlign, TextHAlign_Left, TextHAlign_Center, TextHAlign_Right, TextHAlign_Start, TextHAlign_End,
    TextVAlign, TextVAlign_Top, TextVAlign_Middle, TextVAlign_Bottom,
    TextDirection, TextDirection_Auto, TextDirection_LeftToRight, TextDirection_RightToLeft, TextDirection_FromLayout,
    TextOrientation, TextOrientation_Horizontal, TextOrientation_VerticalRightToLeft,
    TextOrientation_VerticalLeftToRight,
    AccessibleRole, AccessibleRole_Default, AccessibleRole_Group, AccessibleRole_Button, AccessibleRole_CheckBox,
    AccessibleRole_Slider, AccessibleRole_Edit, AccessibleRole_Pane, AccessibleRole_Text,
    LayoutDirection, LayoutDirection_Inherit, LayoutDirection_LeftToRight, LayoutDirection_RightToLeft,
    CanvasScaleMode, CanvasScaleMode_ConstantPixel, CanvasScaleMode_ScaleWithScreen,
    SizeFit, SizeFit_Unconstrained, SizeFit_Min, SizeFit_Preferred,
    EngineConfig, GltfOptions, RecordingOptions, ClipInfo, GltfResult as CppGltfResult,
    RenderStatsSnapshot, RenderPassStats, TextureResidencyStats,
)

from ._engine_api cimport (
    vec2, vec3, vec4, mat4,
    make_vec2, make_vec3, make_vec4, make_mat4,
    vec2_x, vec2_y, vec3_x, vec3_y, vec3_z,
    vec4_x, vec4_y, vec4_z, vec4_w, mat4_get,
    function_void, function_float, function_int, function_uint2,
    function_int_bool, function_float2, function_bool_float,
    make_void_callback, make_update_callback, make_key_callback,
    make_resize_callback, make_key_event_callback,
    make_float2_callback, make_int_bool_callback,
    wrap_py_object, unwrap_py_object, make_system_update_callback,
    CppEngineAPI, CppSceneAPI, CppInputAPI, CppPhysicsAPI, CppEcsAPI, CppUIAPI,
    videoRecordingAvailable, findFfmpeg, checkPipelineJson, loadPipelineDocumentText, Readback,
    CppPlanetLod, LodBody, LodView, LodNodeInfo, LodJob, LodFaceColours,
    CppComputeContext, CppComputeBuffer, CppComputeKernel, ComputeKernelInfo, ComputeKernelBinding,
    ComputeBindingType, ComputeUint3, CppComputeImage, CppComputeBinding, ComputeBindingDim,
    ExrPixelType, saveHdr, saveExr, saveImage, CppBufferStream,
)
from libcpp.memory cimport shared_ptr


# ═══════════════════════════════════════════════════════════════
# Module-level constants
# ═══════════════════════════════════════════════════════════════

NULL_ENTITY = <uint32_t>NullEntity

# Camera types
CAMERA_PERSPECTIVE = <int>CameraType_Perspective
CAMERA_ORTHOGRAPHIC = <int>CameraType_Orthographic

# Light types
LIGHT_DIRECTIONAL = <int>LightType_Directional
LIGHT_POINT = <int>LightType_Point
LIGHT_SPOT = <int>LightType_Spot

# Rigid body types
RIGIDBODY_STATIC = <int>RigidBodyType_Static
RIGIDBODY_KINEMATIC = <int>RigidBodyType_Kinematic
RIGIDBODY_DYNAMIC = <int>RigidBodyType_Dynamic

# Collider shapes
COLLIDER_BOX = <int>ColliderShape_Box
COLLIDER_SPHERE = <int>ColliderShape_Sphere
COLLIDER_CAPSULE = <int>ColliderShape_Capsule
COLLIDER_MESH = <int>ColliderShape_Mesh
COLLIDER_PLANE = <int>ColliderShape_Plane

# UI anchors
UI_ANCHOR_TOP_LEFT = <int>UIAnchor_TopLeft
UI_ANCHOR_TOP_CENTER = <int>UIAnchor_TopCenter
UI_ANCHOR_TOP_RIGHT = <int>UIAnchor_TopRight
UI_ANCHOR_MIDDLE_LEFT = <int>UIAnchor_MiddleLeft
UI_ANCHOR_MIDDLE_CENTER = <int>UIAnchor_MiddleCenter
UI_ANCHOR_MIDDLE_RIGHT = <int>UIAnchor_MiddleRight
UI_ANCHOR_BOTTOM_LEFT = <int>UIAnchor_BottomLeft
UI_ANCHOR_BOTTOM_CENTER = <int>UIAnchor_BottomCenter
UI_ANCHOR_BOTTOM_RIGHT = <int>UIAnchor_BottomRight

# Text alignment
TEXT_ALIGN_LEFT = <int>TextHAlign_Left
TEXT_ALIGN_CENTER = <int>TextHAlign_Center
TEXT_ALIGN_RIGHT = <int>TextHAlign_Right
# The start and end of each paragraph's direction (canvas UI); left and right
# for 2D text labels
TEXT_ALIGN_START = <int>TextHAlign_Start
TEXT_ALIGN_END = <int>TextHAlign_End

# Vertical text alignment (canvas UI)
TEXT_ALIGN_TOP = <int>TextVAlign_Top
TEXT_ALIGN_MIDDLE = <int>TextVAlign_Middle
TEXT_ALIGN_BOTTOM = <int>TextVAlign_Bottom

# Paragraph direction (canvas UI)
TEXT_DIRECTION_AUTO = <int>TextDirection_Auto
TEXT_DIRECTION_LTR = <int>TextDirection_LeftToRight
TEXT_DIRECTION_RTL = <int>TextDirection_RightToLeft
TEXT_DIRECTION_LAYOUT = <int>TextDirection_FromLayout

# Text orientation (canvas UI)
TEXT_ORIENTATION_HORIZONTAL = <int>TextOrientation_Horizontal
TEXT_ORIENTATION_VERTICAL_RL = <int>TextOrientation_VerticalRightToLeft
TEXT_ORIENTATION_VERTICAL_LR = <int>TextOrientation_VerticalLeftToRight

# What an element is to screen readers (canvas UI)
ACCESSIBLE_ROLE_DEFAULT = <int>AccessibleRole_Default
ACCESSIBLE_ROLE_GROUP = <int>AccessibleRole_Group
ACCESSIBLE_ROLE_BUTTON = <int>AccessibleRole_Button
ACCESSIBLE_ROLE_CHECKBOX = <int>AccessibleRole_CheckBox
ACCESSIBLE_ROLE_SLIDER = <int>AccessibleRole_Slider
ACCESSIBLE_ROLE_EDIT = <int>AccessibleRole_Edit
ACCESSIBLE_ROLE_PANE = <int>AccessibleRole_Pane
ACCESSIBLE_ROLE_TEXT = <int>AccessibleRole_Text

# Layout direction (canvas UI)
LAYOUT_DIRECTION_INHERIT = <int>LayoutDirection_Inherit
LAYOUT_DIRECTION_LTR = <int>LayoutDirection_LeftToRight
LAYOUT_DIRECTION_RTL = <int>LayoutDirection_RightToLeft

# Canvas scale modes
CANVAS_CONSTANT_PIXEL = <int>CanvasScaleMode_ConstantPixel
CANVAS_SCALE_WITH_SCREEN = <int>CanvasScaleMode_ScaleWithScreen

# Size-to-content modes (canvas UI)
SIZE_FIT_UNCONSTRAINED = <int>SizeFit_Unconstrained
SIZE_FIT_MIN = <int>SizeFit_Min
SIZE_FIT_PREFERRED = <int>SizeFit_Preferred


# ═══════════════════════════════════════════════════════════════
# Helper: Convert GLM ↔ Python tuples (inline in .pyx)
# ═══════════════════════════════════════════════════════════════

cdef inline tuple _vec2_to_tuple(vec2 v):
    return (vec2_x(v), vec2_y(v))

cdef inline tuple _vec3_to_tuple(vec3 v):
    return (vec3_x(v), vec3_y(v), vec3_z(v))

cdef inline tuple _vec4_to_tuple(vec4 v):
    return (vec4_x(v), vec4_y(v), vec4_z(v), vec4_w(v))

cdef inline tuple _mat4_to_tuple(mat4 m):
    return (
        (mat4_get(m, 0, 0), mat4_get(m, 0, 1), mat4_get(m, 0, 2), mat4_get(m, 0, 3)),
        (mat4_get(m, 1, 0), mat4_get(m, 1, 1), mat4_get(m, 1, 2), mat4_get(m, 1, 3)),
        (mat4_get(m, 2, 0), mat4_get(m, 2, 1), mat4_get(m, 2, 2), mat4_get(m, 2, 3)),
        (mat4_get(m, 3, 0), mat4_get(m, 3, 1), mat4_get(m, 3, 2), mat4_get(m, 3, 3)),
    )

cdef inline vec3 _tuple_to_vec3(object t):
    return make_vec3(<float>t[0], <float>t[1], <float>t[2])

cdef inline vec4 _tuple_to_vec4(object t):
    return make_vec4(<float>t[0], <float>t[1], <float>t[2], <float>t[3])

cdef inline mat4 _tuple_to_mat4(object m):
    # Four columns of four, the shape _mat4_to_tuple returns.
    return make_mat4(<float>m[0][0], <float>m[0][1], <float>m[0][2], <float>m[0][3],
                     <float>m[1][0], <float>m[1][1], <float>m[1][2], <float>m[1][3],
                     <float>m[2][0], <float>m[2][1], <float>m[2][2], <float>m[2][3],
                     <float>m[3][0], <float>m[3][1], <float>m[3][2], <float>m[3][3])

cdef inline vec2 _tuple_to_vec2(object t):
    return make_vec2(<float>t[0], <float>t[1])


# ═══════════════════════════════════════════════════════════════
# GltfResult — Python data class for glTF load results
# ═══════════════════════════════════════════════════════════════

class GltfResult:
    """Result of loading a glTF scene."""
    __slots__ = ('success', 'error', 'entities', 'lights', 'variants',
                 'total_vertices', 'total_indices',
                 'total_textures', 'total_materials',
                 'animation_clips', 'skeleton_count')

    def __init__(self):
        self.success = False
        self.error = ""
        self.entities = []
        self.lights = []            # with load_lights=True: the file's light entities
        self.variants = []          # KHR_materials_variants names, by index
        self.total_vertices = 0
        self.total_indices = 0
        self.total_textures = 0
        self.total_materials = 0
        self.animation_clips = []   # list of (name: str, duration: float)
        self.skeleton_count = 0

    def __repr__(self):
        if self.success:
            parts = [f"success=True, entities={len(self.entities)}",
                     f"vertices={self.total_vertices}, textures={self.total_textures}"]
            if self.skeleton_count > 0:
                parts.append(f"skeletons={self.skeleton_count}, clips={len(self.animation_clips)}")
            return f"GltfResult({', '.join(parts)})"
        return f"GltfResult(success=False, error='{self.error}')"


cdef object _wrap_gltf_result(CppGltfResult& cpp_result):
    """Convert C++ GltfResult to Python GltfResult."""
    r = GltfResult()
    r.success = cpp_result.success
    r.error = cpp_result.error.decode('utf-8', errors='replace')
    r.entities = [<uint32_t>cpp_result.entities[i]
                  for i in range(cpp_result.entities.size())]
    r.lights = [<uint32_t>cpp_result.lights[i] for i in range(cpp_result.lights.size())]
    r.variants = [cpp_result.variants[i].decode('utf-8', errors='replace')
                  for i in range(cpp_result.variants.size())]
    r.total_vertices = cpp_result.totalVertices
    r.total_indices = cpp_result.totalIndices
    r.total_textures = cpp_result.totalTextures
    r.total_materials = cpp_result.totalMaterials
    r.skeleton_count = cpp_result.skeletonCount
    r.animation_clips = [
        (cpp_result.animationClips[i].name.decode('utf-8', errors='replace'),
         cpp_result.animationClips[i].duration)
        for i in range(cpp_result.animationClips.size())
    ]
    return r


# ═══════════════════════════════════════════════════════════════
# Scene — Python wrapper for SceneAPI
# ═══════════════════════════════════════════════════════════════

cdef class Scene:
    """Entity/component/transform management.

    Obtained via engine.scene — do not construct directly.
    """

    cdef CppSceneAPI* _ptr
    cdef bint _owned
    # Strong reference back to the owning Engine.
    #
    # _ptr points into the C++ EngineAPI, which Engine.__dealloc__
    # destroys. Without this, `scene = sk.Engine(...).scene` — or simply
    # storing engine.scene on an object that outlives the engine — left a
    # dangling pointer that the next call dereferenced.
    cdef object _owner

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False
        self._owner = None

    def __init__(self, *args, **kwargs):
        # Reachable from Python as Scene(); _ptr would be NULL and every
        # method would dereference it. The docstring said not to, which is
        # not the same as preventing it.
        raise TypeError(
            "Scene cannot be constructed directly; obtain it from engine.scene")

    def __dealloc__(self):
        if self._owned and self._ptr != NULL:
            del self._ptr

    # ── Entity Lifecycle ──────────────────────────────────────

    def create_entity(self, str name=""):
        """Create a new entity with optional name."""
        cdef string cpp_name = name.encode('utf-8')
        return <uint32_t>self._ptr.createEntity(cpp_name)

    def destroy_entity(self, uint32_t entity):
        """Destroy an entity."""
        self._ptr.destroyEntity(entity)

    def is_valid(self, uint32_t entity):
        """Check if an entity handle is valid."""
        return self._ptr.isValid(entity)

    @property
    def entity_count(self):
        """Total entity count."""
        return self._ptr.getEntityCount()

    # ── Entity Queries ────────────────────────────────────────

    def find_entity_by_name(self, str name):
        """Find entity by name (returns NULL_ENTITY if not found)."""
        cdef string cpp_name = name.encode('utf-8')
        return <uint32_t>self._ptr.findEntityByName(cpp_name)

    def find_entities_with_tag(self, str tag):
        """Find all entities with a given tag."""
        cdef string cpp_tag = tag.encode('utf-8')
        cdef vector[EntityHandle] result = self._ptr.findEntitiesWithTag(cpp_tag)
        return [<uint32_t>result[i] for i in range(result.size())]

    def get_main_camera(self):
        """Get the main camera entity."""
        return <uint32_t>self._ptr.getMainCamera()

    def get_all_entities(self):
        """Get all entity handles."""
        cdef vector[EntityHandle] result = self._ptr.getAllEntities()
        return [<uint32_t>result[i] for i in range(result.size())]

    # ── Component Management ──────────────────────────────────

    def add_component(self, uint32_t entity, str component_name):
        """Add a component by name (e.g. 'Transform', 'Light', 'Camera')."""
        cdef string cpp_name = component_name.encode('utf-8')
        return self._ptr.addComponent(entity, cpp_name)

    def remove_component(self, uint32_t entity, str component_name):
        """Remove a component by name."""
        cdef string cpp_name = component_name.encode('utf-8')
        return self._ptr.removeComponent(entity, cpp_name)

    def has_component(self, uint32_t entity, str component_name):
        """Check if entity has a component by name."""
        cdef string cpp_name = component_name.encode('utf-8')
        return self._ptr.hasComponent(entity, cpp_name)

    def get_component_names(self):
        """List all registered component type names."""
        cdef vector[string] result = self._ptr.getComponentNames()
        return [result[i].decode('utf-8') for i in range(result.size())]

    # ── Name / Tag / Active ───────────────────────────────────

    def get_name(self, uint32_t entity):
        return self._ptr.getName(entity).decode('utf-8', errors='replace')

    def set_name(self, uint32_t entity, str name):
        self._ptr.setName(entity, name.encode('utf-8'))

    def get_tag(self, uint32_t entity):
        return self._ptr.getTag(entity).decode('utf-8', errors='replace')

    def set_tag(self, uint32_t entity, str tag):
        self._ptr.setTag(entity, tag.encode('utf-8'))

    def is_active(self, uint32_t entity):
        return self._ptr.isActive(entity)

    def set_active(self, uint32_t entity, bint active):
        self._ptr.setActive(entity, active)

    # ── Transform ─────────────────────────────────────────────

    def get_position(self, uint32_t entity):
        """Get entity position as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getPosition(entity))

    def set_position(self, uint32_t entity, pos):
        """Set entity position from (x, y, z) tuple."""
        self._ptr.setPosition(entity, _tuple_to_vec3(pos))

    def get_rotation(self, uint32_t entity):
        """Get entity rotation (euler radians) as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getRotation(entity))

    def set_rotation(self, uint32_t entity, rot):
        """Set entity rotation (euler radians) from (x, y, z) tuple."""
        self._ptr.setRotation(entity, _tuple_to_vec3(rot))

    def get_scale(self, uint32_t entity):
        """Get entity scale as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getScale(entity))

    def set_scale(self, uint32_t entity, scale):
        """Set entity scale from (x, y, z) tuple."""
        self._ptr.setScale(entity, _tuple_to_vec3(scale))

    def set_transforms(self, entities, positions=None, rotations=None, scales=None):
        """Set the transforms of many entities in one call.

        `entities` is a sequence or array of N entity handles; `positions`,
        `rotations` (Euler angles in radians) and `scales` are arrays of
        shape (N, 3), converted to float32. One left out (None) is left as
        it is. Entities that are not valid or have no transform are skipped.
        Returns how many were set."""
        import numpy as np
        cdef const uint32_t[::1] handles = np.ascontiguousarray(entities, dtype=np.uint32).reshape(-1)
        cdef size_t count = handles.shape[0]
        arrays = []
        for name, value in (("positions", positions), ("rotations", rotations), ("scales", scales)):
            if value is None:
                arrays.append(None)
                continue
            rows = np.ascontiguousarray(value, dtype=np.float32)
            if rows.shape != (count, 3):
                raise ValueError("%s: shape %s, expected (%d, 3)" % (name, rows.shape, count))
            arrays.append(rows.reshape(-1))
        if count == 0:
            return 0
        cdef const float[::1] p, r, s
        cdef const float* pp = NULL
        cdef const float* rp = NULL
        cdef const float* sp = NULL
        if arrays[0] is not None:
            p = arrays[0]
            pp = &p[0]
        if arrays[1] is not None:
            r = arrays[1]
            rp = &r[0]
        if arrays[2] is not None:
            s = arrays[2]
            sp = &s[0]
        return self._ptr.setTransforms(<const EntityHandle*>&handles[0], count, pp, rp, sp)

    def get_transforms(self, entities):
        """The transforms of many entities: (positions, rotations, scales),
        float32 arrays of shape (N, 3), as get_position, get_rotation and
        get_scale give them."""
        import numpy as np
        cdef const uint32_t[::1] handles = np.ascontiguousarray(entities, dtype=np.uint32).reshape(-1)
        cdef size_t count = handles.shape[0]
        out = [np.zeros((count, 3), dtype=np.float32) for _ in range(3)]
        if count == 0:
            return tuple(out)
        cdef float[:, ::1] p = out[0], r = out[1], s = out[2]
        self._ptr.getTransforms(<const EntityHandle*>&handles[0], count, &p[0, 0], &r[0, 0], &s[0, 0])
        return tuple(out)

    def get_world_position(self, uint32_t entity):
        """Get world-space position as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getWorldPosition(entity))

    def get_world_matrix(self, uint32_t entity):
        """Get world matrix as 4x4 tuple-of-tuples (column-major)."""
        return _mat4_to_tuple(self._ptr.getWorldMatrix(entity))

    def get_forward(self, uint32_t entity):
        """Get forward direction as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getForward(entity))

    def get_right(self, uint32_t entity):
        """Get right direction as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getRight(entity))

    def get_up(self, uint32_t entity):
        """Get up direction as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getUp(entity))

    # ── Camera ────────────────────────────────────────────────

    def get_camera_type(self, uint32_t entity):
        return <int>self._ptr.getCameraType(entity)

    def set_camera_type(self, uint32_t entity, int camera_type):
        self._ptr.setCameraType(entity, <CameraType>camera_type)

    def get_camera_fov(self, uint32_t entity):
        return self._ptr.getCameraFov(entity)

    def set_camera_fov(self, uint32_t entity, float fov):
        self._ptr.setCameraFov(entity, fov)

    def get_camera_near(self, uint32_t entity):
        return self._ptr.getCameraNear(entity)

    def set_camera_near(self, uint32_t entity, float near_plane):
        self._ptr.setCameraNear(entity, near_plane)

    def get_camera_far(self, uint32_t entity):
        return self._ptr.getCameraFar(entity)

    def set_camera_far(self, uint32_t entity, float far_plane):
        self._ptr.setCameraFar(entity, far_plane)

    def get_camera_reverse_z(self, uint32_t entity):
        return self._ptr.getCameraReverseZ(entity)

    def set_camera_reverse_z(self, uint32_t entity, bint reverse_z):
        """Perspective only: depth 1 at the near plane, 0 at infinity, no far plane."""
        self._ptr.setCameraReverseZ(entity, reverse_z)

    def get_camera_ortho_size(self, uint32_t entity):
        return self._ptr.getCameraOrthoSize(entity)

    def set_camera_ortho_size(self, uint32_t entity, float size):
        self._ptr.setCameraOrthoSize(entity, size)

    def get_camera_aspect(self, uint32_t entity):
        """The width / height the camera projects at; 0 follows the window."""
        return self._ptr.getCameraAspect(entity)

    def set_camera_aspect(self, uint32_t entity, float aspect):
        """Project at `aspect` (width / height) instead of the window's shape,
        for a camera rendering into an image or a viewport; 0 follows the
        window again."""
        self._ptr.setCameraAspect(entity, aspect)

    def is_camera_main(self, uint32_t entity):
        return self._ptr.isCameraMain(entity)

    def set_camera_main(self, uint32_t entity, bint is_main):
        self._ptr.setCameraMain(entity, is_main)

    # ── Light ─────────────────────────────────────────────────

    def get_light_type(self, uint32_t entity):
        return <int>self._ptr.getLightType(entity)

    def set_light_type(self, uint32_t entity, int light_type):
        self._ptr.setLightType(entity, <LightType>light_type)

    def get_light_color(self, uint32_t entity):
        return _vec3_to_tuple(self._ptr.getLightColor(entity))

    def set_light_color(self, uint32_t entity, color):
        self._ptr.setLightColor(entity, _tuple_to_vec3(color))

    def get_light_intensity(self, uint32_t entity):
        return self._ptr.getLightIntensity(entity)

    def set_light_intensity(self, uint32_t entity, float intensity):
        self._ptr.setLightIntensity(entity, intensity)

    def get_light_range(self, uint32_t entity):
        return self._ptr.getLightRange(entity)

    def set_light_range(self, uint32_t entity, float range):
        self._ptr.setLightRange(entity, range)

    def get_light_cast_shadows(self, uint32_t entity):
        return self._ptr.getLightCastShadows(entity)

    def set_light_cast_shadows(self, uint32_t entity, bint cast_shadows):
        self._ptr.setLightCastShadows(entity, cast_shadows)

    def get_light_cone(self, uint32_t entity):
        """A spot light's (inner, outer) cone half-angles in degrees."""
        return _vec2_to_tuple(self._ptr.getLightCone(entity))

    def set_light_cone(self, uint32_t entity, float inner_degrees, float outer_degrees):
        """A spot light's cone: full intensity within `inner_degrees` of its
        axis, fading to nothing at `outer_degrees` (at most 89)."""
        self._ptr.setLightCone(entity, inner_degrees, outer_degrees)

    def get_light_source_radius(self, uint32_t entity):
        return self._ptr.getLightSourceRadius(entity)

    def set_light_source_radius(self, uint32_t entity, float radius):
        """Radius of the light's emitting sphere. Pipelines that trace shadow
        rays use it for penumbrae; zero is a hard-edged point light."""
        self._ptr.setLightSourceRadius(entity, radius)

    def get_light_source_size(self, uint32_t entity):
        return _vec2_to_tuple(self._ptr.getLightSourceSize(entity))

    def set_light_source_size(self, uint32_t entity, float width, float height):
        """Width and height of the light's emitting rectangle, facing its
        forward axis with its width along its right axis. Pipelines with area
        lights shade it as that rectangle; (0, 0), the default, is none."""
        self._ptr.setLightSourceSize(entity, width, height)

    def get_light_source_image(self, uint32_t entity):
        return self._ptr.getLightSourceImage(entity)

    def set_light_source_image(self, uint32_t entity, uint32_t slot):
        """Which image the light shines with: 0, the default, none;
        pipelines say what other slots mean (the showroom's 1 is its
        "lightImage" for rectangles; in the default pipeline, for a light
        without a rectangle, 1 to 16 make a spot light shine through that
        tile of "lightCookies", and 17 to 32 make a point or spot light
        follow IES profile N - 16 of "lightProfiles"). Published as
        scene.lights[N].source.w."""
        self._ptr.setLightSourceImage(entity, slot)

    # ── Material ──────────────────────────────────────────────

    def set_material_float(self, uint32_t entity, str param, float value):
        cdef string cpp_param = param.encode('utf-8')
        self._ptr.setMaterialFloat(entity, cpp_param, value)

    def get_material_float(self, uint32_t entity, str param, float default_val=0.0):
        cdef string cpp_param = param.encode('utf-8')
        return self._ptr.getMaterialFloat(entity, cpp_param, default_val)

    def set_material_vec3(self, uint32_t entity, str param, value):
        cdef string cpp_param = param.encode('utf-8')
        self._ptr.setMaterialVec3(entity, cpp_param, _tuple_to_vec3(value))

    def get_material_vec3(self, uint32_t entity, str param, default_val=(0.0, 0.0, 0.0)):
        cdef string cpp_param = param.encode('utf-8')
        return _vec3_to_tuple(
            self._ptr.getMaterialVec3(entity, cpp_param, _tuple_to_vec3(default_val)))

    def set_material_vec4(self, uint32_t entity, str param, value):
        cdef string cpp_param = param.encode('utf-8')
        self._ptr.setMaterialVec4(entity, cpp_param, _tuple_to_vec4(value))

    def get_material_vec4(self, uint32_t entity, str param, default_val=(0.0, 0.0, 0.0, 0.0)):
        cdef string cpp_param = param.encode('utf-8')
        return _vec4_to_tuple(
            self._ptr.getMaterialVec4(entity, cpp_param, _tuple_to_vec4(default_val)))

    def has_material_param(self, uint32_t entity, str param):
        cdef string cpp_param = param.encode('utf-8')
        return self._ptr.hasMaterialParam(entity, cpp_param)

    def set_material_texture(self, uint32_t entity, str slot_name, str file_path):
        cdef string cpp_slot = slot_name.encode('utf-8')
        cdef string cpp_path = file_path.encode('utf-8')
        return self._ptr.setMaterialTexture(entity, cpp_slot, cpp_path)

    # ── Sprite / UI ──────────────────────────────────────────

    def set_sprite_texture(self, uint32_t entity, str file_path):
        cdef string cpp_path = file_path.encode('utf-8')
        return self._ptr.setSpriteTexture(entity, cpp_path)

    def set_sprite_color(self, uint32_t entity, color):
        self._ptr.setSpriteColor(entity, _tuple_to_vec4(color))

    def get_sprite_color(self, uint32_t entity):
        return _vec4_to_tuple(self._ptr.getSpriteColor(entity))

    def set_sprite_uv_rect(self, uint32_t entity, uv_rect):
        self._ptr.setSpriteUVRect(entity, _tuple_to_vec4(uv_rect))

    def get_sprite_uv_rect(self, uint32_t entity):
        return _vec4_to_tuple(self._ptr.getSpriteUVRect(entity))

    def is_screen_space_sprite(self, uint32_t entity):
        return self._ptr.isScreenSpaceSprite(entity)

    def set_ui_anchor(self, uint32_t entity, int anchor, offset_pixels=(0.0, 0.0)):
        self._ptr.setUIAnchor(entity, <UIAnchor>anchor, _tuple_to_vec2(offset_pixels))

    def get_ui_anchor(self, uint32_t entity):
        return <int>self._ptr.getUIAnchor(entity)

    def get_ui_anchor_offset(self, uint32_t entity):
        return _vec2_to_tuple(self._ptr.getUIAnchorOffset(entity))

    # ── Text ─────────────────────────────────────────────────

    def set_text(self, uint32_t entity, str text):
        cdef string cpp_text = text.encode('utf-8')
        self._ptr.setText(entity, cpp_text)

    def get_text(self, uint32_t entity):
        return self._ptr.getText(entity).decode('utf-8')

    def set_text_color(self, uint32_t entity, color):
        self._ptr.setTextColor(entity, _tuple_to_vec4(color))

    def set_text_font_size(self, uint32_t entity, float font_size):
        self._ptr.setTextFontSize(entity, font_size)

    def set_text_align(self, uint32_t entity, int align):
        self._ptr.setTextAlign(entity, <TextHAlign>align)

    def set_text_layer_mask(self, uint32_t entity, int mask):
        """Propagated to every glyph entity generated for this label."""
        self._ptr.setTextLayerMask(entity, <uint8_t>mask)

    def set_text_sort_key(self, uint32_t entity, uint32_t sort_key):
        """Draw order — lower draws first, so text over a panel needs a
        higher key than the panel. Labels default to 0."""
        self._ptr.setTextSortKey(entity, sort_key)

    def set_text_visible(self, uint32_t entity, bint visible):
        """Show or hide a label. This is how to hide text — destroying the
        label entity leaves its glyph entities on screen."""
        self._ptr.setTextVisible(entity, visible)

    def is_text_visible(self, uint32_t entity):
        return self._ptr.isTextVisible(entity)

    # ── Renderable ────────────────────────────────────────────

    def is_visible(self, uint32_t entity):
        return self._ptr.isVisible(entity)

    def set_visible(self, uint32_t entity, bint visible):
        self._ptr.setVisible(entity, visible)

    def set_material_variant(self, uint32_t entity, int index):
        """KHR_materials_variants: give an entity from a glTF file with variants
        the material it maps for variant `index` (into GltfResult.variants),
        or its default material for -1 or a variant it does not map.

        Returns False for an entity without variants.
        """
        return self._ptr.setMaterialVariant(entity, index)

    def get_cast_shadows(self, uint32_t entity):
        return self._ptr.getCastShadows(entity)

    def set_cast_shadows(self, uint32_t entity, bint cast_shadows):
        self._ptr.setCastShadows(entity, cast_shadows)

    def get_render_layer_mask(self, uint32_t entity):
        """8-bit layer mask (bits 0-7, default 0xFF = every layer)."""
        return self._ptr.getRenderLayerMask(entity)

    def set_render_layer_mask(self, uint32_t entity, int mask):
        """Restrict which "renderLayerMask"-filtered passes draw this entity."""
        self._ptr.setRenderLayerMask(entity, <uint8_t>mask)

    def get_sort_key(self, uint32_t entity):
        return self._ptr.getSortKey(entity)

    def set_sort_key(self, uint32_t entity, uint32_t sort_key):
        """Draw order hint for passes using "sortMode": "sort_key" - lower draws first."""
        self._ptr.setSortKey(entity, sort_key)

    # ── Hierarchy ─────────────────────────────────────────────

    def get_parent(self, uint32_t entity):
        return <uint32_t>self._ptr.getParent(entity)

    def set_parent(self, uint32_t child, uint32_t parent):
        self._ptr.setParent(child, parent)

    def get_children(self, uint32_t entity):
        cdef vector[EntityHandle] result = self._ptr.getChildren(entity)
        return [<uint32_t>result[i] for i in range(result.size())]

    # ── Animation ─────────────────────────────────────────────

    def get_animation_clip_count(self, uint32_t entity):
        """Get number of animation clips on an entity (0 if not animated)."""
        return self._ptr.getAnimationClipCount(entity)

    def get_animation_clip_name(self, uint32_t entity, int clip_index):
        """Get animation clip name by index."""
        return self._ptr.getAnimationClipName(entity, clip_index).decode('utf-8', errors='replace')

    def get_animation_clip_duration(self, uint32_t entity, int clip_index):
        """Get animation clip duration (seconds) by index."""
        return self._ptr.getAnimationClipDuration(entity, clip_index)

    def play_animation(self, uint32_t entity, int clip_index):
        """Play an animation clip by index (resets time, sets playing)."""
        self._ptr.playAnimation(entity, clip_index)

    def stop_animation(self, uint32_t entity):
        """Stop animation playback (pauses and resets time to 0)."""
        self._ptr.stopAnimation(entity)

    def is_animation_playing(self, uint32_t entity):
        """Check if animation is currently playing."""
        return self._ptr.isAnimationPlaying(entity)

    def get_animation_speed(self, uint32_t entity):
        """Get animation playback speed (default 1.0)."""
        return self._ptr.getAnimationSpeed(entity)

    def set_animation_speed(self, uint32_t entity, float speed):
        """Set animation playback speed."""
        self._ptr.setAnimationSpeed(entity, speed)

    def get_animation_time(self, uint32_t entity):
        """Get current animation time (seconds)."""
        return self._ptr.getAnimationTime(entity)

    def set_animation_time(self, uint32_t entity, float time):
        """Set current animation time (seconds)."""
        self._ptr.setAnimationTime(entity, time)

    def is_animation_looping(self, uint32_t entity):
        """Check if animation is set to loop."""
        return self._ptr.isAnimationLooping(entity)

    def set_animation_looping(self, uint32_t entity, bint loop):
        """Set animation looping."""
        self._ptr.setAnimationLooping(entity, loop)

    def get_current_animation_clip(self, uint32_t entity):
        """Get current animation clip index (-1 if none)."""
        return self._ptr.getCurrentAnimationClip(entity)

    # ── Morph targets ─────────────────────────────────────────

    def get_morph_weights(self, uint32_t entity):
        """The weights of the entity's morph targets, one per target, as a
        list; empty when its mesh has none."""
        return list(self._ptr.getMorphWeights(entity))

    def set_morph_weights(self, uint32_t entity, weights):
        """Set the weights of the entity's morph targets, one per target
        (extra ones dropped, missing ones 0). A playing clip with weight
        channels for the entity's node overwrites them each frame. Returns
        False when its mesh has no morph targets."""
        cdef vector[float] values = [float(w) for w in weights]
        return self._ptr.setMorphWeights(entity, values)

    # ── Serialization ─────────────────────────────────────────

    def save_to_file(self, str path):
        cdef string cpp_path = path.encode('utf-8')
        return self._ptr.saveToFile(cpp_path)

    def load_from_file(self, str path):
        cdef string cpp_path = path.encode('utf-8')
        return self._ptr.loadFromFile(cpp_path)


# ═══════════════════════════════════════════════════════════════
# Input — Python wrapper for InputAPI
# ═══════════════════════════════════════════════════════════════

cdef class Input:
    """Input polling and event callbacks.

    Obtained via engine.input — do not construct directly.
    """

    cdef CppInputAPI* _ptr
    cdef bint _owned
    # Strong reference back to the owning Engine.
    #
    # _ptr points into the C++ EngineAPI, which Engine.__dealloc__
    # destroys. Without this, `scene = sk.Engine(...).scene` — or simply
    # storing engine.scene on an object that outlives the engine — left a
    # dangling pointer that the next call dereferenced.
    cdef object _owner

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False
        self._owner = None

    def __init__(self, *args, **kwargs):
        # Reachable from Python as Input(); _ptr would be NULL and every
        # method would dereference it. The docstring said not to, which is
        # not the same as preventing it.
        raise TypeError(
            "Input cannot be constructed directly; obtain it from engine.input")

    def __dealloc__(self):
        if self._owned and self._ptr != NULL:
            del self._ptr

    # ── Polling ───────────────────────────────────────────────

    def is_key_down(self, int key_code):
        """Check if a key is held down (GLFW key codes)."""
        return self._ptr.isKeyDown(key_code)

    def is_mouse_button_down(self, int button):
        """Check if a mouse button is held down."""
        return self._ptr.isMouseButtonDown(button)

    def get_mouse_position(self):
        """Get mouse position as (x, y) tuple."""
        return _vec2_to_tuple(self._ptr.getMousePosition())

    def get_mouse_delta(self):
        """Get mouse movement since last frame as (dx, dy) tuple."""
        return _vec2_to_tuple(self._ptr.getMouseDelta())

    def get_scroll_delta(self):
        """Get scroll wheel delta as (dx, dy) tuple."""
        return _vec2_to_tuple(self._ptr.getScrollDelta())

    def is_mouse_captured(self):
        """Check if mouse is captured (FPS mode)."""
        return self._ptr.isMouseCaptured()

    def is_gamepad_connected(self):
        """Whether a gamepad is connected: the first joystick GLFW has a
        standard mapping for, polled once per frame."""
        return self._ptr.isGamepadConnected()

    def is_gamepad_button_down(self, int button):
        """Whether a gamepad button is held (keys.GAMEPAD_A ...); False
        without a gamepad or for an unknown button."""
        return self._ptr.isGamepadButtonDown(button)

    def get_gamepad_axis(self, int axis):
        """A gamepad axis (keys.GAMEPAD_LEFT_X ...): sticks from -1 to 1
        (y down positive), triggers from -1 (released) to 1; 0.0 without a
        gamepad or for an unknown axis."""
        return self._ptr.getGamepadAxis(axis)

    # ── Event Callbacks ───────────────────────────────────────

    def set_on_key_event(self, callback):
        """Set key event callback: callback(key_code: int, pressed: bool)."""
        self._ptr.setOnKeyEvent(make_key_event_callback(<PyObject*>callback))

    def set_on_mouse_move(self, callback):
        """Set mouse move callback: callback(x: float, y: float)."""
        self._ptr.setOnMouseMove(make_float2_callback(<PyObject*>callback))

    def set_on_mouse_button(self, callback):
        """Set mouse button callback: callback(button: int, pressed: bool)."""
        self._ptr.setOnMouseButton(make_int_bool_callback(<PyObject*>callback))

    def set_on_mouse_scroll(self, callback):
        """Set mouse scroll callback: callback(x_offset: float, y_offset: float)."""
        self._ptr.setOnMouseScroll(make_float2_callback(<PyObject*>callback))

    def set_on_char(self, callback):
        """Set typed-character callback: callback(char: str), after the keyboard layout
        and dead keys. Arrows, Enter, Backspace and the like send key events only."""
        wrapper = lambda codepoint: callback(chr(codepoint))
        self._ptr.setOnChar(make_key_callback(<PyObject*>wrapper))


# ═══════════════════════════════════════════════════════════════
# Physics — Python wrapper for PhysicsAPI
# ═══════════════════════════════════════════════════════════════

cdef class Physics:
    """Physics simulation control.

    Obtained via engine.physics — do not construct directly.
    """

    cdef CppPhysicsAPI* _ptr
    cdef bint _owned
    # Strong reference back to the owning Engine.
    #
    # _ptr points into the C++ EngineAPI, which Engine.__dealloc__
    # destroys. Without this, `scene = sk.Engine(...).scene` — or simply
    # storing engine.scene on an object that outlives the engine — left a
    # dangling pointer that the next call dereferenced.
    cdef object _owner

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False
        self._owner = None

    def __init__(self, *args, **kwargs):
        # Reachable from Python as Physics(); _ptr would be NULL and every
        # method would dereference it. The docstring said not to, which is
        # not the same as preventing it.
        raise TypeError(
            "Physics cannot be constructed directly; obtain it from engine.physics")

    def __dealloc__(self):
        if self._owned and self._ptr != NULL:
            del self._ptr

    # ── Enable / Disable ──────────────────────────────────────

    @property
    def enabled(self):
        return self._ptr.isEnabled()

    @enabled.setter
    def enabled(self, bint value):
        self._ptr.setEnabled(value)

    # ── World Configuration ───────────────────────────────────

    @property
    def gravity(self):
        """Get gravity as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getGravity())

    @gravity.setter
    def gravity(self, value):
        """Set gravity from (x, y, z) tuple."""
        self._ptr.setGravity(_tuple_to_vec3(value))

    @property
    def fixed_time_step(self):
        return self._ptr.getFixedTimeStep()

    @fixed_time_step.setter
    def fixed_time_step(self, float value):
        self._ptr.setFixedTimeStep(value)

    @property
    def max_sub_steps(self):
        return self._ptr.getMaxSubSteps()

    @max_sub_steps.setter
    def max_sub_steps(self, int value):
        self._ptr.setMaxSubSteps(value)

    # ── Forces / Impulses ─────────────────────────────────────

    def add_force(self, uint32_t entity, force):
        """Apply continuous force from (x, y, z) tuple."""
        self._ptr.addForce(entity, _tuple_to_vec3(force))

    def add_impulse(self, uint32_t entity, impulse):
        """Apply instantaneous impulse from (x, y, z) tuple."""
        self._ptr.addImpulse(entity, _tuple_to_vec3(impulse))

    def add_torque_impulse(self, uint32_t entity, torque):
        """Apply torque impulse from (x, y, z) tuple."""
        self._ptr.addTorqueImpulse(entity, _tuple_to_vec3(torque))

    # ── Velocity ──────────────────────────────────────────────

    def get_linear_velocity(self, uint32_t entity):
        """Get linear velocity as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getLinearVelocity(entity))

    def set_linear_velocity(self, uint32_t entity, velocity):
        """Set linear velocity from (x, y, z) tuple."""
        self._ptr.setLinearVelocity(entity, _tuple_to_vec3(velocity))

    def get_angular_velocity(self, uint32_t entity):
        """Get angular velocity as (x, y, z) tuple."""
        return _vec3_to_tuple(self._ptr.getAngularVelocity(entity))

    def set_angular_velocity(self, uint32_t entity, velocity):
        """Set angular velocity from (x, y, z) tuple."""
        self._ptr.setAngularVelocity(entity, _tuple_to_vec3(velocity))

    # ── Body Management ───────────────────────────────────────

    def rebuild_body(self, uint32_t entity):
        """Rebuild physics body after changing collider shape."""
        self._ptr.rebuildBody(entity)

    @property
    def body_count(self):
        """Total tracked physics body count."""
        return self._ptr.getBodyCount()


# ═══════════════════════════════════════════════════════════════
# Ecs — Python wrapper for EcsAPI (low-level ECS: custom components/systems)
# ═══════════════════════════════════════════════════════════════

cdef class Ecs:
    """Low-level ECS access: custom Python components and systems.

    Obtained via engine.ecs — do not construct directly.

    Complements engine.scene (entity lifecycle, typed built-in component
    accessors) with:
      - Arbitrary Python objects attached to entities as named components
        (set_component/get_component/has_component/remove_component),
        stored by reference — mutating the returned object mutates the
        attached component in place.
      - Custom per-frame systems (add_system), run by the same
        SystemManager/priority ordering as built-in systems
        (TransformSystem=0, CameraSystem=10 by default). A system callback
        that raises is caught, its traceback printed, and reported as a
        failure; after max_consecutive_failures such reports in a row the
        system auto-disables (see is_system_enabled/system_failure_count).
    """

    cdef CppEcsAPI* _ptr
    cdef bint _owned
    # Strong reference back to the owning Engine.
    #
    # _ptr points into the C++ EngineAPI, which Engine.__dealloc__
    # destroys. Without this, `scene = sk.Engine(...).scene` — or simply
    # storing engine.scene on an object that outlives the engine — left a
    # dangling pointer that the next call dereferenced.
    cdef object _owner

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False
        self._owner = None

    def __init__(self, *args, **kwargs):
        # Reachable from Python as Ecs(); _ptr would be NULL and every
        # method would dereference it. The docstring said not to, which is
        # not the same as preventing it.
        raise TypeError(
            "Ecs cannot be constructed directly; obtain it from engine.ecs")

    def __dealloc__(self):
        if self._owned and self._ptr != NULL:
            del self._ptr

    # ── Script Component Access ───────────────────────────────

    def set_component(self, uint32_t entity, str name, value):
        """Attach (or replace) a Python object as a named component on an entity.

        The engine holds a reference to `value` for as long as it's
        attached (until remove_component, overwritten by another
        set_component, or the entity is destroyed).
        """
        cdef string cpp_name = name.encode('utf-8')
        self._ptr.setComponent(entity, cpp_name, wrap_py_object(<PyObject*>value))

    def get_component(self, uint32_t entity, str name):
        """Get a previously-attached component's value (None if absent).

        Returns the SAME object that was attached — mutate it in place to
        update the component.
        """
        cdef string cpp_name = name.encode('utf-8')
        cdef shared_ptr[void] ptr = self._ptr.getComponent(entity, cpp_name)
        if ptr.get() == NULL:
            return None
        return unwrap_py_object(ptr)

    def has_component(self, uint32_t entity, str name):
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.hasComponent(entity, cpp_name)

    def remove_component(self, uint32_t entity, str name):
        """Detach a component. Returns True if it was present."""
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.removeComponent(entity, cpp_name)

    # ── GPU components ───────────────────────────────────────

    def set_gpu_component(self, uint32_t entity, str name, data):
        """Set the row of `entity` in GPU component `name` to the bytes of
        `data` (anything with the buffer protocol and contiguous memory:
        a NumPy record, bytes from struct.pack).

        Every frame, a pipeline buffer layout with "source": {"type":
        "scene", "data": "components", "component": name} receives the
        component's rows, packed in no particular order, and
        scene.custom.components.<name>.count holds how many there are. The
        first row fixes the row size; it must match the layout's element
        size. A destroyed entity's rows go at the next frame. Raises
        ValueError for a row of another size."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef string error = self._ptr.setGpuComponent(
            entity, name.encode('utf-8'), &view[0] if view.shape[0] else NULL, view.shape[0])
        if not error.empty():
            raise ValueError(error.decode('utf-8'))

    def set_gpu_components(self, str name, entities, rows):
        """set_gpu_component for many entities: row i of `rows` (an array
        with one row per entity, such as shape (N, 4) float32 or a
        structured array of N records) for entity i. Sets none when an
        entity is not valid; raises ValueError then, or for rows of another
        size."""
        import numpy as np
        cdef const uint32_t[::1] handles = np.ascontiguousarray(entities, dtype=np.uint32).reshape(-1)
        cdef size_t count = handles.shape[0]
        array = np.ascontiguousarray(rows)
        if count == 0:
            return
        if array.shape[0] != count:
            raise ValueError("%d entities, %d rows" % (count, array.shape[0]))
        cdef const unsigned char[::1] view = memoryview(array).cast('B')
        cdef string error = self._ptr.setGpuComponents(
            name.encode('utf-8'), <const EntityHandle*>&handles[0], count, &view[0], view.shape[0] // count)
        if not error.empty():
            raise ValueError(error.decode('utf-8'))

    def remove_gpu_component(self, uint32_t entity, str name):
        """Remove the row of `entity` from GPU component `name`. Returns True if it had one."""
        return self._ptr.removeGpuComponent(entity, name.encode('utf-8'))

    def gpu_component_count(self, str name):
        """How many rows GPU component `name` has."""
        return self._ptr.gpuComponentCount(name.encode('utf-8'))

    def gpu_component_stride(self, str name):
        """The row size of GPU component `name` in bytes, 0 before its first row."""
        return self._ptr.gpuComponentStride(name.encode('utf-8'))

    def get_component_names(self, uint32_t entity):
        """Names of all script-defined components attached to an entity."""
        cdef vector[string] names = self._ptr.getComponentNames(entity)
        return [n.decode('utf-8') for n in names]

    def find_entities_with_component(self, str name):
        """All entities carrying a component named `name`.

        O(entities with *any* script component) — a scan, not an indexed
        lookup. Fine for gameplay/UI-level scripting; avoid in hot
        per-frame loops over large entity counts.
        """
        cdef string cpp_name = name.encode('utf-8')
        cdef vector[EntityHandle] entities = self._ptr.findEntitiesWithComponent(cpp_name)
        return [<uint32_t>e for e in entities]

    # ── System Management ─────────────────────────────────────

    def add_system(self, str name, callback, int priority=0, int max_consecutive_failures=5):
        """Register a per-frame system: callback(dt: float).

        Runs every frame through the same priority-ordered SystemManager
        as built-in systems (lower priority runs first). Fails (returns
        False) if a system named `name` is already registered — call
        remove_system first to replace one.

        max_consecutive_failures <= 0 disables auto-disable entirely (the
        system keeps running — and failing — every frame).
        """
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.addSystem(
            cpp_name, make_system_update_callback(<PyObject*>callback),
            priority, max_consecutive_failures)

    def remove_system(self, str name):
        """Unregister a system. Returns True if it was found and removed."""
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.removeSystem(cpp_name)

    def has_system(self, str name):
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.hasSystem(cpp_name)

    def set_system_enabled(self, str name, bint enabled):
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.setSystemEnabled(cpp_name, enabled)

    def is_system_enabled(self, str name):
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.isSystemEnabled(cpp_name)

    def get_system_failure_count(self, str name):
        """Consecutive failures reported so far (0 if healthy or not found)."""
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.getSystemFailureCount(cpp_name)

    def get_system_max_failures(self, str name):
        cdef string cpp_name = name.encode('utf-8')
        return self._ptr.getSystemMaxFailures(cpp_name)

    def set_system_max_failures(self, str name, int max_failures):
        cdef string cpp_name = name.encode('utf-8')
        self._ptr.setSystemMaxFailures(cpp_name, max_failures)

    def reset_system_failure_count(self, str name):
        cdef string cpp_name = name.encode('utf-8')
        self._ptr.resetSystemFailureCount(cpp_name)


# ═══════════════════════════════════════════════════════════════
# UI — Python wrapper for UIAPI (the canvas UI)
# ═══════════════════════════════════════════════════════════════

cdef class UI:
    """The canvas UI: screen and world canvases, their elements and widgets.

    Obtained via engine.ui from on_init on — do not construct directly.

    A canvas is an entity; elements are entities below it. Positions and
    sizes are in canvas units, from the top-left of the parent's rect with y
    pointing down. Colours are (r, g, b, a) in sRGB with straight alpha.
    Text is a str. Destroy an element with engine.scene.destroy_entity, which
    destroys its children too.

    Pointer state is updated once a frame, after on_update, so on_update sees
    the frame before's: was_clicked and value_changed are true for one frame.
    """

    cdef CppUIAPI* _ptr
    cdef bint _owned
    # Keeps the Engine, and so the C++ object _ptr points into, alive.
    cdef object _owner

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False
        self._owner = None

    def __init__(self, *args, **kwargs):
        raise TypeError("UI cannot be constructed directly; obtain it from engine.ui")

    def __dealloc__(self):
        if self._owned and self._ptr != NULL:
            del self._ptr

    # ── Fonts ─────────────────────────────────────────────────

    def load_font(self, str path):
        """Id of the font at path, loading it on first use; 0 if it cannot be loaded."""
        return self._ptr.loadFont(path.encode("utf-8"))

    @property
    def default_font(self):
        """The font text uses when it names none: fonts/Roboto-Regular.ttf until set."""
        return self._ptr.getDefaultFont()

    @default_font.setter
    def default_font(self, uint32_t font):
        self._ptr.setDefaultFont(font)

    def set_font_fallbacks(self, uint32_t font, fallbacks):
        """The fonts, in order, that text in font draws characters from when font
        lacks them: Devanagari or Tibetan inside Latin text, say."""
        cdef vector[uint32_t] ids = [<uint32_t>f for f in fallbacks]
        self._ptr.setFontFallbacks(font, ids)

    def set_default_font_fallbacks(self, fallbacks):
        """Fallbacks for every font that has none of its own."""
        cdef vector[uint32_t] ids = [<uint32_t>f for f in fallbacks]
        self._ptr.setDefaultFontFallbacks(ids)

    def set_font_name(self, uint32_t font, str name):
        """The name rich text's <font=name> finds the font by. A font is also
        named after its file without the extension, such as Roboto-Regular."""
        self._ptr.setFontName(font, name.encode("utf-8"))

    def set_font_style(self, uint32_t font, uint32_t bold=0, uint32_t italic=0, uint32_t bold_italic=0):
        """The fonts rich text draws font with in bold, in italic and in both;
        0 for none, in which case the text is drawn thicker or slanted."""
        self._ptr.setFontStyles(font, bold, italic, bold_italic)

    # ── Canvases ──────────────────────────────────────────────

    def create_canvas(self, reference_size=(1920, 1080), int scale_mode=CANVAS_SCALE_WITH_SCREEN,
                      int sort_order=0):
        """A canvas over the screen. A higher sort_order draws over, and takes the pointer first."""
        return <uint32_t>self._ptr.createCanvas(_tuple_to_vec2(reference_size),
                                                <CanvasScaleMode>scale_mode, sort_order)

    def create_world_canvas(self, pixel_size, world_size, float emission=1.0, bint lit=False):
        """A canvas of pixel_size pixels on a quad of world_size units, facing +Z.

        Place it with engine.scene.set_position and set_rotation. Unlit, its
        material emits the canvas at emission times its colour, at a
        brightness exposure does not change. With lit=True the canvas is the
        material's base colour, lit by the scene, and emission is not used.
        """
        return <uint32_t>self._ptr.createWorldCanvas(_tuple_to_vec2(pixel_size), _tuple_to_vec2(world_size),
                                                     emission, lit)

    def set_canvas_scaling(self, uint32_t canvas, int mode, reference_size=(1920, 1080),
                           float scale_factor=1.0, float match=0.5):
        """CANVAS_CONSTANT_PIXEL: scale_factor pixels per unit. CANVAS_SCALE_WITH_SCREEN:
        reference_size fitted to the target, match 0 following its width and 1 its height."""
        self._ptr.setCanvasScaling(canvas, <CanvasScaleMode>mode, _tuple_to_vec2(reference_size),
                                   scale_factor, match)

    def set_canvas_sort_order(self, uint32_t canvas, int sort_order):
        self._ptr.setCanvasSortOrder(canvas, sort_order)

    def set_canvas_pixel_size(self, uint32_t canvas, pixel_size):
        """World canvases: the size of the texture, in pixels."""
        self._ptr.setCanvasPixelSize(canvas, _tuple_to_vec2(pixel_size))

    def set_canvas_clear_color(self, uint32_t canvas, color):
        """World canvases: the colour the texture is cleared to each frame."""
        self._ptr.setCanvasClearColor(canvas, _tuple_to_vec4(color))

    def get_canvas_size(self, uint32_t canvas):
        """(width, height) in canvas units, as last laid out."""
        return _vec2_to_tuple(self._ptr.getCanvasSize(canvas))

    # ── Elements ──────────────────────────────────────────────
    #
    # Each is added as the last child of parent, so it draws over its earlier
    # siblings, centred in it.

    def create_element(self, uint32_t parent, size=(100, 100)):
        """An element with no content, for grouping and placing others."""
        return <uint32_t>self._ptr.createElement(parent, _tuple_to_vec2(size))

    def create_panel(self, uint32_t parent, size=(100, 100), color=(1, 1, 1, 1), str texture="",
                     border=(0, 0, 0, 0)):
        """A filled rectangle, or a 9-slice of texture keeping border (left, top,
        right, bottom texture pixels) unstretched."""
        return <uint32_t>self._ptr.createPanel(parent, _tuple_to_vec2(size), _tuple_to_vec4(color),
                                               texture.encode("utf-8"), _tuple_to_vec4(border))

    def create_image(self, uint32_t parent, size=(100, 100), str texture="", color=(1, 1, 1, 1)):
        """texture stretched over the rect times color; without a texture, color alone."""
        return <uint32_t>self._ptr.createImage(parent, _tuple_to_vec2(size), texture.encode("utf-8"),
                                               _tuple_to_vec4(color))

    def create_text(self, uint32_t parent, str text, float size=24.0, color=(1, 1, 1, 1), uint32_t font=0):
        """Text filling its parent. size is the height from ascent to descent;
        font 0 is the default font."""
        return <uint32_t>self._ptr.createText(parent, text.encode("utf-8"), size, _tuple_to_vec4(color), font)

    def create_button(self, uint32_t parent, str label, size=(160, 40)):
        return <uint32_t>self._ptr.createButton(parent, label.encode("utf-8"), _tuple_to_vec2(size))

    def create_toggle(self, uint32_t parent, str label, bint is_on=False, size=(200, 28)):
        return <uint32_t>self._ptr.createToggle(parent, label.encode("utf-8"), is_on, _tuple_to_vec2(size))

    def create_slider(self, uint32_t parent, float min=0.0, float max=1.0, float value=0.0, size=(200, 24)):
        return <uint32_t>self._ptr.createSlider(parent, min, max, value, _tuple_to_vec2(size))

    def create_text_field(self, uint32_t parent, str placeholder="", size=(240, 36)):
        """A single-line text field showing placeholder while empty. Pressing it
        focuses it; while focused it takes the keyboard."""
        return <uint32_t>self._ptr.createTextField(parent, placeholder.encode("utf-8"), _tuple_to_vec2(size))

    def set_parent(self, uint32_t element, uint32_t parent):
        """Move element to the end of parent's children."""
        self._ptr.setParent(element, parent)

    # ── Layout ────────────────────────────────────────────────

    def set_rect(self, uint32_t element, anchor_min=(0.5, 0.5), anchor_max=(0.5, 0.5), pivot=(0.5, 0.5),
                 position=(0, 0), size=(100, 100)):
        """Anchors are fractions of the parent's rect, (0, 0) its top-left. With equal
        anchors size is the size; with different ones it is added to the span between
        them. position offsets the pivot, a fraction of the element's own rect."""
        self._ptr.setRect(element, _tuple_to_vec2(anchor_min), _tuple_to_vec2(anchor_max),
                          _tuple_to_vec2(pivot), _tuple_to_vec2(position), _tuple_to_vec2(size))

    def set_anchor(self, uint32_t element, anchor):
        """Both anchors and the pivot: (0, 0) places by the top-left corner from the
        parent's top-left, (1, 1) by the bottom-right from its bottom-right."""
        self._ptr.setAnchor(element, _tuple_to_vec2(anchor))

    def set_position(self, uint32_t element, position):
        self._ptr.setPosition(element, _tuple_to_vec2(position))

    def set_size(self, uint32_t element, size):
        self._ptr.setSize(element, _tuple_to_vec2(size))

    def get_rect(self, uint32_t element):
        """(x, y, width, height) in canvas units, as last laid out."""
        return _vec4_to_tuple(self._ptr.getRect(element))

    def set_visible(self, uint32_t element, bint visible):
        self._ptr.setVisible(element, visible)

    def is_visible(self, uint32_t element):
        return self._ptr.isVisible(element)

    def set_clip(self, uint32_t element, bint clip):
        """Clip the element's descendants to its rect."""
        self._ptr.setClip(element, clip)

    # ── Layout groups ─────────────────────────────────────────
    #
    # A layout group places its children itself, in order, ignoring their
    # anchors, and sizes them from their layout sizes: min, preferred when
    # there is room, and a share of what is left by flexible size. Text reports
    # its measured size, an image its texture's, anything else its own size.

    def create_horizontal_layout(self, uint32_t parent, size=(100, 100), float spacing=0.0,
                                 padding=(0, 0, 0, 0)):
        """A row of children, left to right. padding is (left, top, right, bottom)."""
        return <uint32_t>self._ptr.createHorizontalLayout(parent, _tuple_to_vec2(size), spacing,
                                                          _tuple_to_vec4(padding))

    def create_vertical_layout(self, uint32_t parent, size=(100, 100), float spacing=0.0,
                               padding=(0, 0, 0, 0)):
        """A column of children, top to bottom. padding is (left, top, right, bottom)."""
        return <uint32_t>self._ptr.createVerticalLayout(parent, _tuple_to_vec2(size), spacing,
                                                        _tuple_to_vec4(padding))

    def create_grid_layout(self, uint32_t parent, size=(100, 100), cell_size=(100, 100), int columns=0,
                           spacing=(0, 0), padding=(0, 0, 0, 0)):
        """Cells of cell_size, row by row; columns 0 fits as many as the width allows."""
        return <uint32_t>self._ptr.createGridLayout(parent, _tuple_to_vec2(size), _tuple_to_vec2(cell_size),
                                                    columns, _tuple_to_vec2(spacing), _tuple_to_vec4(padding))

    def set_layout_alignment(self, uint32_t group, alignment):
        """Where a group puts children in room they leave: (0, 0) top-left, (0.5, 0.5) centre."""
        self._ptr.setLayoutAlignment(group, _tuple_to_vec2(alignment))

    def set_layout_child_sizing(self, uint32_t group, bint control_width=True, bint control_height=True,
                                bint expand_width=False, bint expand_height=False):
        """For a row or column: control sizes children from their layout sizes; expand
        along the group's axis shares spare room, across it fills the group."""
        self._ptr.setLayoutChildSizing(group, control_width, control_height, expand_width, expand_height)

    def set_layout_element(self, uint32_t element, min=(-1, -1), preferred=(-1, -1), flexible=(-1, -1),
                           bint ignore=False):
        """Override the layout sizes element reports; -1 keeps its own on that axis.
        ignore leaves it to its anchors inside a group."""
        self._ptr.setLayoutElement(element, _tuple_to_vec2(min), _tuple_to_vec2(preferred),
                                   _tuple_to_vec2(flexible), ignore)

    def set_size_to_content(self, uint32_t element, int horizontal=SIZE_FIT_UNCONSTRAINED,
                            int vertical=SIZE_FIT_UNCONSTRAINED):
        """Size an element placed by its anchors to its own layout sizes, per axis:
        SIZE_FIT_UNCONSTRAINED, SIZE_FIT_MIN or SIZE_FIT_PREFERRED."""
        self._ptr.setSizeToContent(element, <SizeFit>horizontal, <SizeFit>vertical)

    def get_preferred_size(self, uint32_t element):
        """(width, height): the preferred layout size as last measured."""
        return _vec2_to_tuple(self._ptr.getPreferredSize(element))

    # ── Scroll views ──────────────────────────────────────────
    #
    # A scroll view clips a column and scrolls it: by the wheel over it, by
    # dragging the column (a press on a button inside that turns into a drag is
    # not a click), and by its scrollbars, hidden while everything fits.
    # Positions run from 0 at the start (top, left) to 1 at the end.

    def create_scroll_view(self, uint32_t parent, size=(300, 200), bint horizontal=False, bint vertical=True):
        """A scroll view; add children to scroll_content(view)."""
        return <uint32_t>self._ptr.createScrollView(parent, _tuple_to_vec2(size), horizontal, vertical)

    def scroll_content(self, uint32_t view):
        """The column inside the view that holds its children: they stack top to
        bottom, spanning the view's width when it scrolls only vertically."""
        return <uint32_t>self._ptr.getScrollContent(view)

    def get_scroll_position(self, uint32_t view):
        """(x, y), each 0 at the start and 1 at the end."""
        return _vec2_to_tuple(self._ptr.getScrollPosition(view))

    def set_scroll_position(self, uint32_t view, position):
        """Move the view, stopping any drift from a released drag."""
        self._ptr.setScrollPosition(view, _tuple_to_vec2(position))

    def scroll_to(self, uint32_t view, uint32_t element):
        """Scroll the least distance that brings element fully into view."""
        self._ptr.scrollTo(view, element)

    def set_scroll_settings(self, uint32_t view, float wheel_speed=48.0, bint inertia=True):
        """Canvas units per wheel step, and whether a released drag keeps moving."""
        self._ptr.setScrollSettings(view, wheel_speed, inertia)

    def set_scroll_elastic(self, uint32_t view, bint elastic=True, float elasticity=0.1):
        """Let dragging, and coasting after a drag, take the view past its
        ends with growing resistance; it springs back in about elasticity
        seconds. Off for a new view. While past an end, get_scroll_position
        is below 0 or above 1."""
        self._ptr.setScrollElastic(view, elastic, elasticity)

    # ── Content ───────────────────────────────────────────────
    #
    # The text methods act on the element's own text or, for a button or
    # toggle, on its label. On a text field, set_text and get_text act on what
    # the user edits, and the font methods on how it is drawn.

    def set_text(self, uint32_t element, str text):
        self._ptr.setText(element, text.encode("utf-8"))

    def get_text(self, uint32_t element):
        return self._ptr.getText(element).decode("utf-8")

    def set_font(self, uint32_t element, uint32_t font):
        self._ptr.setFont(element, font)

    def set_font_size(self, uint32_t element, float size):
        self._ptr.setFontSize(element, size)

    def set_text_align(self, uint32_t element, int horizontal, int vertical=TEXT_ALIGN_TOP):
        """TEXT_ALIGN_LEFT/CENTER/RIGHT/START/END and TEXT_ALIGN_TOP/MIDDLE/BOTTOM.
        START and END follow each paragraph's direction."""
        self._ptr.setTextAlign(element, <TextHAlign>horizontal, <TextVAlign>vertical)

    def set_text_direction(self, uint32_t element, int direction):
        """TEXT_DIRECTION_AUTO (the default: from each paragraph's first letter
        with a direction, or the element's layout direction without one),
        TEXT_DIRECTION_LTR, TEXT_DIRECTION_RTL or TEXT_DIRECTION_LAYOUT (always
        the layout direction). It sets where START and END align and the order
        of words of both directions."""
        self._ptr.setTextDirection(element, <TextDirection>direction)

    def set_layout_direction(self, uint32_t element, int direction):
        """LAYOUT_DIRECTION_INHERIT, LAYOUT_DIRECTION_LTR or LAYOUT_DIRECTION_RTL
        for a canvas or an element: right to left mirrors how its children are
        placed, the order of layout groups, sliders, toggles and scroll views.
        Elements inherit it; a canvas lays out left to right unless set."""
        self._ptr.setLayoutDirection(element, <LayoutDirection>direction)

    def is_right_to_left(self, uint32_t element):
        """Whether the element lays out right to left, as last laid out."""
        return self._ptr.isRightToLeft(element)

    def set_mirror_in_rtl(self, uint32_t element, bint mirror=True):
        """Flip the element's image left for right where it lays out right to
        left, for an arrow or other directional icon."""
        self._ptr.setMirrorInRightToLeft(element, mirror)

    def set_text_wrap(self, uint32_t element, bint wrap):
        self._ptr.setTextWrap(element, wrap)

    def set_text_orientation(self, uint32_t element, int orientation, bint upright_digits=False):
        """TEXT_ORIENTATION_HORIZONTAL, or columns down the rect stacked from the
        right (TEXT_ORIENTATION_VERTICAL_RL, Chinese and Japanese) or from the left
        (TEXT_ORIENTATION_VERTICAL_LR, Mongolian). In columns, Chinese, Japanese and
        Korean stand upright with their vertical forms and other characters are
        turned clockwise; with upright_digits, runs of one or two digits stand
        upright side by side."""
        self._ptr.setTextOrientation(element, <TextOrientation>orientation, upright_digits)

    def set_accessible(self, uint32_t element, str name="", str description="", bint hidden=False,
                       int role=ACCESSIBLE_ROLE_DEFAULT):
        """What screen readers are told of the element (on Windows, through UI
        Automation). name replaces the name it has without it (a button's or
        toggle's label, a text's own text, a field's placeholder; sliders and
        scroll views have none) and is read out with description. An element
        given a name becomes a group if it has no role of its own. hidden leaves
        it and its descendants out, for decoration. role (ACCESSIBLE_ROLE_*)
        replaces the role its components give it."""
        self._ptr.setAccessible(element, name.encode("utf-8"), description.encode("utf-8"), hidden,
                                <AccessibleRole>role)

    def set_rich_text(self, uint32_t element, bint enabled=True):
        """Read the text as markup: <color=#RRGGBB> or <color=#RRGGBBAA>, <b>, <i>,
        <size=N> in canvas units, <font=name>, <link=id>, <u> and <s>, each closed
        by </name>, with &lt; and &gt; for the brackets. A tag that is not one of
        these is shown as text."""
        self._ptr.setRichText(element, enabled)

    def set_link_color(self, uint32_t element, color):
        """The colour of a link in rich text while the pointer is over it."""
        self._ptr.setLinkColor(element, _tuple_to_vec4(color))

    def clicked_link(self, uint32_t element):
        """The id of the link in the text the user clicked this frame, or ''."""
        return self._ptr.clickedLink(element).decode("utf-8")

    def hovered_link(self, uint32_t element):
        """The id of the link in the text under the pointer, or ''."""
        return self._ptr.hoveredLink(element).decode("utf-8")

    def set_color(self, uint32_t element, color):
        """The colour of the element's own text, image and panel."""
        self._ptr.setColor(element, _tuple_to_vec4(color))

    def set_geometry(self, uint32_t element, positions, indices, colors):
        """Replace solid triangles in element-local units; empty arrays clear them."""
        cdef vector[vec2] points
        cdef vector[vec4] tint
        cdef vector[uint32_t] triangles
        for p in positions:
            points.push_back(_tuple_to_vec2(p))
        for c in colors:
            tint.push_back(_tuple_to_vec4(c))
        for i in indices:
            if not isinstance(i, int) or i < 0 or i >= len(positions):
                raise ValueError("geometry index out of bounds")
            triangles.push_back(i)
        self._ptr.setGeometry(element, points, triangles, tint)

    def set_widget_part_color(self, uint32_t element, str part, color):
        """Style surface, text, track, fill, or handle without child-order assumptions."""
        self._ptr.setWidgetPartColor(element, part.encode('utf-8'), _tuple_to_vec4(color))

    def set_interaction_colors(self, uint32_t element, normal, hover, pressed, focused, disabled):
        """Set multiplicative normal, hover, pressed, focused and disabled colors."""
        self._ptr.setInteractionColors(element, _tuple_to_vec4(normal), _tuple_to_vec4(hover),
            _tuple_to_vec4(pressed), _tuple_to_vec4(focused), _tuple_to_vec4(disabled))

    def set_texture(self, uint32_t element, str path):
        """The texture of the element's image or panel. False if it cannot be loaded."""
        return self._ptr.setTexture(element, path.encode("utf-8"))

    def set_texture_data(self, uint32_t element, data, uint32_t width=0, uint32_t height=0,
                         str format="R8G8B8A8_SRGB"):
        """The texture of the element's image or panel, from texels in memory.

        `data` holds rows of tightly packed texels, top row first: a NumPy
        array shaped (height, width, channels) or (height, width), whose
        shape gives the size, or bytes with `width` and `height`. `format` is
        a pipeline JSON format name. The texture belongs to the element and
        is freed once it gets another or is destroyed. Uploading waits for
        the GPU. Raises ValueError when the bytes or the format do not fit,
        or the element is not an image or a panel.
        """
        cdef const unsigned char[::1] view
        cdef string error
        mv = memoryview(data)
        if width == 0 or height == 0:
            if mv.ndim < 2:
                raise ValueError("give width and height, or data shaped (height, width[, channels])")
            height, width = mv.shape[0], mv.shape[1]
        view = mv.cast('B')
        if view.shape[0] == 0:
            raise ValueError("the image has no texels")
        error = self._ptr.setTextureData(element, &view[0], view.shape[0], width, height, format.encode("utf-8"))
        if not error.empty():
            raise ValueError(error.decode("utf-8"))

    def set_panel_border(self, uint32_t element, border, float border_scale=1.0):
        self._ptr.setPanelBorder(element, _tuple_to_vec4(border), border_scale)

    def set_raycast_target(self, uint32_t element, bint target):
        """Whether the element's image or panel stops the pointer."""
        self._ptr.setRaycastTarget(element, target)

    # ── Interaction ───────────────────────────────────────────

    def set_interactable(self, uint32_t element, bint enabled=True):
        """Make the element take the pointer, or disable it. Widgets are interactable."""
        self._ptr.setInteractable(element, enabled)

    def add_tint_target(self, uint32_t interactable, uint32_t target):
        """Make target's panel, image and text colours take the state colour
        of interactable (hover, pressed, focused, disabled). Buttons, toggles
        and sliders list their label and parts already."""
        self._ptr.addTintTarget(interactable, target)

    def remove_tint_target(self, uint32_t interactable, uint32_t target):
        """Stop target taking interactable's state colour."""
        self._ptr.removeTintTarget(interactable, target)

    def get_tint_targets(self, uint32_t interactable):
        """The elements that take interactable's state colour."""
        cdef vector[EntityHandle] result = self._ptr.getTintTargets(interactable)
        return [<uint32_t>result[i] for i in range(result.size())]

    def is_hovered(self, uint32_t element):
        return self._ptr.isHovered(element)

    def is_pressed(self, uint32_t element):
        return self._ptr.isPressed(element)

    def was_clicked(self, uint32_t element):
        """Released over the element after being pressed on it, this frame."""
        return self._ptr.wasClicked(element)

    def value_changed(self, uint32_t element):
        """A toggle's or slider's value was changed by the pointer, this frame."""
        return self._ptr.valueChanged(element)

    def toggle_value(self, uint32_t toggle):
        return self._ptr.getToggle(toggle)

    def set_toggle_value(self, uint32_t toggle, bint is_on):
        self._ptr.setToggle(toggle, is_on)

    def slider_value(self, uint32_t slider):
        return self._ptr.getSliderValue(slider)

    def set_slider_value(self, uint32_t slider, float value):
        self._ptr.setSliderValue(slider, value)

    def set_slider_range(self, uint32_t slider, float min, float max, bint whole_numbers=False):
        self._ptr.setSliderRange(slider, min, max, whole_numbers)

    def focus(self, uint32_t element):
        """Give element keyboard and gamepad focus; NULL_ENTITY clears it. Pressing an
        element focuses it too; Tab, the arrow keys and the D-pad move focus, Enter,
        Space or gamepad A activate it, Escape or gamepad B clear it."""
        self._ptr.focus(element)

    @property
    def focused_element(self):
        """The focused element, or NULL_ENTITY."""
        return <uint32_t>self._ptr.getFocusedElement()

    @property
    def keyboard_captured(self):
        """Does the focused element take the keyboard (a text field)? Then the camera
        sees no keys and on_key_pressed is not called; check it before acting on keys."""
        return self._ptr.isKeyboardCaptured()

    def set_navigable(self, uint32_t element, bint navigable):
        """Whether Tab, the arrow keys and the D-pad can reach element."""
        self._ptr.setNavigable(element, navigable)

    @property
    def pointer_over_ui(self):
        """Is the pointer over a UI element, or pressing one? Check it before
        treating a click as one in the scene."""
        return self._ptr.isPointerOverUI()

    @property
    def pointer_canvas(self):
        """The canvas under the pointer, or NULL_ENTITY."""
        return <uint32_t>self._ptr.getPointerCanvas()

    @property
    def pointer_position(self):
        """The pointer on pointer_canvas, in canvas units."""
        return _vec2_to_tuple(self._ptr.getPointerPosition())

    @property
    def hovered_element(self):
        """The interactable element under the pointer, or NULL_ENTITY."""
        return <uint32_t>self._ptr.getHoveredElement()

    # ── Text fields ───────────────────────────────────────────
    #
    # A focused field takes typed text, Backspace and Delete (with Ctrl, a
    # word), Left and Right (with Ctrl, by word), Home and End, Shift to
    # select, Ctrl+A, C, X and V with the system clipboard, and Enter to
    # submit. Pressing places the caret, dragging selects and a double-click
    # selects a word. set_text puts the caret at the end. There is no input
    # method (IME) composition.

    def text_submitted(self, uint32_t field):
        """The user pressed Enter in the field, this frame."""
        return self._ptr.textSubmitted(field)

    def text_changed(self, uint32_t field):
        """The user changed the field's text, this frame."""
        return self._ptr.textChanged(field)

    def select_all(self, uint32_t field):
        self._ptr.selectAll(field)

    def set_text_field_options(self, uint32_t field, uint32_t max_length=0, bint password=False,
                               bint read_only=False):
        """max_length in characters, 0 for no limit; password draws a dot per character
        and stops copying; read_only keeps selecting and copying but not editing."""
        self._ptr.setTextFieldOptions(field, max_length, password, read_only)

    def set_text_field_multi_line(self, uint32_t field, bint multi_line=True, bint submit_on_enter=False):
        """Let the field hold many lines: its text wraps and scrolls up and
        down, Up and Down move between lines, and Enter starts a new line while
        Ctrl+Enter submits. With submit_on_enter, Enter submits and Shift+Enter
        starts a new line. False goes back to one line."""
        self._ptr.setTextFieldMultiLine(field, multi_line, submit_on_enter)

    def get_text_field_composition(self, uint32_t field):
        """What an input method is composing in the field: shown in the field
        but not yet part of its text. Empty when nothing is being composed there."""
        return self._ptr.getTextFieldComposition(field).decode("utf-8")


# ═══════════════════════════════════════════════════════════════
# Compute — compute shaders on buffers (include/Compute/Compute.h)
# ═══════════════════════════════════════════════════════════════

cdef ComputeUint3 _uint3(value, str what):
    """An int or a sequence of up to three ints, missing ones 1."""
    cdef ComputeUint3 out
    values = (value,) if isinstance(value, int) else tuple(value)
    if not 1 <= len(values) <= 3:
        raise ValueError("%s: one to three numbers" % what)
    values = values + (1,) * (3 - len(values))
    for i in range(3):
        if values[i] < 0:
            raise ValueError("%s: negative" % what)
        out[i] = <uint32_t>values[i]
    return out


cdef class GpuBuffer:
    """A buffer in device memory, made by ComputeContext.buffer or
    ComputeContext.upload, usable as a storage or uniform buffer.
    upload() and the to_* methods copy and return once the copy is done.
    """
    cdef CppComputeBuffer _buf
    cdef ComputeContext _context

    def __init__(self):
        raise TypeError("use ComputeContext.buffer() or ComputeContext.upload()")

    @staticmethod
    cdef GpuBuffer _wrap(CppComputeBuffer buf, ComputeContext context):
        cdef GpuBuffer out = GpuBuffer.__new__(GpuBuffer)
        out._buf = buf
        out._context = context
        return out

    @property
    def size(self):
        """Size in bytes."""
        return self._buf.size()

    @property
    def valid(self):
        """False once the context is shut down."""
        return self._buf.valid()

    def __len__(self):
        return self._buf.size()

    def upload(self, data, size_t offset=0):
        """Copy `data` (anything with the buffer protocol and contiguous
        memory, such as a NumPy array or bytes) into the buffer at `offset`
        bytes."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef size_t n = view.shape[0]
        if n == 0:
            return
        with nogil:
            self._buf.upload(&view[0], n, offset)

    def download_into(self, out, size_t offset=0):
        """Copy bytes from the buffer at `offset` into `out`, a writable
        object with the buffer protocol and contiguous memory, as many as it
        holds."""
        cdef unsigned char[::1] view = memoryview(out).cast('B')
        cdef size_t n = view.shape[0]
        if n == 0:
            return
        with nogil:
            self._buf.download(&view[0], n, offset)

    def to_bytes(self, size_t offset=0, size=None):
        """`size` bytes (by default the rest of the buffer) from `offset`."""
        cdef size_t total = self._buf.size()
        if offset > total:
            raise ValueError("offset %d is past the end of a %d-byte buffer" % (offset, total))
        out = bytearray(total - offset if size is None else size)
        self.download_into(out, offset)
        return bytes(out)

    def to_numpy(self, dtype="float32", shape=None):
        """The whole buffer as a new NumPy array of `dtype`, reshaped to
        `shape` when given. The buffer's size must be a multiple of the
        dtype's size."""
        import numpy as np
        dt = np.dtype(dtype)
        cdef size_t total = self._buf.size()
        if total % dt.itemsize != 0:
            raise ValueError("a %d-byte buffer is not a whole number of %s values" % (total, dt))
        out = np.empty(total // dt.itemsize, dtype=dt)
        self.download_into(out)
        return out if shape is None else out.reshape(shape)

    def __repr__(self):
        return "GpuBuffer(size=%d%s)" % (self._buf.size(), "" if self._buf.valid() else ", released")


import re as _re

_FORMAT_KINDS = {"SFLOAT": "f", "UNORM": "u", "SRGB": "u", "UINT": "u", "SNORM": "i", "SINT": "i"}


def _format_layout(str name):
    """(NumPy dtype string, channels) of a pipeline JSON format name whose
    channels share one plain type ("R32G32B32A32_SFLOAT" -> ("<f4", 4)), a
    packed format ("B10G11R11_UFLOAT_PACK32") -> ("<u4", 1), or None."""
    parts = name.split("_")
    if len(parts) == 3 and parts[2].startswith("PACK"):
        return ("<u%d" % (int(parts[2][4:]) // 8), 1)
    if len(parts) != 2 or parts[1] not in _FORMAT_KINDS:
        return None
    if not _re.fullmatch(r"(?:[RGBA]\d+)+", parts[0]):
        return None
    channels = _re.findall(r"\d+", parts[0])
    if len(set(channels)) != 1:
        return None
    bits = int(channels[0])
    if bits % 8:
        return None
    kind = _FORMAT_KINDS[parts[1]]
    return ("%s%s%d" % ("|" if bits == 8 else "<", kind, bits // 8), len(channels))


_FORMAT_FOR_DTYPE = {"f4": ("R32", "SFLOAT"), "f2": ("R16", "SFLOAT"), "u1": ("R8", "UNORM"),
                     "u2": ("R16", "UINT"), "u4": ("R32", "UINT"), "i1": ("R8", "SINT"),
                     "i2": ("R16", "SINT"), "i4": ("R32", "SINT")}


def _format_for(dtype, int channels):
    """The format name for `channels` channels of a NumPy dtype:
    float32 -> R32.._SFLOAT, float16 -> R16.._SFLOAT, uint8 -> R8.._UNORM,
    other integers -> .._UINT / .._SINT."""
    key = "%s%d" % (dtype.kind, dtype.itemsize)
    if key not in _FORMAT_FOR_DTYPE or not 1 <= channels <= 4:
        raise ValueError("no format for %d channel(s) of %s; give format=" % (channels, dtype))
    bits, kind = _FORMAT_FOR_DTYPE[key]
    return "".join(c + bits[1:] for c in "RGBA"[:channels]) + "_" + kind


cdef _float_texels(array):
    """`array` as contiguous float32 rows of texels: (texels, height, width, channels)."""
    import numpy as np
    texels = np.ascontiguousarray(array, dtype=np.float32)
    if texels.ndim == 2:
        return texels, texels.shape[0], texels.shape[1], 1
    if texels.ndim == 3:
        return texels, texels.shape[0], texels.shape[1], texels.shape[2]
    raise ValueError("an image is (height, width) or (height, width, channels); the array has %d axes" % texels.ndim)


def save_hdr(path, array):
    """Write `array`, (height, width) or (height, width, channels) with 1
    to 4 channels of values convertible to float32, as a Radiance .hdr file:
    RGB with a shared 8-bit exponent. 1 channel is grey, 2 are red and green
    with blue zero, and alpha is dropped."""
    cdef const float[:, :, ::1] view
    texels, height, width, channels = _float_texels(array)
    view = texels.reshape(height, width, channels)
    cdef string p = str(path).encode("utf-8")
    cdef uint32_t w = width, h = height, c = channels
    with nogil:
        saveHdr(p, &view[0, 0, 0], w, h, c)


def save_exr(path, array, bint half=True):
    """Write `array`, (height, width) or (height, width, channels) with 1
    to 4 channels of values convertible to float32, as an uncompressed
    OpenEXR file of 16-bit floats, or 32-bit with half=False. 1 channel is
    written as Y, 2 as R and G, 3 as R, G and B, 4 as R, G, B and A."""
    cdef const float[:, :, ::1] view
    texels, height, width, channels = _float_texels(array)
    view = texels.reshape(height, width, channels)
    cdef string p = str(path).encode("utf-8")
    cdef uint32_t w = width, h = height, c = channels
    cdef ExrPixelType kind = ExrPixelType.Half if half else ExrPixelType.Float
    with nogil:
        saveExr(p, &view[0, 0, 0], w, h, c, kind)


cdef class GpuImage:
    """An image in device memory, made by ComputeContext.image or
    ComputeContext.upload_image: 2D, a 2D array (layers > 1; 6 x n square
    layers can be bound as a cube or cube array) or 3D (depth > 1), with one
    or more mip levels. Usable as a storage image and as a sampled image, as
    far as its format supports each on the device. Copies return once the
    copy is done; each copies one mip level, level 0 unless `mip` says
    otherwise. Level m is max(1, size >> m) in width, height and depth.
    """
    cdef CppComputeImage _image
    cdef ComputeContext _context

    def __init__(self):
        raise TypeError("use ComputeContext.image() or ComputeContext.upload_image()")

    @staticmethod
    cdef GpuImage _wrap(CppComputeImage image, ComputeContext context):
        cdef GpuImage out = GpuImage.__new__(GpuImage)
        out._image = image
        out._context = context
        return out

    @property
    def width(self):
        return self._image.width(0)

    @property
    def height(self):
        return self._image.height(0)

    @property
    def depth(self):
        return self._image.depth(0)

    @property
    def layers(self):
        return self._image.layers()

    @property
    def mips(self):
        """The number of mip levels."""
        return self._image.mipLevels()

    def level(self, uint32_t mip):
        """Mip level `mip`, to give ComputeContext.run in place of the image."""
        return GpuImageLevel(self, mip)

    def generate_mips(self):
        """Fill mip levels 1 and up from level 0, each by halving the one
        before it with linear filtering. Raises RuntimeError when the device
        cannot blit and linearly filter the format."""
        with nogil:
            self._image.generateMips()

    def save(self, path, uint32_t mip=0, uint32_t layer=0, bint half=True):
        """Write one layer (of an array) or slice (of a 3D image) at mip
        level `mip` to `path`, by its extension: .hdr, .exr (16-bit floats,
        or 32-bit with half=False) or .npy (float32, height x width x
        channels). Texels are converted to floats: UNORM to 0..1, sRGB
        decoded to linear, packed formats unpacked; integer formats raise
        ValueError."""
        cdef string p = str(path).encode("utf-8")
        cdef ExrPixelType kind = ExrPixelType.Half if half else ExrPixelType.Float
        with nogil:
            saveImage(p, self._image, mip, layer, kind)

    @property
    def format(self):
        """The pipeline JSON name of its format."""
        return self._image.format().decode("utf-8")

    @property
    def texel_size(self):
        return self._image.texelSize()

    @property
    def nbytes(self):
        """Bytes of mip level 0: width x height x depth x layers x texel_size."""
        return self._image.byteSize(0)

    def level_nbytes(self, uint32_t mip):
        """Bytes of mip level `mip`, all layers."""
        return self._image.byteSize(mip)

    @property
    def valid(self):
        """False once the context is shut down."""
        return self._image.valid()

    @property
    def shape(self):
        """level_shape(0)."""
        return self.level_shape(0)

    def level_shape(self, uint32_t mip):
        """The shape to_numpy(mip) returns: (height, width) of the level,
        preceded by its depth for a 3D image or by layers for an array,
        followed by the channels when there are more than one, or by
        texel_size bytes for a format whose channels have no single NumPy
        type."""
        if mip >= self._image.mipLevels():
            raise IndexError("mip level %d of an image with %d level(s)" % (mip, self._image.mipLevels()))
        layout = _format_layout(self.format)
        out = (self._image.height(mip), self._image.width(mip))
        if self._image.depth(0) > 1:
            out = (self._image.depth(mip),) + out
        if self.layers > 1:
            out = (self.layers,) + out
        if layout is None:
            return out + (self.texel_size,)
        return out + ((layout[1],) if layout[1] > 1 else ())

    def upload(self, data, uint32_t mip=0):
        """Copy `data` (anything with the buffer protocol and contiguous
        memory, holding exactly level_nbytes(mip) bytes, texels tightly
        packed) into mip level `mip`."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef size_t n = view.shape[0]
        cdef const unsigned char* ptr = &view[0] if n else NULL
        with nogil:
            self._image.upload(ptr, n, mip)

    def download_into(self, out, uint32_t mip=0):
        """Copy mip level `mip` into `out`, a writable object with the
        buffer protocol and contiguous memory holding level_nbytes(mip)
        bytes."""
        cdef unsigned char[::1] view = memoryview(out).cast('B')
        cdef size_t n = view.shape[0]
        cdef unsigned char* ptr = &view[0] if n else NULL
        with nogil:
            self._image.download(ptr, n, mip)

    def to_bytes(self, uint32_t mip=0):
        out = bytearray(self._image.byteSize(mip))
        self.download_into(out, mip)
        return bytes(out)

    def to_numpy(self, uint32_t mip=0):
        """Mip level `mip` as a new NumPy array shaped as level_shape(mip),
        of the format's channel type (float16 for 16-bit floats, uint8 for
        8-bit UNORM and SRGB, uint32 for packed formats, uint8 bytes for
        formats whose channels have no single type)."""
        import numpy as np
        layout = _format_layout(self.format)
        out = np.empty(self.level_shape(mip), dtype=np.uint8 if layout is None else np.dtype(layout[0]))
        self.download_into(out, mip)
        return out

    def __repr__(self):
        return "GpuImage(%dx%dx%d, layers=%d, mips=%d, %s%s)" % (
            self.width, self.height, self.depth, self.layers, self.mips, self.format,
            "" if self._image.valid() else ", released")


cdef class GpuImageLevel:
    """One mip level of a GpuImage, for ComputeContext.run: a storage image
    is bound at that level, a sampled image with only that level. Made by
    GpuImage.level()."""
    cdef readonly GpuImage image
    cdef readonly uint32_t mip

    def __init__(self, GpuImage image, uint32_t mip):
        if mip >= image.mips:
            raise IndexError("mip level %d of an image with %d level(s)" % (mip, image.mips))
        self.image = image
        self.mip = mip

    def __repr__(self):
        return "GpuImageLevel(%r, mip=%d)" % (self.image, self.mip)


cdef class ComputeKernel:
    """A compute shader made by ComputeContext.kernel. Its bindings, push
    constant size and workgroup size are read from the SPIR-V."""
    cdef CppComputeKernel _kernel
    cdef ComputeContext _context
    cdef readonly object path

    def __init__(self):
        raise TypeError("use ComputeContext.kernel()")

    @staticmethod
    cdef ComputeKernel _wrap(CppComputeKernel kernel, ComputeContext context, path):
        cdef ComputeKernel out = ComputeKernel.__new__(ComputeKernel)
        out._kernel = kernel
        out._context = context
        out.path = path
        return out

    @property
    def bindings(self):
        """What the shader declares in set 0, ascending by binding: dicts of
        "binding", "type" ("storage", "uniform", "storage_image" or
        "sampled_image"), "read_only" and "name"; images also have "dim"
        ("2d", "3d" or "cube"), "arrayed" and "format" (the storage image's
        format qualifier as a pipeline JSON format name, or None)."""
        cdef const ComputeKernelInfo* info = &self._kernel.info()
        cdef ComputeKernelBinding b
        out = []
        for i in range(info.bindings.size()):
            b = info.bindings[i]
            if b.type == ComputeBindingType.UniformBuffer:
                kind = "uniform"
            elif b.type == ComputeBindingType.StorageImage:
                kind = "storage_image"
            elif b.type == ComputeBindingType.SampledImage:
                kind = "sampled_image"
            else:
                kind = "storage"
            entry = {
                "binding": b.binding,
                "type": kind,
                "read_only": b.readOnly,
                "name": b.name.decode("utf-8"),
            }
            if kind.endswith("_image"):
                entry["dim"] = ("3d" if b.dim == ComputeBindingDim.Image3D else
                                "cube" if b.dim == ComputeBindingDim.Cube else "2d")
                entry["arrayed"] = b.arrayed
                entry["format"] = b.format.decode("utf-8") or None
            out.append(entry)
        return out

    @property
    def push_constant_size(self):
        """Bytes of push constants, 0 when the shader has none."""
        return self._kernel.info().pushConstantSize

    @property
    def local_size(self):
        """The workgroup size (x, y, z)."""
        cdef const ComputeKernelInfo* info = &self._kernel.info()
        return (info.localSize[0], info.localSize[1], info.localSize[2])

    def __repr__(self):
        return "ComputeKernel(%r, bindings=%d, local_size=%r)" % (
            str(self.path), len(self.bindings), self.local_size)


cdef class ComputeContext:
    """Runs compute shaders on buffers, synchronously.

    ComputeContext() creates its own Vulkan device, with no window;
    Engine.compute gives one on a running engine's device. Every call
    returns once the GPU has finished.

    Usage::

        import numpy as np
        import shoonyakasha as sk

        with sk.ComputeContext() as ctx:
            kernel = ctx.kernel("square.comp")      # compiled with glslc, or a .spv
            values = np.arange(1024, dtype=np.float32)
            result = np.empty_like(values)
            ctx.run(kernel, [values, result], threads=values.size)
            np.save("squares.npy", result)

    A context may be used from one thread at a time; Engine.compute from
    the engine's callbacks.
    """
    cdef shared_ptr[CppComputeContext] _ctx
    cdef object _engine
    cdef object __weakref__

    def __init__(self, bint enable_validation=False):
        """Create a Vulkan instance and a device without a window.
        enable_validation turns on the Khronos validation layer when it is
        installed."""
        cdef CppComputeContext* ctx = new CppComputeContext(enable_validation)
        self._ctx = shared_ptr[CppComputeContext](ctx)

    @staticmethod
    cdef ComputeContext _wrap(shared_ptr[CppComputeContext] ctx, engine):
        cdef ComputeContext out = ComputeContext.__new__(ComputeContext)
        out._ctx = ctx
        out._engine = engine
        return out

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        if self._engine is None:
            self.shutdown()
        return False

    def shutdown(self):
        """Free every buffer, image and kernel of this context, and its device when
        the context created it. Using them afterwards raises RuntimeError.
        For Engine.compute the engine does this itself after on_cleanup."""
        self._ctx.get().shutdown()

    @property
    def is_shutdown(self):
        return self._ctx.get().isShutdown()

    def buffer(self, size_t size):
        """A buffer of `size` bytes (at least 1) filled with zeros."""
        cdef CppComputeBuffer buf
        with nogil:
            buf = self._ctx.get().createBuffer(size)
        return GpuBuffer._wrap(buf, self)

    def upload(self, data):
        """A buffer holding a copy of `data`, anything with the buffer
        protocol and contiguous memory."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef GpuBuffer out = self.buffer(view.shape[0])
        out.upload(data)
        return out

    def image(self, uint32_t width, uint32_t height, str format="R32G32B32A32_SFLOAT",
              uint32_t depth=1, uint32_t layers=1, uint32_t mips=1):
        """An image filled with zeros. `format` is a pipeline JSON format
        name. depth > 1 makes a 3D image, layers > 1 a 2D array; not both.
        `mips` is the number of mip levels; 0 makes the full chain down to
        1x1."""
        cdef string fmt = format.encode("utf-8")
        cdef CppComputeImage image
        with nogil:
            image = self._ctx.get().createImage(fmt, width, height, depth, layers, mips)
        return GpuImage._wrap(image, self)

    def upload_image(self, array, str format=None, str kind="2d", uint32_t mips=1):
        """An image holding a copy of a NumPy array.

        `kind` "2d" takes (height, width) or (height, width, channels);
        "3d" (depth, height, width[, channels]); "array" (layers, height,
        width[, channels]). `format` defaults to the dtype's: float32 ->
        R32.._SFLOAT, float16 -> R16.._SFLOAT, uint8 -> R8.._UNORM, other
        integers -> .._UINT / .._SINT, with one channel per element of the
        last axis. With `mips` other than 1 (0 for the full chain) the array
        fills level 0 and generate_mips() the rest.
        """
        import numpy as np
        array = np.ascontiguousarray(array)
        spatial = {"2d": 2, "3d": 3, "array": 3}.get(kind)
        if spatial is None:
            raise ValueError('kind is "2d", "3d" or "array"')
        layout = _format_layout(format) if format else None
        if array.ndim == spatial + 1:
            channels = array.shape[-1]
        elif array.ndim == spatial:
            channels = 1
        else:
            raise ValueError("a %s image needs %d or %d axes, the array has %d" % (
                kind, spatial, spatial + 1, array.ndim))
        if layout is not None and array.ndim == spatial and layout[1] > 1:
            raise ValueError("%s has %d channels; the array needs a channel axis" % (format, layout[1]))
        if format is None:
            format = _format_for(array.dtype, channels)
        height, width = array.shape[spatial - 2], array.shape[spatial - 1]
        first = array.shape[0] if spatial == 3 else 1
        out = self.image(width, height, format,
                         depth=first if kind == "3d" else 1,
                         layers=first if kind == "array" else 1,
                         mips=mips)
        out.upload(array)
        if out.mips > 1:
            out.generate_mips()
        return out

    def kernel(self, path, **compile_options):
        """A kernel from a SPIR-V file, or from GLSL source (.comp), which is
        compiled first with shoonyakasha.shaders.compile (`compile_options`
        are passed to it: glslc, args, force)."""
        from pathlib import Path
        path = Path(path)
        if path.suffix != ".spv":
            from . import shaders
            path = shaders.compile(path, **compile_options)
        return ComputeKernel._wrap(self._ctx.get().loadKernel(str(path).encode("utf-8")), self, path)

    def run(self, ComputeKernel kernel, bindings, groups=None, threads=None, push=None):
        """Dispatch `kernel` and wait for it.

        `bindings` holds one entry per binding of the kernel, in the order
        of kernel.bindings. A buffer binding takes a GpuBuffer, or an array
        (anything with the buffer protocol and contiguous memory), which is
        copied to a buffer before the dispatch and, when it is writable and
        the binding is not read-only, copied back into after it. An image
        binding takes a GpuImage, bound as the shader's image type declares:
        2D needs depth 1 and layers 1, 2D array depth 1, 3D depth > 1, cube
        6 square layers, cube array 6 x n. A storage image is bound at mip
        level 0, a sampled image with all its levels; image.level(mip) binds
        that one level instead. Sampled images are read with linear
        filtering, between texels and mip levels, where the format allows
        it, nearest otherwise, clamped to the edge.

        Give either `groups`, the number of workgroups, or `threads`, the
        number of invocations, which is divided by the kernel's local size
        and rounded up; each is an int or up to three ints.

        `push` (anything with the buffer protocol, such as bytes from
        struct.pack or a NumPy array) is copied into the push constants; it
        may be shorter than kernel.push_constant_size, the rest is zero.
        """
        if (groups is None) == (threads is None):
            raise ValueError("give either groups or threads")
        info = kernel.bindings
        bindings = list(bindings)
        if len(bindings) != len(info):
            raise ValueError("the kernel has %d binding(s), %d were given" % (len(info), len(bindings)))

        cdef vector[CppComputeBinding] resources
        copy_back = []
        cdef GpuBuffer buf
        cdef GpuImage img
        for item, binding in zip(bindings, info):
            if isinstance(item, GpuImage):
                img = item
                resources.push_back(CppComputeBinding(img._image))
                continue
            if isinstance(item, GpuImageLevel):
                img = (<GpuImageLevel>item).image
                resources.push_back(CppComputeBinding(img._image, (<GpuImageLevel>item).mip))
                continue
            if binding["type"].endswith("_image"):
                raise TypeError("binding %d ('%s') is an image: give a GpuImage (ctx.upload_image)" % (
                    binding["binding"], binding["name"]))
            if isinstance(item, GpuBuffer):
                buf = item
            else:
                buf = self.upload(item)
                view = memoryview(item)
                if not view.readonly and not binding["read_only"]:
                    copy_back.append((buf, item))
            resources.push_back(CppComputeBinding(buf._buf))

        cdef ComputeUint3 count
        cdef uint32_t local
        if groups is not None:
            count = _uint3(groups, "groups")
        else:
            count = _uint3(threads, "threads")
            size = kernel.local_size
            for i in range(3):
                local = size[i]
                count[i] = (count[i] + local - 1) // local

        cdef const unsigned char[::1] constants
        cdef const void* push_ptr = NULL
        cdef size_t push_size = 0
        if push is not None:
            constants = memoryview(push).cast('B')
            push_size = constants.shape[0]
            if push_size:
                push_ptr = &constants[0]

        with nogil:
            self._ctx.get().run(kernel._kernel, resources, count, push_ptr, push_size)

        for buf, item in copy_back:
            buf.download_into(item)

    def __repr__(self):
        return "ComputeContext(%s%s)" % ("engine" if self._engine is not None else "headless",
                                         ", shut down" if self.is_shutdown else "")


# ═══════════════════════════════════════════════════════════════
# Engine — Python wrapper for EngineAPI (the main entry point)
# ═══════════════════════════════════════════════════════════════

import atexit as _atexit
import weakref as _weakref

# Engines not yet freed. At interpreter exit each one's GPU resources and
# device are destroyed, whether or not the script still references it.
_live_engines = _weakref.WeakSet()


def _release_engines():
    for engine in list(_live_engines):
        (<Engine>engine)._release()


_atexit.register(_release_engines)


cdef class BufferStream:
    """Bytes for a pipeline buffer, written from any thread: Engine.stream(name).

    write() queues the bytes and returns; once a frame, before on_update,
    the engine applies the queued writes as set_buffer does, oldest first. A
    write that covers queued ones replaces them, so a producer faster than
    the frame rate leaves only its newest data. A refused write (one that
    does not fit the buffer, for instance) cannot raise in the producer's
    thread: `error` holds the reason.
    """
    cdef shared_ptr[CppBufferStream] _stream

    def __init__(self):
        raise TypeError("use Engine.stream(name)")

    @staticmethod
    cdef BufferStream _wrap(shared_ptr[CppBufferStream] stream):
        cdef BufferStream out = BufferStream.__new__(BufferStream)
        out._stream = stream
        return out

    @property
    def name(self):
        return self._stream.get().name().decode("utf-8")

    def write(self, data, size_t offset=0):
        """Queue `data` (anything with the buffer protocol and contiguous
        memory, such as a NumPy array or bytes) for byte `offset` of the
        buffer. Safe from any thread; does not hold the GIL while copying."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef size_t n = view.shape[0]
        if n == 0:
            return
        with nogil:
            self._stream.get().write(&view[0], n, offset)

    @property
    def pending_bytes(self):
        """Bytes queued and not yet applied."""
        return self._stream.get().pendingBytes()

    @property
    def applied(self):
        """Writes applied so far."""
        return self._stream.get().applied()

    @property
    def error(self):
        """Why the last write applied was refused; '' when it was taken."""
        return self._stream.get().error().decode("utf-8")

    def __repr__(self):
        return "BufferStream(%r, applied=%d, pending_bytes=%d)" % (self.name, self.applied, self.pending_bytes)


cdef class Engine:
    """Main engine entry point.

    Usage::

        engine = Engine(title="My Game", width=1920, height=1080,
                        pipeline_json_path="pipeline.json")

        def on_init():
            engine.create_camera((0, 5, 15))

        engine.set_on_init(on_init)
        engine.run()
    """

    cdef CppEngineAPI* _ptr

    # Keep Python references to sub-API wrappers to avoid GC
    cdef Scene _scene_wrapper
    cdef Input _input_wrapper
    cdef Physics _physics_wrapper
    cdef Ecs _ecs_wrapper
    cdef UI _ui_wrapper

    # Keep Python references to callbacks to prevent GC
    cdef list _callback_refs
    cdef object __weakref__

    def __cinit__(self):
        self._ptr = NULL
        self._callback_refs = []

    def __init__(self, str title="Shoonyakasha Application",
                 int width=1600, int height=900,
                 str log_file="application.log", int log_level=1,
                 str hdr_environment_path="",
                 str pipeline_json_path="",
                 int max_frames_in_flight=2,
                 dict render_graph_parameters=None,
                 environment_color=(0.25, 0.28, 0.33),
                 enable_validation=None,
                 bint ibl_cache=True):
        """Create engine with configuration.

        Args:
            title: Window title
            width: Window width
            height: Window height
            log_file: Log file path
            log_level: 0=Debug, 1=Info, 2=Warning, 3=Error
            hdr_environment_path: HDR environment map, or an .ibl file made
                by bake_environment. Empty: a pipeline that samples IBL gets a
                uniform environment_color instead.
            pipeline_json_path: JSON render graph: a pipeline JSON file, a
                directory holding pipeline.json, or an http(s) URL, which
                shoonyakasha.pipeline.fetch downloads first. Empty: the
                default pipeline, shoonyakasha.pipeline.DEFAULT.
            max_frames_in_flight: Vulkan frames in flight
            render_graph_parameters: Dict of str→int for SSBO sizing etc.
            environment_color: (r, g, b) of the uniform environment used
                when there is no HDR map.
            enable_validation: Vulkan validation layers. None follows the
                build: off in the released package, where they would slow
                every draw. SHOONYAKASHA_VALIDATION=1 or 0 overrides it.
            ibl_cache: save an HDR map's lighting beside it (sky.hdr.ibl)
                and load it from there while the map is unchanged.
        """
        from . import pipeline as _pipeline
        if not pipeline_json_path:
            pipeline_json_path = str(_pipeline.DEFAULT)
        elif _pipeline._is_url(pipeline_json_path):
            pipeline_json_path = str(_pipeline.fetch(pipeline_json_path))
        # Where the engine finds the shaders the default pipeline ships for
        # its own use (UI, skinning), whatever pipeline this engine runs.
        import os as _os
        _os.environ.setdefault("SHOONYAKASHA_DEFAULT_PIPELINE", str(_pipeline.DEFAULT))
        cdef EngineConfig cfg
        cfg.width = width
        cfg.height = height
        cfg.title = title.encode('utf-8')
        cfg.logFile = log_file.encode('utf-8')
        cfg.logLevel = log_level
        cfg.hdrEnvironmentPath = hdr_environment_path.encode('utf-8')
        for i in range(3):
            cfg.uniformEnvironmentColor[i] = float(environment_color[i])
        cfg.iblCache = ibl_cache
        cfg.pipelineJsonPath = pipeline_json_path.encode('utf-8')
        cfg.maxFramesInFlight = max_frames_in_flight
        if enable_validation is not None:
            cfg.enableValidation = bool(enable_validation)

        if render_graph_parameters:
            for k, v in render_graph_parameters.items():
                cfg.renderGraphParameters.push_back(
                    pair[string, uint32_t](k.encode('utf-8'), <uint32_t>v))

        self._ptr = new CppEngineAPI(cfg)
        _live_engines.add(self)

    def __dealloc__(self):
        self._release()

    cdef void _release(self):
        if self._ptr != NULL:
            del self._ptr
            self._ptr = NULL

    # ── Lifecycle ─────────────────────────────────────────────

    def run(self):
        """Run the engine. Blocks until the window is closed.

        The GIL is released during the engine loop so Python callbacks
        can be invoked from the engine thread.
        """
        with nogil:
            self._ptr.run()

    # ── Callback Registration ─────────────────────────────────

    def request_quit(self):
        """Exit after this frame, finalizing recording and running cleanup."""
        self._ptr.requestQuit()

    @property
    def framebuffer_size(self):
        """Actual framebuffer pixels, or (0, 0) before initialization."""
        cdef tuple size = _vec2_to_tuple(self._ptr.getFramebufferSize())
        return (int(size[0]), int(size[1]))

    def set_on_init(self, callback):
        """Set initialization callback: callback()."""
        self._callback_refs.append(callback)
        self._ptr.setOnInit(make_void_callback(<PyObject*>callback))

    def set_on_post_init(self, callback):
        """Set post-initialization callback: callback()."""
        self._callback_refs.append(callback)
        self._ptr.setOnPostInit(make_void_callback(<PyObject*>callback))

    def set_on_update(self, callback):
        """Set per-frame update callback: callback(dt: float)."""
        self._callback_refs.append(callback)
        self._ptr.setOnUpdate(make_update_callback(<PyObject*>callback))

    def set_on_pre_render(self, callback):
        """Set pre-render callback: callback(dt: float)."""
        self._callback_refs.append(callback)
        self._ptr.setOnPreRender(make_update_callback(<PyObject*>callback))

    def set_on_post_render(self, callback):
        """Set post-render callback: callback()."""
        self._callback_refs.append(callback)
        self._ptr.setOnPostRender(make_void_callback(<PyObject*>callback))

    def set_on_key_pressed(self, callback):
        """Set key-press callback: callback(key_code: int)."""
        self._callback_refs.append(callback)
        self._ptr.setOnKeyPressed(make_key_callback(<PyObject*>callback))

    def set_on_resize(self, callback):
        """Set window resize callback: callback(width: int, height: int)."""
        self._callback_refs.append(callback)
        self._ptr.setOnResize(make_resize_callback(<PyObject*>callback))

    def set_on_cleanup(self, callback):
        """Set cleanup callback: callback()."""
        self._callback_refs.append(callback)
        self._ptr.setOnCleanup(make_void_callback(<PyObject*>callback))

    # ── Sub-API Access ────────────────────────────────────────

    @property
    def scene(self):
        """Access scene/entity management API."""
        if self._scene_wrapper is None:
            self._scene_wrapper = Scene.__new__(Scene)
            self._scene_wrapper._ptr = &self._ptr.getScene()
            self._scene_wrapper._owned = False
            self._scene_wrapper._owner = self
        return self._scene_wrapper

    @property
    def input(self):
        """Access input polling/events API."""
        if self._input_wrapper is None:
            self._input_wrapper = Input.__new__(Input)
            self._input_wrapper._ptr = &self._ptr.getInput()
            self._input_wrapper._owned = False
            self._input_wrapper._owner = self
        return self._input_wrapper

    @property
    def physics(self):
        """Access physics simulation API."""
        if self._physics_wrapper is None:
            self._physics_wrapper = Physics.__new__(Physics)
            self._physics_wrapper._ptr = &self._ptr.getPhysics()
            self._physics_wrapper._owned = False
            self._physics_wrapper._owner = self
        return self._physics_wrapper

    @property
    def ecs(self):
        """Access low-level ECS API (custom components/systems)."""
        if self._ecs_wrapper is None:
            self._ecs_wrapper = Ecs.__new__(Ecs)
            self._ecs_wrapper._ptr = &self._ptr.getEcs()
            self._ecs_wrapper._owned = False
            self._ecs_wrapper._owner = self
        return self._ecs_wrapper

    @property
    def ui(self):
        """Access the canvas UI API. Available from on_init on."""
        if self._ui_wrapper is None:
            self._ui_wrapper = UI.__new__(UI)
            self._ui_wrapper._ptr = &self._ptr.getUI()
            self._ui_wrapper._owned = False
            self._ui_wrapper._owner = self
        return self._ui_wrapper

    # ── Convenience Helpers ───────────────────────────────────

    def create_camera(self, pos, float fov=60.0, float speed=8.0,
                      float near_plane=0.1, float far_plane=1000.0):
        """Create a camera entity.

        Args:
            pos: Position as (x, y, z) tuple
            fov: Field of view in degrees
            speed: Movement speed
            near_plane: Near clipping plane
            far_plane: Far clipping plane

        Returns:
            Entity handle (int)
        """
        return <uint32_t>self._ptr.createCamera(
            _tuple_to_vec3(pos), fov, speed, near_plane, far_plane)

    # ── Frame capture ────────────────────────────────────────
    #
    # Both capture the frame that was last presented, so what lands on disk is
    # what was on screen. Readback is synchronous, so recording costs frame rate.

    def capture_screenshot(self, str path):
        """Write the last presented frame to disk.

        Format follows the extension: .png, .jpg, .bmp, .tga, .hdr.
        Returns True on success.
        """
        return self._ptr.captureScreenshot(path.encode("utf-8"))

    def set_environment(self, str path):
        """Light the scene with an HDR environment map, or an .ibl file made by
        bake_environment. An HDR map's lighting is saved beside it (sky.hdr.ibl,
        unless ibl_cache=False) and loaded from there while the map is unchanged.
        From on_init on. Returns False, keeping the current environment, when it
        cannot be loaded."""
        return self._ptr.setEnvironment(path.encode("utf-8"))

    def set_environment_color(self, color):
        """An environment of one colour, (r, g, b) linear, in every direction."""
        return self._ptr.setEnvironmentColor(_tuple_to_vec3(color))

    def bake_environment(self, str hdr_path, str output_path):
        """Write the lighting of an HDR map to an .ibl file to ship with an
        application, without changing the scene's environment. From on_init on.
        Returns True on success."""
        return self._ptr.bakeEnvironment(hdr_path.encode("utf-8"), output_path.encode("utf-8"))

    def start_recording(self, str path, int fps=30, int quality=18,
                        str codec="libx264", str ffmpeg_path="", bint fixed_step=False):
        """Record every presented frame to a video file.

        The container follows the extension -- .mkv, .mp4, .webm. Needs ffmpeg
        on PATH or in $FFMPEG; see shoonyakasha.video_recording_available().

        quality is x264's CRF: 0 lossless, 18 visually lossless, 51 worst.
        With fixed_step, every frame passes exactly 1/fps seconds as its dt
        until the recording stops, so the video plays motion at its own
        speed however fast the frames render.
        """
        cdef RecordingOptions opts
        opts.fps = fps
        opts.quality = quality
        opts.codec = codec.encode("utf-8")
        opts.ffmpegPath = ffmpeg_path.encode("utf-8")
        opts.fixedStep = fixed_step
        return self._ptr.startRecording(path.encode("utf-8"), opts)

    def stop_recording(self):
        """Finish the recording and finalise the file.

        Called automatically at shutdown; a recording that is never stopped may
        produce a file that will not play.
        """
        return self._ptr.stopRecording()

    @property
    def recording_paused(self):
        """While True, presented frames are not written to the recording.

        For rendering several frames per recorded one, such as sub-frames
        averaged into motion blur. Set it before the frame renders, from the
        update callback.
        """
        return self._ptr.isRecordingPaused()

    @recording_paused.setter
    def recording_paused(self, bint value):
        self._ptr.setRecordingPaused(value)

    @property
    def render_scale(self):
        """The scene renders at the window's size times this, scaled to the
        window when it is shown.

        2 supersamples: four samples per pixel, for crisper captures at four
        times the cost. Between 0.25 and 4; a change recompiles the graph
        before the next frame. Pipelines read the render size as
        scene.screen.renderResolution, the window's as
        scene.screen.resolution.
        """
        return self._ptr.getRenderScale()

    @render_scale.setter
    def render_scale(self, float value):
        self._ptr.setRenderScale(value)

    def reload_pipeline(self):
        """Load the pipeline JSON and the SPIR-V its passes name again, and
        rebuild the render graph, waiting for the GPU.

        Compile changed GLSL first, with shoonyakasha.shaders.compile_dir.
        Parameters, set_custom_* values, set_buffer data, pipeline images and
        passes switched with set_pass_enabled or apply_pipeline_preset stay.
        Call it from on_update, an ECS system, a UI callback or
        on_post_render, not from on_init or on_pre_render.

        Raises RuntimeError with the reason when the JSON does not load or the
        graph does not compile; the previous graph is then compiled again.
        """
        cdef string error = self._ptr.reloadPipeline()
        if not error.empty():
            raise RuntimeError(error.decode("utf-8"))

    @property
    def is_recording(self):
        """Whether a recording is in progress.

        A property, like every other no-argument accessor on this class —
        having this one be a method while recorded_frame_count beside it was
        a property was a trap worth removing.
        """
        return self._ptr.isRecording()

    @property
    def recorded_frame_count(self):
        """Frames written to the current or most recent recording."""
        return self._ptr.getRecordedFrameCount()

    def load_gltf_scene(self, str path, **kwargs):
        """Load a glTF scene.

        Args:
            path: Path to .gltf or .glb file
            **kwargs: GltfOptions fields (load_textures, load_materials, etc.)

        Returns:
            GltfResult with success, entities list, and statistics
        """
        cdef GltfOptions opts
        if 'load_textures' in kwargs:
            opts.loadTextures = kwargs['load_textures']
        if 'load_materials' in kwargs:
            opts.loadMaterials = kwargs['load_materials']
        if 'create_entities' in kwargs:
            opts.createEntities = kwargs['create_entities']
        if 'load_skins' in kwargs:
            opts.loadSkins = kwargs['load_skins']
        if 'load_animations' in kwargs:
            opts.loadAnimations = kwargs['load_animations']
        if 'load_lights' in kwargs:
            opts.loadLights = kwargs['load_lights']
        if 'stream_textures' in kwargs:
            opts.streamTextures = kwargs['stream_textures']
        if 'flatten_hierarchy' in kwargs:
            opts.flattenHierarchy = kwargs['flatten_hierarchy']
        if 'max_texture_size' in kwargs:
            opts.maxTextureSize = kwargs['max_texture_size']
        if 'generate_mipmaps' in kwargs:
            opts.generateMipmaps = kwargs['generate_mipmaps']
        if 'srgb_albedo' in kwargs:
            opts.srgbAlbedo = kwargs['srgb_albedo']
        if 'compress_textures' in kwargs:
            opts.compressTextures = kwargs['compress_textures']
        if 'lods' in kwargs:
            opts.generateLods = kwargs['lods']
        if 'name_prefix' in kwargs:
            opts.namePrefix = kwargs['name_prefix'].encode('utf-8')

        cdef string cpp_path = path.encode('utf-8')
        cdef CppGltfResult result = self._ptr.loadGltfScene(cpp_path, opts)
        return _wrap_gltf_result(result)

    def get_texture_streaming_progress(self):
        """(finished, requested): textures loaded with stream_textures=True that
        have been uploaded (or failed to decode), and all requested so far."""
        cdef pair[size_t, size_t] progress = self._ptr.getTextureStreamingProgress()
        return (progress.first, progress.second)

    def set_texture_upload_budget(self, double megabytes):
        """Megabytes of streamed textures uploaded per frame at most (at least
        one texture a frame while any are waiting); 16 by default."""
        self._ptr.setTextureUploadBudget(<size_t>(max(megabytes, 0.0) * 1024 * 1024))

    def set_texture_memory_budget(self, double megabytes):
        """Megabytes streamed textures may take on the GPU: each is kept at the
        resolution the main camera's view needs, as far as the budget allows.
        0, the default, keeps every one at full size. Textures streamed while a
        budget is set start at their mip of at most 128 texels."""
        self._ptr.setTextureMemoryBudget(<uint64_t>(max(megabytes, 0.0) * 1024 * 1024))

    def get_texture_residency(self):
        """Residency of streamed textures: a dict with textures, resident_mb
        (their mips on the GPU), needed_mb (at the mips last frame's view
        needs), budget_mb, waiting (textures waiting for finer mips) and
        held_back (textures the budget holds coarser than needed)."""
        cdef TextureResidencyStats s = self._ptr.getTextureResidency()
        return {"textures": s.textures, "resident_mb": s.residentBytes / 1048576.0,
                "needed_mb": s.neededBytes / 1048576.0, "budget_mb": s.budget / 1048576.0,
                "waiting": s.waiting, "held_back": s.heldBack}

    def create_directional_light(self, direction, color=(1.0, 1.0, 1.0),
                                  float intensity=2.0):
        """Create a directional light entity.

        Args:
            direction: Direction as (x, y, z) tuple
            color: Color as (r, g, b) tuple (default white)
            intensity: Light intensity

        Returns:
            Entity handle (int)
        """
        return <uint32_t>self._ptr.createDirectionalLight(
            _tuple_to_vec3(direction), _tuple_to_vec3(color), intensity)

    def create_point_light(self, pos, color=(1.0, 1.0, 1.0),
                            float intensity=5.0, float range=15.0):
        """Create a point light entity.

        Args:
            pos: Position as (x, y, z) tuple
            color: Color as (r, g, b) tuple (default white)
            intensity: Light intensity
            range: Light range

        Returns:
            Entity handle (int)
        """
        return <uint32_t>self._ptr.createPointLight(
            _tuple_to_vec3(pos), _tuple_to_vec3(color), intensity, range)

    @staticmethod
    def _flat(values):
        """A sequence of vectors (a list of tuples, an N x k array) or a flat
        sequence, as one flat list."""
        if values is None:
            return []
        if hasattr(values, "tolist"):          # NumPy arrays, array.array
            values = values.tolist()
        values = list(values)
        if values and hasattr(values[0], "__len__"):
            return [x for v in values for x in v]
        return values

    def create_mesh(self, positions, indices=None, normals=None, uvs=None, colors=None, str name="", tangents=None):
        """Create an entity drawing triangles from arrays.

        Args:
            positions: (x, y, z) per vertex: a list of tuples, an N x 3
                array, or flat
            indices: three per triangle, counter-clockwise from the front;
                None draws every three vertices as a triangle
            normals: (x, y, z) per vertex; None gives each vertex the
                area-weighted normal of its triangles
            uvs: (u, v) per vertex, or None
            colors: (r, g, b) per vertex, or None for white
            name: the entity's name
            tangents: (x, y, z, w) per vertex: the direction of +u, and w
                = 1 or -1, the bitangent's side (bitangent = w * cross(normal,
                tangent)). None generates MikkTSpace tangents when there are
                uvs; normal maps use them.

        The entity has an identity transform, the default material (white,
        metallic 0, roughness 0.5) and casts and receives shadows; set_position
        and the set_material_* calls change them. It renders, is culled and is
        ray traced like a loaded glTF mesh.

        Returns:
            The entity handle. Raises ValueError when the arrays do not fit.
        """
        cdef vector[float] pos = Engine._flat(positions)
        cdef vector[uint32_t] idx = [int(i) for i in Engine._flat(indices)]
        cdef vector[float] nrm = Engine._flat(normals)
        cdef vector[float] tex = Engine._flat(uvs)
        cdef vector[float] col = Engine._flat(colors)
        cdef vector[float] tan = Engine._flat(tangents)
        cdef string error
        cdef EntityHandle e = self._ptr.createMesh(pos, idx, nrm, tex, col, tan, name.encode("utf-8"), error)
        if not error.empty():
            raise ValueError(error.decode("utf-8"))
        return <uint32_t>e

    def set_mesh(self, uint32_t entity, positions, indices=None, normals=None, uvs=None, colors=None, tangents=None):
        """Replace the triangles of a mesh entity, taking them as create_mesh
        does; its material and transform stay. The old geometry is freed once
        no frame in flight draws it. Uploading waits for the GPU. Raises
        ValueError when the arrays do not fit or the entity has no mesh."""
        cdef vector[float] pos = Engine._flat(positions)
        cdef vector[uint32_t] idx = [int(i) for i in Engine._flat(indices)]
        cdef vector[float] nrm = Engine._flat(normals)
        cdef vector[float] tex = Engine._flat(uvs)
        cdef vector[float] col = Engine._flat(colors)
        cdef vector[float] tan = Engine._flat(tangents)
        cdef string error = self._ptr.setMesh(entity, pos, idx, nrm, tex, col, tan)
        if not error.empty():
            raise ValueError(error.decode("utf-8"))

    def create_point_lights(self, positions, colors=(1.0, 1.0, 1.0), intensity=5.0, range=15.0):
        """Create many point lights in one call.

        Args:
            positions: a sequence of (x, y, z), one per light (a list of
                tuples, or an N x 3 array)
            colors: one (r, g, b) for every light, or a sequence of one per
                light
            intensity: one intensity for every light, or a sequence of one
                per light
            range: one range for every light, or a sequence of one per light

        Returns:
            The lights' entity handles (ints), in the order of `positions`.
        """
        cdef vector[float] pos, col, inten, rng
        for p in positions:
            pos.push_back(<float>p[0])
            pos.push_back(<float>p[1])
            pos.push_back(<float>p[2])
        def one_or_each(value, width):
            try:
                first = value[0]
            except (TypeError, IndexError):
                return [value] if width == 1 else list(value)
            if width == 1:
                return list(value)
            try:
                first[0]
            except (TypeError, IndexError):
                return list(value[:width])
            return [c for v in value for c in v[:width]]
        for v in one_or_each(colors, 3):
            col.push_back(<float>v)
        for v in one_or_each(intensity, 1):
            inten.push_back(<float>v)
        for v in one_or_each(range, 1):
            rng.push_back(<float>v)
        cdef vector[EntityHandle] made = self._ptr.createPointLights(pos, col, inten, rng)
        return [<uint32_t>e for e in made]

    def create_sprite(self, world_pos, str texture_path, size=(1.0, 1.0),
                       tint=(1.0, 1.0, 1.0, 1.0)):
        """Create a world-space sprite (billboard quad in 3D world coordinates).

        Args:
            world_pos: Position as (x, y, z) tuple
            texture_path: Path to the sprite's image file
            size: Sprite size in world units, as (width, height)
            tint: Tint/multiply color as (r, g, b, a)

        Returns:
            Entity handle (int)
        """
        cdef string cpp_path = texture_path.encode('utf-8')
        return <uint32_t>self._ptr.createSprite(
            _tuple_to_vec3(world_pos), cpp_path, _tuple_to_vec2(size), _tuple_to_vec4(tint))

    def create_ui_panel(self, int anchor, offset_pixels, size_pixels,
                         str texture_path="", color=(1.0, 1.0, 1.0, 1.0)):
        """Create a screen-space UI panel anchored to a viewport corner/edge/center.

        Args:
            anchor: One of the UI_ANCHOR_* constants
            offset_pixels: Offset from the anchor to the panel's center, as (x, y)
            size_pixels: Panel size in pixels, as (width, height)
            texture_path: Path to an image file, or "" for a flat-colored panel
            color: Tint/fill color as (r, g, b, a)

        Returns:
            Entity handle (int)
        """
        cdef string cpp_path = texture_path.encode('utf-8')
        return <uint32_t>self._ptr.createUIPanel(
            <UIAnchor>anchor, _tuple_to_vec2(offset_pixels), _tuple_to_vec2(size_pixels),
            cpp_path, _tuple_to_vec4(color))

    def create_text(self, str text, int anchor, offset_pixels, str font_path,
                     float font_size=24.0, color=(1.0, 1.0, 1.0, 1.0)):
        """Create a screen-space text label anchored to a viewport corner/edge/center.

        Args:
            text: The label's text (ASCII 32-126 supported)
            anchor: One of the UI_ANCHOR_* constants
            offset_pixels: Offset from the anchor to the label's reference point, as (x, y)
            font_path: Path to a .ttf/.otf font file
            font_size: Baked glyph pixel height
            color: Text color as (r, g, b, a)

        Returns:
            Entity handle (int)
        """
        cdef string cpp_text = text.encode('utf-8')
        cdef string cpp_font = font_path.encode('utf-8')
        return <uint32_t>self._ptr.createText(
            cpp_text, <UIAnchor>anchor, _tuple_to_vec2(offset_pixels), cpp_font,
            font_size, _tuple_to_vec4(color))

    @property
    def camera_entity(self):
        """Get the camera entity handle."""
        return <uint32_t>self._ptr.getCameraEntity()

    @property
    def delta_time(self):
        """Get frame delta time in seconds."""
        return self._ptr.getDeltaTime()

    # ── Scene Context Custom Values ───────────────────────────

    def set_custom_float(self, str key, float value):
        """Set custom float for shader uniforms (dot-path key)."""
        self._ptr.setCustomFloat(key.encode('utf-8'), value)

    def set_custom_vec2(self, str key, value):
        """Set custom vec2 for shader uniforms."""
        self._ptr.setCustomVec2(key.encode('utf-8'), _tuple_to_vec2(value))

    def set_custom_vec3(self, str key, value):
        """Set custom vec3 for shader uniforms."""
        self._ptr.setCustomVec3(key.encode('utf-8'), _tuple_to_vec3(value))

    def set_custom_vec4(self, str key, value):
        """Set custom vec4 for shader uniforms."""
        self._ptr.setCustomVec4(key.encode('utf-8'), _tuple_to_vec4(value))

    def set_custom_mat4(self, str key, value):
        """Set custom mat4 for shader uniforms.

        `value` is four columns of four floats (column-major), the shape
        Scene.get_world_matrix returns.
        """
        self._ptr.setCustomMat4(key.encode('utf-8'), _tuple_to_mat4(value))

    def set_custom_uint(self, str key, uint32_t value):
        """Set custom uint for shader uniforms."""
        self._ptr.setCustomUint(key.encode('utf-8'), value)

    # Arrays: key[0] .. key[n-1], for buffer fields with arrayCount whose
    # source ends in "[i]" (scene.custom.<key>[i]). Elements left from an
    # earlier, longer array are removed.

    def set_custom_float_array(self, str key, values):
        """Set custom floats key[0] .. key[n-1] from a sequence."""
        cdef vector[float] v
        for x in values:
            v.push_back(<float>x)
        self._ptr.setCustomFloatArray(key.encode('utf-8'), v)

    def set_custom_vec2_array(self, str key, values):
        """Set custom vec2s key[0] .. key[n-1] from a sequence of pairs."""
        cdef vector[vec2] v
        for x in values:
            v.push_back(_tuple_to_vec2(x))
        self._ptr.setCustomVec2Array(key.encode('utf-8'), v)

    def set_custom_vec3_array(self, str key, values):
        """Set custom vec3s key[0] .. key[n-1] from a sequence of triples."""
        cdef vector[vec3] v
        for x in values:
            v.push_back(_tuple_to_vec3(x))
        self._ptr.setCustomVec3Array(key.encode('utf-8'), v)

    def set_custom_vec4_array(self, str key, values):
        """Set custom vec4s key[0] .. key[n-1] from a sequence of 4-tuples."""
        cdef vector[vec4] v
        for x in values:
            v.push_back(_tuple_to_vec4(x))
        self._ptr.setCustomVec4Array(key.encode('utf-8'), v)

    def set_custom_mat4_array(self, str key, values):
        """Set custom mat4s key[0] .. key[n-1], each four columns of four floats."""
        cdef vector[mat4] v
        for x in values:
            v.push_back(_tuple_to_mat4(x))
        self._ptr.setCustomMat4Array(key.encode('utf-8'), v)

    def set_custom_uint_array(self, str key, values):
        """Set custom uints key[0] .. key[n-1] from a sequence."""
        cdef vector[uint32_t] v
        for x in values:
            v.push_back(<uint32_t>x)
        self._ptr.setCustomUintArray(key.encode('utf-8'), v)

    def set_buffer(self, str name, data, size_t offset=0):
        """Write bytes into the buffer of the pipeline's bufferLayouts entry
        `name`: a uniform buffer, or a storage buffer with an elementCount.

        `data` is anything with the buffer protocol and contiguous memory:
        bytes, bytearray, array.array or a NumPy array, written as its raw
        bytes at byte `offset`. The bytes stay until set again. A uniform
        buffer gets them in every frame, after its dot-path fields; a
        storage buffer gets the changed bytes copied in before the next
        frame's passes. May be called from on_init, before the pipeline is
        loaded. Raises ValueError when there is no such buffer or the bytes
        do not fit it.
        """
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        cdef string error
        if view.shape[0] == 0:
            return
        error = self._ptr.setBuffer(name.encode('utf-8'), &view[0], view.shape[0], offset)
        if not error.empty():
            raise ValueError(error.decode('utf-8'))

    def set_pipeline_image_data(self, str name, data, uint32_t width=0, uint32_t height=0,
                                str format="R8G8B8A8_SRGB", bint mipmaps=False):
        """Make the image the pipeline's "externalImage": name bindings sample
        from texels in memory instead of a file.

        `data` is anything with the buffer protocol and contiguous memory,
        rows of tightly packed texels with the top row first: a NumPy array
        shaped (height, width, channels) or (height, width), whose shape gives
        the size, or bytes with `width` and `height`. `format` is a pipeline
        JSON format name; the bytes must be width x height texels of it.
        With `mipmaps` the image gets a full mip chain made from the texels,
        and its own sampler filters between the levels (a binding's
        autoBindSampler needs a maxLod above 0 to reach them); the device
        must be able to blit the format with linear filtering.
        The engine keeps the image until a later one replaces it. Uploading
        waits for the GPU, so this suits images that change now and then.
        May be called from on_init. Raises ValueError when the bytes or the
        format do not fit.
        """
        cdef const unsigned char[::1] view
        cdef string error
        mv = memoryview(data)
        if width == 0 or height == 0:
            if mv.ndim < 2:
                raise ValueError("give width and height, or data shaped (height, width[, channels])")
            height, width = mv.shape[0], mv.shape[1]
        view = mv.cast('B')
        if view.shape[0] == 0:
            raise ValueError("the image has no texels")
        error = self._ptr.setPipelineImageData(name.encode('utf-8'), &view[0], view.shape[0],
                                               width, height, format.encode('utf-8'), mipmaps)
        if not error.empty():
            raise ValueError(error.decode('utf-8'))

    def stream(self, str name):
        """The BufferStream into bufferLayouts entry `name`, a buffer
        set_buffer accepts: the same stream each call for a name. Its write()
        may be called from any thread, before or while the engine runs."""
        return BufferStream._wrap(self._ptr.stream(name.encode("utf-8")))

    def request_readback(self, str name):
        """Copy the buffer layout or image resource `name` back at the end
        of the next frame, whatever the readback frequency its pipeline JSON
        declares. Returns False when it declares no readback.
        """
        return self._ptr.requestReadback(name.encode('utf-8'))

    def get_readback(self, str name):
        """The most recent completed readback of `name`, or None.

        A dict with "data" (bytes), "frame" (the frame copied, from 0) and
        for an image "width", "height" and "format" (its pipeline JSON format
        name): mip 0, layer 0, rows of tightly packed texels, the depth of a
        depth format; for a buffer "element_count" and "element_stride".
        Results arrive as many frames after the copy as there are frames in
        flight.
        """
        cdef Readback r
        if not self._ptr.getReadback(name.encode('utf-8'), r):
            return None
        out = {"data": (<const char*>r.data)[:r.size], "frame": r.frame}
        if r.image:
            out["width"] = r.width
            out["height"] = r.height
            out["format"] = r.format.decode('utf-8')
        else:
            out["element_count"] = r.elementCount
            out["element_stride"] = r.elementStride
        return out

    @property
    def compute(self):
        """A ComputeContext on the engine's device, for running compute
        shaders on buffers and images synchronously from the engine's callbacks (from
        on_init on). It is shut down after on_cleanup, and its buffers,
        images and kernels with it. Raises RuntimeError before the device exists.
        """
        cdef shared_ptr[CppComputeContext] ctx = self._ptr.compute()
        if not ctx:
            raise RuntimeError("the engine has no device yet: use engine.compute from on_init on")
        return ComputeContext._wrap(ctx, self)

    def set_sun_shadows(self, uint32_t cascades=4, float max_distance=60.0,
                        float split_lambda=0.75, uint32_t resolution=2048,
                        float caster_extension=50.0, uint32_t sun=0):
        """Configure a sun's shadow cascades.

        `sun` 0 is the first directional light, in light order, with cast
        shadows on; 1 the second. The engine refits the cascades to the camera
        every frame and publishes them as scene.shadows.sun.* (the first) and
        scene.shadows.suns[N].* dot-paths. `resolution` is the shadow map's
        size in texels and should match it; `split_lambda` blends evenly spaced
        (0) and logarithmic (1) splits.
        """
        self._ptr.setSunShadowSettings(cascades, max_distance, split_lambda,
                                       resolution, caster_extension, sun)

    def set_local_shadows(self, uint32_t spot=16, uint32_t point=8,
                          uint32_t spot_resolution=2048, uint32_t point_resolution=1024,
                          uint32_t atlas_resolution=4096):
        """Shadow slots for spot and point lights with cast shadows on.

        Each frame the engine gives the slots to the lights that matter most
        (bright, near the camera, able to reach the view) and publishes them as
        scene.shadows.spot* and scene.shadows.point* dot-paths. The counts
        used are at most 16 spot and 8 point slots, and at most the slots the
        pipeline's shadow passes render.
        With atlas_resolution > 0 every map is a tile of one atlas of that
        size, sized each frame by how large the light looks, up to
        spot_resolution (point_resolution per cube face); with 0 the maps are
        array layers of exactly those sizes. The defaults match the default
        pipeline.
        """
        self._ptr.setLocalShadowSettings(spot, point, spot_resolution, point_resolution,
                                         atlas_resolution)

    def get_sun_shadow_cascade(self, uint32_t index, uint32_t sun=0):
        """World-to-light-clip matrix of cascade `index` of sun `sun` (0 or 1) this frame, as four columns."""
        return _mat4_to_tuple(self._ptr.getSunShadowCascade(index, sun))

    def set_pass_enabled(self, str pass_name, bint enabled):
        """Turn a pipeline pass on or off from the next frame.

        A disabled pass draws nothing but still clears its attachments, so a
        disabled shadow pass leaves everything lit. May be called from the
        on_init callback, before the pipeline is loaded. The declared name of a
        repeated pass ("repeat" in the JSON) switches every instance. Returns
        False if the pipeline has no pass with that name.
        """
        return self._ptr.setPassEnabled(pass_name.encode('utf-8'), enabled)

    def get_pass_draw_stats(self, str pass_name):
        """(drawn, culled): entities a geometry pass drew and culled as outside its view, last run."""
        name = pass_name.encode('utf-8')
        return (self._ptr.getPassDrawnCount(name), self._ptr.getPassCulledCount(name))

    def get_pass_occluded_count(self, str pass_name):
        """Entities a geometry pass with an execution "occlusion" image left out as hidden, last run."""
        return self._ptr.getPassOccludedCount(pass_name.encode('utf-8'))

    def set_occlusion_culling(self, bint enabled):
        """Whether passes with an execution "occlusion" image leave out the entities it hides
        (on by default)."""
        self._ptr.setOcclusionCulling(enabled)

    def is_occlusion_culling_enabled(self):
        """Whether occlusion culling is on (set_occlusion_culling)."""
        return self._ptr.isOcclusionCullingEnabled()

    def set_lod_threshold(self, float pixels):
        """Meshes loaded with lods=True draw the coarsest level of detail whose
        error, seen from the main camera, covers at most this many pixels; 0
        draws every mesh in full. Default 1."""
        self._ptr.setLodThreshold(pixels)

    def get_lod_threshold(self):
        """The level-of-detail threshold in pixels (set_lod_threshold)."""
        return self._ptr.getLodThreshold()

    def get_lod_counts(self):
        """Entities drawn at each level of detail last frame, among those with
        levels: [0] the full mesh, [k] the k-th coarser level."""
        return list(self._ptr.getLodCounts())

    # ── Render statistics ──────────────────────────────────────

    def enable_render_stats(self, bint gpu_timing=True):
        """Collect frame rate, frame times, draw counts and per-pass times.

        With gpu_timing, passes are also timed on the GPU with timestamp
        queries where the device has them. May be called before run().
        Setting SHOONYAKASHA_STATS=1 does this at startup and prints a
        summary every second.
        """
        self._ptr.setRenderStatsEnabled(True, gpu_timing)

    def disable_render_stats(self):
        """Stop collecting render statistics."""
        self._ptr.setRenderStatsEnabled(False, False)

    @property
    def render_stats_enabled(self):
        """Whether render statistics are being collected."""
        return self._ptr.isRenderStatsEnabled()

    @property
    def render_stats(self):
        """Render statistics over the last whole second, or None while off.

        A dict with fps, frame_time_ms, frame_time_max_ms, cpu_record_ms,
        gpu_ms (None without GPU timing), draw_calls, dispatches, vertices,
        validation_layers (True: CPU times include the layers' checks),
        summary (readable text) and passes: one dict per pass in execution
        order with name, cpu_ms, gpu_ms, draw_calls, dispatches, vertices.
        GPU times describe frames that finished one or two frames ago.
        """
        cdef RenderStatsSnapshot s = self._ptr.getRenderStats()
        if not s.enabled:
            return None
        passes = []
        cdef RenderPassStats p
        for p in s.passes:
            passes.append({
                'name': p.name.decode('utf-8'),
                'cpu_ms': p.cpuMs,
                'gpu_ms': p.gpuMs if p.gpuValid else None,
                'draw_calls': p.drawCalls,
                'dispatches': p.dispatches,
                'vertices': p.vertices,
            })
        return {
            'fps': s.fps,
            'frame_time_ms': s.frameTimeMs,
            'frame_time_max_ms': s.frameTimeMaxMs,
            'cpu_record_ms': s.cpuRecordMs,
            'gpu_ms': s.gpuMs if s.gpuValid else None,
            'draw_calls': s.drawCalls,
            'dispatches': s.dispatches,
            'vertices': s.vertices,
            'validation_layers': s.validationLayers,
            'passes': passes,
            'summary': s.summary.decode('utf-8'),
        }

    def is_pass_enabled(self, str pass_name):
        """Whether a pipeline pass is enabled; False if there is no such pass."""
        return self._ptr.isPassEnabled(pass_name.encode('utf-8'))

    def apply_pipeline_preset(self, str name):
        """Apply one of the pipeline's "presets", such as the default pipeline's
        "low", "medium" and "high" quality tiers: switches its passes and sets
        its scene.custom values. May be called from the on_init callback,
        before the pipeline is loaded. Returns False if there is no such preset.
        """
        return self._ptr.applyPipelinePreset(name.encode('utf-8'))

    def set_pipeline_image(self, str name, str path):
        """Load an image file for the pipeline's descriptor bindings with
        "externalImage": name, which sample white until one is set. Its
        colours are sRGB. May be called from on_init, and again to change it
        while running. Returns False if the file could not be loaded."""
        return self._ptr.setPipelineImage(name.encode('utf-8'), path.encode('utf-8'))

    def ray_query_supported(self):
        """Whether the device traces rays with ray queries, which the default
        pipeline's "raytraced" preset needs. Known from the on_init callback
        on; False before run()."""
        return self._ptr.rayQuerySupported()

    def get_pipeline_presets(self):
        """Names of the presets the loaded pipeline declares."""
        return [n.decode('utf-8') for n in self._ptr.getPipelinePresets()]


# ═══════════════════════════════════════════════════════════════
# Frame capture — module level
# ═══════════════════════════════════════════════════════════════

def video_recording_available():
    """Can this machine record video? False when no ffmpeg was found.

    Worth calling before start_recording(), which otherwise returns False
    without saying which of several reasons applied.
    """
    return videoRecordingAvailable()


def find_ffmpeg():
    """The ffmpeg that recording would use, or '' if none was found.

    Searched in order: $FFMPEG, PATH, then the usual install locations.
    """
    return findFfmpeg().decode('utf-8')


def _check_pipeline_json(str text):
    """The engine's loader error for pipeline JSON text, '' when it loads.
    shoonyakasha.pipeline.validate reports it."""
    return checkPipelineJson(text.encode('utf-8')).decode('utf-8')


def _load_pipeline_document(path):
    """The pipeline at `path` with its includes merged, as the engine reads
    it: (JSON text, '') on success, ('', error) on failure."""
    cdef string error
    cdef string text = loadPipelineDocumentText(str(path).encode('utf-8'), error)
    return text.decode('utf-8'), error.decode('utf-8')



# ═══════════════════════════════════════════════════════════════
# Planet level of detail
# ═══════════════════════════════════════════════════════════════

cdef LodView _lod_view(view):
    """A LodView from (position, forward, fov degrees, aspect)."""
    cdef LodView out
    position, forward, fov, aspect = view
    for k in range(3):
        out.position[k] = position[k]
        out.forward[k] = forward[k]
    out.fovDegrees = fov
    out.aspect = aspect
    return out


cdef vector[LodBody] _lod_bodies(bodies):
    """LodBody per (index, centre, occluder radius, ocean)."""
    cdef vector[LodBody] out
    cdef LodBody body
    for index, centre, occluder, ocean in bodies:
        body.index = index
        for k in range(3):
            body.centre[k] = centre[k]
        body.occluder = occluder
        body.ocean = ocean
        out.push_back(body)
    return out


cdef dict _lod_node(const LodNodeInfo& node):
    return {
        "face": node.face, "level": node.level, "x": node.x, "y": node.y,
        "slot": node.slot if node.slot >= 0 else None,
        "centre": (node.centre[0], node.centre[1], node.centre[2]),
        "edge": node.edge, "low": node.low, "high": node.high, "measured": node.measured,
        "bound_centre": (node.boundCentre[0], node.boundCentre[1], node.boundCentre[2]),
        "bound_radius": node.boundRadius,
    }


cdef class PlanetLod:
    """The level of detail of planets drawn as cube-spheres of chunks from
    one pool of GPU slots (include/Planet/PlanetLod.h): a quadtree per body,
    the pool's slots and the jobs that build chunks into them, the heights
    read back, the chunks to build again for a new sun, and the draw lists
    with each slot's instance values. shoonyakasha.planet.world.PlanetWorld
    drives it.

    A view is (position, forward, fov degrees, aspect); bodies are a list of
    (index, centre, occluder radius, ocean), positions in planet space, metres.
    """
    cdef CppPlanetLod* _ptr

    def __cinit__(self, int slots, int max_jobs, int vertices_per_chunk, double skirt_fraction,
                  int evict_frames=120, double merge_ratio=1.25):
        self._ptr = new CppPlanetLod(slots, max_jobs, vertices_per_chunk, skirt_fraction, evict_frames, merge_ratio)

    def __dealloc__(self):
        if self._ptr != NULL:
            del self._ptr
            self._ptr = NULL

    def create_tree(self, int body, double radius, double low, double high, int max_level, double split_factor,
                    double height_pad):
        """A tree for `body` with its six roots, each given a slot and a job."""
        self._ptr.createTree(body, radius, low, high, max_level, split_factor, height_pad)

    def drop_tree(self, int body):
        """The body's tree gone, its chunks released."""
        self._ptr.dropTree(body)

    def has_tree(self, int body):
        return self._ptr.hasTree(body)

    def split_factor(self, int body):
        return self._ptr.splitFactor(body)

    def set_split_factor(self, int body, double split_factor):
        self._ptr.setSplitFactor(body, split_factor)

    def set_face_colours(self, colours):
        """Six (r, g, b): the colours of colour mode 1."""
        cdef LodFaceColours out
        for face in range(6):
            for k in range(3):
                out[face][k] = colours[face][k]
        self._ptr.setFaceColours(out)

    def update(self, view, bodies, int64_t frame, bint frozen=False, bint requeue=False):
        """Unless `frozen`: the trees updated for the view, their missing
        chunks built (in view first, nearest first) and unused ones released;
        with `requeue` every built chunk queued to be built again; then the
        queue worked through with the jobs left."""
        self._ptr.update(_lod_view(view), _lod_bodies(bodies), frame, frozen, requeue)

    def take_heights(self, data):
        """ChunkHeights read back: rows of (lowest, highest, stamp, unused) float32 per slot."""
        cdef const unsigned char[::1] view = memoryview(data).cast('B')
        if view.shape[0] == 0:
            return
        self._ptr.takeHeights(<const float*>&view[0], view.shape[0] // 16)

    def write_draws(self, view, bodies, int colour_mode, double shadow_reach, uint32_t index_count):
        """The draw lists and the instances for the view."""
        self._ptr.writeDraws(_lod_view(view), _lod_bodies(bodies), colour_mode, shadow_reach, index_count)

    def upload(self, Engine engine):
        """ChunkInstances, ChunkDraws, OceanDraws and ShadowDraws handed to the engine."""
        cdef const vector[float]* instances = &self._ptr.instances()
        cdef const vector[uint32_t]* draws
        cdef string error
        error = engine._ptr.setBuffer(b"ChunkInstances", instances.data(), instances.size() * 4, 0)
        if not error.empty():
            raise ValueError(error.decode("utf-8"))
        for name, which in ((b"ChunkDraws", 0), (b"OceanDraws", 1), (b"ShadowDraws", 2)):
            draws = &self._ptr.chunkDraws() if which == 0 else (
                &self._ptr.oceanDraws() if which == 1 else &self._ptr.shadowDraws())
            error = engine._ptr.setBuffer(name, draws.data(), draws.size() * 4, 0)
            if not error.empty():
                raise ValueError(error.decode("utf-8"))

    def jobs(self):
        """This frame's jobs: ((face, level, x, y), (slot, skirt depth, stamp, body)) each."""
        cdef const vector[LodJob]* jobs = &self._ptr.jobs()
        out = []
        for k in range(jobs.size()):
            job = jobs.at(k)
            out.append(((job.a[0], job.a[1], job.a[2], job.a[3]), (job.b[0], job.b[1], job.b[2], job.b[3])))
        return out

    def clear_jobs(self):
        self._ptr.clearJobs()

    def instances(self):
        """The instance values as bytes: per slot three vec4 of float32."""
        cdef const vector[float]* values = &self._ptr.instances()
        return (<const char*>values.data())[:values.size() * 4]

    def draws(self, str which):
        """"chunk", "ocean" or "shadow" draws as bytes: a uint32 count, padding to 16 bytes, five uint32 per draw."""
        cdef const vector[uint32_t]* values
        if which == "chunk":
            values = &self._ptr.chunkDraws()
        elif which == "ocean":
            values = &self._ptr.oceanDraws()
        elif which == "shadow":
            values = &self._ptr.shadowDraws()
        else:
            raise ValueError("chunk, ocean or shadow")
        return (<const char*>values.data())[:values.size() * 4]

    @property
    def culled(self):
        """Selected chunks the last write_draws left out: out of view or behind the horizon."""
        return self._ptr.culled()

    @property
    def slots(self):
        return self._ptr.slots()

    @property
    def used(self):
        return self._ptr.used()

    @property
    def pending(self):
        """Chunks waiting for their heights."""
        return self._ptr.pending()

    @property
    def rebuild_queued(self):
        return self._ptr.rebuildQueued()

    def missing(self, int body):
        return self._ptr.missing(body)

    def selected_count(self, int body):
        return self._ptr.selectedCount(body)

    def selected(self, int body):
        """The nodes drawn, in the order selected, as dicts."""
        cdef vector[LodNodeInfo] nodes = self._ptr.selected(body)
        return [_lod_node(nodes[k]) for k in range(nodes.size())]

    def nodes(self, int body):
        """Every node of the body's tree, as dicts."""
        cdef vector[LodNodeInfo] nodes = self._ptr.nodes(body)
        return [_lod_node(nodes[k]) for k in range(nodes.size())]

    def missing_nodes(self, int body):
        """(distance, node dict) of the missing chunks, nearest first."""
        cdef vector[pair[double, LodNodeInfo]] nodes = self._ptr.missingNodes(body)
        return [(nodes[k].first, _lod_node(nodes[k].second)) for k in range(nodes.size())]
