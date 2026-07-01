# distutils: language = c++
#
# _engine_api.pxd — All C++ declarations for the Shoonyakasha facade
#
# Consolidates GLM types, callback bridges, and all four API classes
# into a single .pxd to avoid Cython forward-declaration limitations.
#
# Cython cannot "complete" a forward-declared cppclass from another .pxd —
# the first declaration wins. So all classes must be fully declared here.
#

from libcpp.string cimport string
from libcpp.vector cimport vector
from libcpp.memory cimport shared_ptr
from libcpp cimport bool as cbool
from libc.stdint cimport uint32_t
from cpython.ref cimport PyObject

from ._facade_types cimport (
    EntityHandle, EngineConfig, GltfOptions,
    GltfResult as CppGltfResult,
    CameraType, LightType, UIAnchor, TextHAlign,
)


# ═══════════════════════════════════════════════════════════════
# GLM opaque types
# ═══════════════════════════════════════════════════════════════

cdef extern from "glm/glm.hpp" namespace "glm":
    cdef cppclass vec2 "glm::vec2":
        pass
    cdef cppclass vec3 "glm::vec3":
        pass
    cdef cppclass vec4 "glm::vec4":
        pass
    cdef cppclass mat4 "glm::mat4":
        pass


# ═══════════════════════════════════════════════════════════════
# GLM bridge helpers
# ═══════════════════════════════════════════════════════════════

cdef extern from "_glm_bridge.h" namespace "ShoonyakashaBridge":
    vec2 make_vec2(float x, float y)
    vec3 make_vec3(float x, float y, float z)
    vec4 make_vec4(float x, float y, float z, float w)
    mat4 make_mat4(float m00, float m01, float m02, float m03,
                   float m10, float m11, float m12, float m13,
                   float m20, float m21, float m22, float m23,
                   float m30, float m31, float m32, float m33)

    float vec2_x(const vec2& v)
    float vec2_y(const vec2& v)
    float vec3_x(const vec3& v)
    float vec3_y(const vec3& v)
    float vec3_z(const vec3& v)
    float vec4_x(const vec4& v)
    float vec4_y(const vec4& v)
    float vec4_z(const vec4& v)
    float vec4_w(const vec4& v)
    float mat4_get(const mat4& m, int col, int row)


# ═══════════════════════════════════════════════════════════════
# std::function types (opaque to Cython)
# ═══════════════════════════════════════════════════════════════

cdef extern from "<functional>" namespace "std":
    cdef cppclass function_void "std::function<void()>":
        pass
    cdef cppclass function_float "std::function<void(float)>":
        pass
    cdef cppclass function_int "std::function<void(int)>":
        pass
    cdef cppclass function_uint2 "std::function<void(uint32_t, uint32_t)>":
        pass
    cdef cppclass function_int_bool "std::function<void(int, bool)>":
        pass
    cdef cppclass function_float2 "std::function<void(float, float)>":
        pass
    cdef cppclass function_bool_float "std::function<bool(float)>":
        pass


# ═══════════════════════════════════════════════════════════════
# Callback bridge helpers
# ═══════════════════════════════════════════════════════════════

cdef extern from "_callback_bridge.h" namespace "ShoonyakashaBridge":
    function_void make_void_callback(PyObject* callable)
    function_float make_update_callback(PyObject* callable)
    function_int make_key_callback(PyObject* callable)
    function_uint2 make_resize_callback(PyObject* callable)
    function_int_bool make_key_event_callback(PyObject* callable)
    function_float2 make_float2_callback(PyObject* callable)
    function_int_bool make_int_bool_callback(PyObject* callable)


# ═══════════════════════════════════════════════════════════════
# ECS bridge helpers (generic script component payloads + systems)
# ═══════════════════════════════════════════════════════════════

