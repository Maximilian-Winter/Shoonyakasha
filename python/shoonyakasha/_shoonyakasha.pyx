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
from libc.stdint cimport uint8_t, uint32_t
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
    TextVAlign, TextVAlign_Top, TextVAlign_Middle, TextVAlign_Bottom,
    CanvasScaleMode, CanvasScaleMode_ConstantPixel, CanvasScaleMode_ScaleWithScreen,
    EngineConfig, GltfOptions, RecordingOptions, ClipInfo, GltfResult as CppGltfResult,
    RenderStatsSnapshot, RenderPassStats,
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
    videoRecordingAvailable, findFfmpeg,
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

# Vertical text alignment (canvas UI)
TEXT_ALIGN_TOP = <int>TextVAlign_Top
TEXT_ALIGN_MIDDLE = <int>TextVAlign_Middle
TEXT_ALIGN_BOTTOM = <int>TextVAlign_Bottom

# Canvas scale modes
CANVAS_CONSTANT_PIXEL = <int>CanvasScaleMode_ConstantPixel
CANVAS_SCALE_WITH_SCREEN = <int>CanvasScaleMode_ScaleWithScreen


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
        """Which image the light's rectangle shines with: 0, the default,
        none; pipelines say what other slots mean (the showroom's 1 is its
        "lightImage"). Published as scene.lights[N].source.w."""
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

    # ── Canvases ──────────────────────────────────────────────

    def create_canvas(self, reference_size=(1920, 1080), int scale_mode=CANVAS_SCALE_WITH_SCREEN,
                      int sort_order=0):
        """A canvas over the screen. A higher sort_order draws over, and takes the pointer first."""
        return <uint32_t>self._ptr.createCanvas(_tuple_to_vec2(reference_size),
                                                <CanvasScaleMode>scale_mode, sort_order)

    def create_world_canvas(self, pixel_size, world_size, float emission=1.0):
        """A canvas of pixel_size pixels on a quad of world_size units, facing +Z.

        Place it with engine.scene.set_position and set_rotation. Its material
        emits the canvas at emission times its colour.
        """
        return <uint32_t>self._ptr.createWorldCanvas(_tuple_to_vec2(pixel_size), _tuple_to_vec2(world_size),
                                                     emission)

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

    # ── Content ───────────────────────────────────────────────
    #
    # The text methods act on the element's own text or, for a button or
    # toggle, on its label.

    def set_text(self, uint32_t element, str text):
        self._ptr.setText(element, text.encode("utf-8"))

    def get_text(self, uint32_t element):
        return self._ptr.getText(element).decode("utf-8")

    def set_font(self, uint32_t element, uint32_t font):
        self._ptr.setFont(element, font)

    def set_font_size(self, uint32_t element, float size):
        self._ptr.setFontSize(element, size)

    def set_text_align(self, uint32_t element, int horizontal, int vertical=TEXT_ALIGN_TOP):
        """TEXT_ALIGN_LEFT/CENTER/RIGHT and TEXT_ALIGN_TOP/MIDDLE/BOTTOM."""
        self._ptr.setTextAlign(element, <TextHAlign>horizontal, <TextVAlign>vertical)

    def set_text_wrap(self, uint32_t element, bint wrap):
        self._ptr.setTextWrap(element, wrap)

    def set_color(self, uint32_t element, color):
        """The colour of the element's own text, image and panel."""
        self._ptr.setColor(element, _tuple_to_vec4(color))

    def set_texture(self, uint32_t element, str path):
        """The texture of the element's image or panel. False if it cannot be loaded."""
        return self._ptr.setTexture(element, path.encode("utf-8"))

    def set_panel_border(self, uint32_t element, border, float border_scale=1.0):
        self._ptr.setPanelBorder(element, _tuple_to_vec4(border), border_scale)

    def set_raycast_target(self, uint32_t element, bint target):
        """Whether the element's image or panel stops the pointer."""
        self._ptr.setRaycastTarget(element, target)

    # ── Interaction ───────────────────────────────────────────

    def set_interactable(self, uint32_t element, bint enabled=True):
        """Make the element take the pointer, or disable it. Widgets are interactable."""
        self._ptr.setInteractable(element, enabled)

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
    cdef Ecs _ecs_wrapper
    cdef UI _ui_wrapper

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
                 dict render_graph_parameters=None,
                 environment_color=(0.25, 0.28, 0.33),
                 enable_validation=None):
        """Create engine with configuration.

        Args:
            title: Window title
            width: Window width
            height: Window height
            log_file: Log file path
            log_level: 0=Debug, 1=Info, 2=Warning, 3=Error
            hdr_environment_path: HDR environment map. Empty: a pipeline
                that samples IBL gets a uniform environment_color instead.
            pipeline_json_path: JSON render graph. Empty: the default
                pipeline, shoonyakasha.pipeline.DEFAULT.
            max_frames_in_flight: Vulkan frames in flight
            render_graph_parameters: Dict of str→int for SSBO sizing etc.
            environment_color: (r, g, b) of the uniform environment used
                when there is no HDR map.
            enable_validation: Vulkan validation layers. None follows the
                build: off in the released package, where they would slow
                every draw. SHOONYAKASHA_VALIDATION=1 or 0 overrides it.
        """
        if not pipeline_json_path:
            from . import pipeline as _pipeline
            pipeline_json_path = str(_pipeline.DEFAULT)
        cdef EngineConfig cfg
        cfg.width = width
        cfg.height = height
        cfg.title = title.encode('utf-8')
        cfg.logFile = log_file.encode('utf-8')
        cfg.logLevel = log_level
        cfg.hdrEnvironmentPath = hdr_environment_path.encode('utf-8')
        for i in range(3):
            cfg.uniformEnvironmentColor[i] = float(environment_color[i])
        cfg.pipelineJsonPath = pipeline_json_path.encode('utf-8')
        cfg.maxFramesInFlight = max_frames_in_flight
        if enable_validation is not None:
            cfg.enableValidation = bool(enable_validation)

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

    def start_recording(self, str path, int fps=30, int quality=18,
                        str codec="libx264", str ffmpeg_path=""):
        """Record every presented frame to a video file.

        The container follows the extension -- .mkv, .mp4, .webm. Needs ffmpeg
        on PATH or in $FFMPEG; see shoonyakasha.video_recording_available().

        quality is x264's CRF: 0 lossless, 18 visually lossless, 51 worst.
        """
        cdef RecordingOptions opts
        opts.fps = fps
        opts.quality = quality
        opts.codec = codec.encode("utf-8")
        opts.ffmpegPath = ffmpeg_path.encode("utf-8")
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

    def set_custom_mat4(self, str key, value):
        """Set custom mat4 for shader uniforms.

        `value` is four columns of four floats (column-major), the shape
        Scene.get_world_matrix returns.
        """
        self._ptr.setCustomMat4(key.encode('utf-8'), _tuple_to_mat4(value))

    def set_custom_uint(self, str key, uint32_t value):
        """Set custom uint for shader uniforms."""
        self._ptr.setCustomUint(key.encode('utf-8'), value)

    def set_sun_shadows(self, uint32_t cascades=4, float max_distance=60.0,
                        float split_lambda=0.75, uint32_t resolution=2048,
                        float caster_extension=50.0):
        """Configure the sun's shadow cascades.

        Applies to the first directional light with cast shadows on. The engine
        refits the cascades to the camera every frame and publishes them as
        scene.shadows.sun.* dot-paths. `resolution` is the shadow map's size in
        texels and should match it; `split_lambda` blends evenly spaced (0)
        and logarithmic (1) splits.
        """
        self._ptr.setSunShadowSettings(cascades, max_distance, split_lambda,
                                       resolution, caster_extension)

    def set_local_shadows(self, uint32_t spot=8, uint32_t point=4,
                          uint32_t spot_resolution=2048, uint32_t point_resolution=1024,
                          uint32_t atlas_resolution=4096):
        """Shadow slots for spot and point lights with cast shadows on.

        Each frame the engine gives the slots to the lights that matter most
        (bright, near the camera, able to reach the view) and publishes them as
        scene.shadows.spot* and scene.shadows.point* dot-paths. The counts
        should match the pipeline's passes; at most 8 spot and 4 point slots.
        With atlas_resolution > 0 every map is a tile of one atlas of that
        size, sized each frame by how large the light looks, up to
        spot_resolution (point_resolution per cube face); with 0 the maps are
        array layers of exactly those sizes. The defaults match the default
        pipeline.
        """
        self._ptr.setLocalShadowSettings(spot, point, spot_resolution, point_resolution,
                                         atlas_resolution)

    def get_sun_shadow_cascade(self, uint32_t index):
        """World-to-light-clip matrix of a sun cascade this frame, as four columns."""
        return _mat4_to_tuple(self._ptr.getSunShadowCascade(index))

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
