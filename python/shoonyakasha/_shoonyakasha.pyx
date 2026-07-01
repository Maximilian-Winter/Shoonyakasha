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
from libc.stdint cimport uint32_t
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
    TextHAlign, TextHAlign_Left, TextHAlign_Center, TextHAlign_Right,
    EngineConfig, GltfOptions, ClipInfo, GltfResult as CppGltfResult,
)

from ._engine_api cimport (
    vec2, vec3, vec4, mat4,
    make_vec2, make_vec3, make_vec4, make_mat4,
    vec2_x, vec2_y, vec3_x, vec3_y, vec3_z,
    vec4_x, vec4_y, vec4_z, vec4_w, mat4_get,
    function_void, function_float, function_int, function_uint2,
    function_int_bool, function_float2,
    make_void_callback, make_update_callback, make_key_callback,
    make_resize_callback, make_key_event_callback,
    make_float2_callback, make_int_bool_callback,
    CppEngineAPI, CppSceneAPI, CppInputAPI, CppPhysicsAPI,
)


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

cdef inline vec2 _tuple_to_vec2(object t):
    return make_vec2(<float>t[0], <float>t[1])


# ═══════════════════════════════════════════════════════════════
# GltfResult — Python data class for glTF load results
# ═══════════════════════════════════════════════════════════════

class GltfResult:
    """Result of loading a glTF scene."""
    __slots__ = ('success', 'error', 'entities',
                 'total_vertices', 'total_indices',
                 'total_textures', 'total_materials',
                 'animation_clips', 'skeleton_count')

    def __init__(self):
        self.success = False
        self.error = ""
        self.entities = []
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

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False

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

    def get_camera_ortho_size(self, uint32_t entity):
        return self._ptr.getCameraOrthoSize(entity)

    def set_camera_ortho_size(self, uint32_t entity, float size):
        self._ptr.setCameraOrthoSize(entity, size)

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

    # ── Renderable ────────────────────────────────────────────

    def is_visible(self, uint32_t entity):
        return self._ptr.isVisible(entity)

    def set_visible(self, uint32_t entity, bint visible):
        self._ptr.setVisible(entity, visible)

    def get_cast_shadows(self, uint32_t entity):
        return self._ptr.getCastShadows(entity)

    def set_cast_shadows(self, uint32_t entity, bint cast_shadows):
        self._ptr.setCastShadows(entity, cast_shadows)

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

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False

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


# ═══════════════════════════════════════════════════════════════
# Physics — Python wrapper for PhysicsAPI
# ═══════════════════════════════════════════════════════════════

cdef class Physics:
    """Physics simulation control.

    Obtained via engine.physics — do not construct directly.
    """

    cdef CppPhysicsAPI* _ptr
    cdef bint _owned

    def __cinit__(self):
        self._ptr = NULL
        self._owned = False

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
# Engine — Python wrapper for EngineAPI (the main entry point)
# ═══════════════════════════════════════════════════════════════

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

    # Keep Python references to callbacks to prevent GC
    cdef list _callback_refs

    def __cinit__(self):
        self._ptr = NULL
        self._callback_refs = []

    def __init__(self, str title="Shoonyakasha Application",
                 int width=1600, int height=900,
                 str log_file="application.log", int log_level=1,
                 str hdr_environment_path="",
                 str pipeline_json_path="",
                 int max_frames_in_flight=2,
                 dict render_graph_parameters=None):
        """Create engine with configuration.

        Args:
            title: Window title
            width: Window width
            height: Window height
            log_file: Log file path
            log_level: 0=Debug, 1=Info, 2=Warning, 3=Error
            hdr_environment_path: HDR environment map (empty = no IBL)
            pipeline_json_path: JSON render graph (required)
            max_frames_in_flight: Vulkan frames in flight
            render_graph_parameters: Dict of str→int for SSBO sizing etc.
        """
        cdef EngineConfig cfg
        cfg.width = width
        cfg.height = height
        cfg.title = title.encode('utf-8')
        cfg.logFile = log_file.encode('utf-8')
        cfg.logLevel = log_level
        cfg.hdrEnvironmentPath = hdr_environment_path.encode('utf-8')
        cfg.pipelineJsonPath = pipeline_json_path.encode('utf-8')
        cfg.maxFramesInFlight = max_frames_in_flight

        if render_graph_parameters:
            for k, v in render_graph_parameters.items():
                cfg.renderGraphParameters.push_back(
                    pair[string, uint32_t](k.encode('utf-8'), <uint32_t>v))

        self._ptr = new CppEngineAPI(cfg)

    def __dealloc__(self):
        if self._ptr != NULL:
            del self._ptr

    # ── Lifecycle ─────────────────────────────────────────────

    def run(self):
        """Run the engine. Blocks until the window is closed.

        The GIL is released during the engine loop so Python callbacks
        can be invoked from the engine thread.
        """
        with nogil:
            self._ptr.run()

    # ── Callback Registration ─────────────────────────────────

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
        return self._scene_wrapper

    @property
    def input(self):
        """Access input polling/events API."""
        if self._input_wrapper is None:
            self._input_wrapper = Input.__new__(Input)
            self._input_wrapper._ptr = &self._ptr.getInput()
            self._input_wrapper._owned = False
        return self._input_wrapper

    @property
    def physics(self):
        """Access physics simulation API."""
        if self._physics_wrapper is None:
            self._physics_wrapper = Physics.__new__(Physics)
            self._physics_wrapper._ptr = &self._ptr.getPhysics()
            self._physics_wrapper._owned = False
        return self._physics_wrapper

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
        if 'flatten_hierarchy' in kwargs:
            opts.flattenHierarchy = kwargs['flatten_hierarchy']
        if 'max_texture_size' in kwargs:
            opts.maxTextureSize = kwargs['max_texture_size']
        if 'generate_mipmaps' in kwargs:
            opts.generateMipmaps = kwargs['generate_mipmaps']
        if 'srgb_albedo' in kwargs:
            opts.srgbAlbedo = kwargs['srgb_albedo']
        if 'name_prefix' in kwargs:
            opts.namePrefix = kwargs['name_prefix'].encode('utf-8')

        cdef string cpp_path = path.encode('utf-8')
        cdef CppGltfResult result = self._ptr.loadGltfScene(cpp_path, opts)
        return _wrap_gltf_result(result)

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

    def set_custom_uint(self, str key, uint32_t value):
        """Set custom uint for shader uniforms."""
        self._ptr.setCustomUint(key.encode('utf-8'), value)