cdef extern from "_ecs_bridge.h" namespace "ShoonyakashaBridge":
    shared_ptr[void] wrap_py_object(PyObject* obj)
    # Declared returning `object` (not PyObject*): unwrap_py_object hands
    # back a NEW reference, and this is Cython's documented idiom for
    # "C++ function returns ownership of a PyObject* it created" - Cython
    # takes over managing that reference automatically. Never call this
    # with a null/empty shared_ptr (see Ecs.get_component in _shoonyakasha.pyx).
    object unwrap_py_object(const shared_ptr[void]& ptr)
    function_bool_float make_system_update_callback(PyObject* callable)


# ═══════════════════════════════════════════════════════════════
# SceneAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/SceneAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppSceneAPI "Shoonyakasha::Facade::SceneAPI":

        # Entity Lifecycle
        EntityHandle createEntity(const string& name) except +
        void destroyEntity(EntityHandle entity)
        cbool isValid(EntityHandle entity) const
        size_t getEntityCount() const

        # Entity Queries
        EntityHandle findEntityByName(const string& name) const
        vector[EntityHandle] findEntitiesWithTag(const string& tag) const
        EntityHandle getMainCamera()
        vector[EntityHandle] getAllEntities() const

        # Component Management (string-based)
        cbool addComponent(EntityHandle entity, const string& componentName)
        cbool removeComponent(EntityHandle entity, const string& componentName)
        cbool hasComponent(EntityHandle entity, const string& componentName) const
        vector[string] getComponentNames() const

        # Name / Tag / Active
        string getName(EntityHandle entity) const
        void setName(EntityHandle entity, const string& name)
        string getTag(EntityHandle entity) const
        void setTag(EntityHandle entity, const string& tag)
        cbool isActive(EntityHandle entity) const
        void setActive(EntityHandle entity, cbool active)

        # Transform
        vec3 getPosition(EntityHandle entity) const
        void setPosition(EntityHandle entity, const vec3& pos)
        vec3 getRotation(EntityHandle entity) const
        void setRotation(EntityHandle entity, const vec3& eulerRadians)
        vec3 getScale(EntityHandle entity) const
        void setScale(EntityHandle entity, const vec3& scale)
        vec3 getWorldPosition(EntityHandle entity) const
        mat4 getWorldMatrix(EntityHandle entity) const
        vec3 getForward(EntityHandle entity) const
        vec3 getRight(EntityHandle entity) const
        vec3 getUp(EntityHandle entity) const

        # Camera
        CameraType getCameraType(EntityHandle entity) const
        void setCameraType(EntityHandle entity, CameraType type)
        float getCameraFov(EntityHandle entity) const
        void setCameraFov(EntityHandle entity, float fov)
        float getCameraNear(EntityHandle entity) const
        void setCameraNear(EntityHandle entity, float nearPlane)
        float getCameraFar(EntityHandle entity) const
        void setCameraFar(EntityHandle entity, float farPlane)
        float getCameraOrthoSize(EntityHandle entity) const
        void setCameraOrthoSize(EntityHandle entity, float size)
        cbool isCameraMain(EntityHandle entity) const
        void setCameraMain(EntityHandle entity, cbool isMain)

        # Light
        LightType getLightType(EntityHandle entity) const
        void setLightType(EntityHandle entity, LightType type)
        vec3 getLightColor(EntityHandle entity) const
        void setLightColor(EntityHandle entity, const vec3& color)
        float getLightIntensity(EntityHandle entity) const
        void setLightIntensity(EntityHandle entity, float intensity)
        float getLightRange(EntityHandle entity) const
        void setLightRange(EntityHandle entity, float range)
        cbool getLightCastShadows(EntityHandle entity) const
        void setLightCastShadows(EntityHandle entity, cbool castShadows)

        # Material
        void setMaterialFloat(EntityHandle entity, const string& param, float value)
        float getMaterialFloat(EntityHandle entity, const string& param, float defaultVal) const
        void setMaterialVec3(EntityHandle entity, const string& param, const vec3& value)
        vec3 getMaterialVec3(EntityHandle entity, const string& param, const vec3& defaultVal) const
        void setMaterialVec4(EntityHandle entity, const string& param, const vec4& value)
        vec4 getMaterialVec4(EntityHandle entity, const string& param, const vec4& defaultVal) const
        cbool hasMaterialParam(EntityHandle entity, const string& param) const
        cbool setMaterialTexture(EntityHandle entity, const string& slotName, const string& filePath)

        # Sprite / UI
        cbool setSpriteTexture(EntityHandle entity, const string& filePath)
        void setSpriteColor(EntityHandle entity, const vec4& color)
        vec4 getSpriteColor(EntityHandle entity) const
        void setSpriteUVRect(EntityHandle entity, const vec4& uvRect)
        vec4 getSpriteUVRect(EntityHandle entity) const
        cbool isScreenSpaceSprite(EntityHandle entity) const
        void setUIAnchor(EntityHandle entity, UIAnchor anchor, const vec2& offsetPixels)
        UIAnchor getUIAnchor(EntityHandle entity) const
        vec2 getUIAnchorOffset(EntityHandle entity) const

        # Text
        void setText(EntityHandle entity, const string& text)
        string getText(EntityHandle entity) const
        void setTextColor(EntityHandle entity, const vec4& color)
        void setTextFontSize(EntityHandle entity, float fontSize)
        void setTextAlign(EntityHandle entity, TextHAlign align)

        # Renderable
        cbool isVisible(EntityHandle entity) const
        void setVisible(EntityHandle entity, cbool visible)
        cbool getCastShadows(EntityHandle entity) const
        void setCastShadows(EntityHandle entity, cbool castShadows)

        # Hierarchy
        EntityHandle getParent(EntityHandle entity) const
        void setParent(EntityHandle child, EntityHandle parent)
        vector[EntityHandle] getChildren(EntityHandle entity) const

        # Animation
        int getAnimationClipCount(EntityHandle entity) const
        string getAnimationClipName(EntityHandle entity, int clipIndex) const
        float getAnimationClipDuration(EntityHandle entity, int clipIndex) const
        void playAnimation(EntityHandle entity, int clipIndex)
        void stopAnimation(EntityHandle entity)
        cbool isAnimationPlaying(EntityHandle entity) const
        float getAnimationSpeed(EntityHandle entity) const
        void setAnimationSpeed(EntityHandle entity, float speed)
        float getAnimationTime(EntityHandle entity) const
        void setAnimationTime(EntityHandle entity, float time)
        cbool isAnimationLooping(EntityHandle entity) const
        void setAnimationLooping(EntityHandle entity, cbool loop)
        int getCurrentAnimationClip(EntityHandle entity) const

        # Serialization
        cbool saveToFile(const string& path) const
        cbool loadFromFile(const string& path)


# ═══════════════════════════════════════════════════════════════
# InputAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/InputAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppInputAPI "Shoonyakasha::Facade::InputAPI":

        # Polling
        cbool isKeyDown(int keyCode) const
        cbool isMouseButtonDown(int button) const
        vec2 getMousePosition() const
        vec2 getMouseDelta() const
        vec2 getScrollDelta() const
        cbool isMouseCaptured() const

        # Event Callbacks
        void setOnKeyEvent(function_int_bool cb)
        void setOnMouseMove(function_float2 cb)
        void setOnMouseButton(function_int_bool cb)
        void setOnMouseScroll(function_float2 cb)


# ═══════════════════════════════════════════════════════════════
# PhysicsAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/PhysicsAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppPhysicsAPI "Shoonyakasha::Facade::PhysicsAPI":

        # Enable / Disable
        cbool isEnabled() const
        void setEnabled(cbool enabled)

        # World Configuration
        void setGravity(const vec3& gravity)
        vec3 getGravity() const
        void setFixedTimeStep(float timeStep)
        float getFixedTimeStep() const
        void setMaxSubSteps(int maxSubSteps)
        int getMaxSubSteps() const

        # Forces / Impulses
        void addForce(EntityHandle entity, const vec3& force)
        void addImpulse(EntityHandle entity, const vec3& impulse)
        void addTorqueImpulse(EntityHandle entity, const vec3& torque)

        # Velocity
        void setLinearVelocity(EntityHandle entity, const vec3& velocity)
        vec3 getLinearVelocity(EntityHandle entity) const
        void setAngularVelocity(EntityHandle entity, const vec3& velocity)
        vec3 getAngularVelocity(EntityHandle entity) const

        # Body Management
        void rebuildBody(EntityHandle entity)
        uint32_t getBodyCount() const


# ═══════════════════════════════════════════════════════════════
# EcsAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/EcsAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppEcsAPI "Shoonyakasha::Facade::EcsAPI":

        # Script Component Access
        void setComponent(EntityHandle entity, const string& name, shared_ptr[void] data)
        shared_ptr[void] getComponent(EntityHandle entity, const string& name) const
        cbool hasComponent(EntityHandle entity, const string& name) const
        cbool removeComponent(EntityHandle entity, const string& name)
        vector[string] getComponentNames(EntityHandle entity) const
        vector[EntityHandle] findEntitiesWithComponent(const string& name) const

        # System Management
        cbool addSystem(const string& name, function_bool_float fn,
                        int priority, int maxConsecutiveFailures)
        cbool removeSystem(const string& name)
        cbool hasSystem(const string& name) const
        cbool setSystemEnabled(const string& name, cbool enabled)
        cbool isSystemEnabled(const string& name) const
        int getSystemFailureCount(const string& name) const
        int getSystemMaxFailures(const string& name) const
        void setSystemMaxFailures(const string& name, int max)
        void resetSystemFailureCount(const string& name)


# ═══════════════════════════════════════════════════════════════
# EngineAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/EngineAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppEngineAPI "Shoonyakasha::Facade::EngineAPI":
        CppEngineAPI(const EngineConfig& config) except +

        # Lifecycle
        void run() except + nogil

        # Callback registration
        void setOnInit(function_void cb)
        void setOnPostInit(function_void cb)
        void setOnUpdate(function_float cb)
        void setOnPreRender(function_float cb)
        void setOnPostRender(function_void cb)
        void setOnKeyPressed(function_int cb)
        void setOnResize(function_uint2 cb)
        void setOnCleanup(function_void cb)

        # Sub-API access (returns references)
        CppSceneAPI& getScene()
        CppInputAPI& getInput()
        CppPhysicsAPI& getPhysics()
        CppEcsAPI& getEcs()

        # Convenience helpers
        EntityHandle createCamera(const vec3& pos, float fov, float speed,
                                  float nearPlane, float farPlane)
        CppGltfResult loadGltfScene(const string& path, const GltfOptions& opts) except +
        EntityHandle createDirectionalLight(const vec3& direction,
                                            const vec3& color, float intensity)
        EntityHandle createPointLight(const vec3& position,
                                      const vec3& color, float intensity, float range)
        EntityHandle createSprite(const vec3& worldPos, const string& texturePath,
                                  const vec2& size, const vec4& tint)
        EntityHandle createUIPanel(UIAnchor anchor, const vec2& offsetPixels,
                                   const vec2& sizePixels, const string& texturePath,
                                   const vec4& color)
        EntityHandle createText(const string& text, UIAnchor anchor,
                                const vec2& offsetPixels, const string& fontPath,
                                float fontSize, const vec4& color)
        EntityHandle getCameraEntity() const
        float getDeltaTime() const

        # Custom scene values
        void setCustomFloat(const string& key, float value)
        void setCustomVec2(const string& key, const vec2& value)
        void setCustomVec3(const string& key, const vec3& value)
        void setCustomVec4(const string& key, const vec4& value)
        void setCustomUint(const string& key, uint32_t value)
