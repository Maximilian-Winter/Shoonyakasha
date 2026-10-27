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
from libcpp.utility cimport pair
from libcpp cimport bool as cbool
from libc.stdint cimport uint8_t, uint32_t, uint64_t, int64_t
from cpython.ref cimport PyObject

from ._facade_types cimport (
    EntityHandle, EngineConfig, GltfOptions, RecordingOptions,
    GltfResult as CppGltfResult, RenderStatsSnapshot, TextureResidencyStats,
    CameraType, LightType, UIAnchor, TextHAlign, TextVAlign, TextDirection, TextOrientation, LayoutDirection,
    CanvasScaleMode, SizeFit, AccessibleRole,
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
        cbool getCameraReverseZ(EntityHandle entity) const
        void setCameraReverseZ(EntityHandle entity, cbool reverseZ)
        float getCameraOrthoSize(EntityHandle entity) const
        void setCameraOrthoSize(EntityHandle entity, float size)
        float getCameraAspect(EntityHandle entity) const
        void setCameraAspect(EntityHandle entity, float aspect)
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
        vec2 getLightCone(EntityHandle entity) const
        void setLightCone(EntityHandle entity, float innerDegrees, float outerDegrees)
        float getLightSourceRadius(EntityHandle entity) const
        void setLightSourceRadius(EntityHandle entity, float radius)
        vec2 getLightSourceSize(EntityHandle entity) const
        void setLightSourceSize(EntityHandle entity, float width, float height)
        uint32_t getLightSourceImage(EntityHandle entity) const
        void setLightSourceImage(EntityHandle entity, uint32_t slot)

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
        void setTextLayerMask(EntityHandle entity, uint8_t mask)
        void setTextSortKey(EntityHandle entity, uint32_t sortKey)
        void setTextVisible(EntityHandle entity, cbool visible)
        cbool isTextVisible(EntityHandle entity)

        # Renderable
        cbool isVisible(EntityHandle entity) const
        void setVisible(EntityHandle entity, cbool visible)
        cbool setMaterialVariant(EntityHandle entity, int index)
        cbool getCastShadows(EntityHandle entity) const
        void setCastShadows(EntityHandle entity, cbool castShadows)
        uint8_t getRenderLayerMask(EntityHandle entity) const
        void setRenderLayerMask(EntityHandle entity, uint8_t mask)
        uint32_t getSortKey(EntityHandle entity) const
        void setSortKey(EntityHandle entity, uint32_t sortKey)

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

        # Morph targets
        vector[float] getMorphWeights(EntityHandle entity) const
        cbool setMorphWeights(EntityHandle entity, const vector[float]& weights)

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
        cbool isGamepadConnected() const
        cbool isGamepadButtonDown(int button) const
        float getGamepadAxis(int axis) const

        # Event Callbacks
        void setOnKeyEvent(function_int_bool cb)
        void setOnMouseMove(function_float2 cb)
        void setOnMouseButton(function_int_bool cb)
        void setOnMouseScroll(function_float2 cb)
        void setOnChar(function_int cb)


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
# UIAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "Facade/UIAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass CppUIAPI "Shoonyakasha::Facade::UIAPI":

        # Fonts
        uint32_t loadFont(const string& path)
        uint32_t getDefaultFont()
        void setDefaultFont(uint32_t font)
        void setFontFallbacks(uint32_t font, const vector[uint32_t]& fallbacks)
        void setDefaultFontFallbacks(const vector[uint32_t]& fallbacks)
        void setFontName(uint32_t font, const string& name)
        void setFontStyles(uint32_t font, uint32_t bold, uint32_t italic, uint32_t boldItalic)

        # Canvases
        EntityHandle createCanvas(const vec2& referenceSize, CanvasScaleMode scaleMode, int sortOrder)
        EntityHandle createWorldCanvas(const vec2& pixelSize, const vec2& worldSize, float emission, bint lit)
        void setCanvasScaling(EntityHandle canvas, CanvasScaleMode mode, const vec2& referenceSize,
                              float scaleFactor, float match)
        void setCanvasSortOrder(EntityHandle canvas, int sortOrder)
        void setCanvasPixelSize(EntityHandle canvas, const vec2& pixelSize)
        void setCanvasClearColor(EntityHandle canvas, const vec4& color)
        vec2 getCanvasSize(EntityHandle canvas) const

        # Elements
        EntityHandle createElement(EntityHandle parent, const vec2& size)
        EntityHandle createPanel(EntityHandle parent, const vec2& size, const vec4& color,
                                 const string& texturePath, const vec4& border)
        EntityHandle createImage(EntityHandle parent, const vec2& size, const string& texturePath,
                                 const vec4& color)
        EntityHandle createText(EntityHandle parent, const string& text, float fontSize,
                                const vec4& color, uint32_t font)
        EntityHandle createButton(EntityHandle parent, const string& label, const vec2& size)
        EntityHandle createToggle(EntityHandle parent, const string& label, cbool isOn, const vec2& size)
        EntityHandle createSlider(EntityHandle parent, float minValue, float maxValue, float value,
                                  const vec2& size)
        EntityHandle createTextField(EntityHandle parent, const string& placeholder, const vec2& size)
        void setParent(EntityHandle element, EntityHandle parent)

        # Layout
        void setRect(EntityHandle element, const vec2& anchorMin, const vec2& anchorMax,
                     const vec2& pivot, const vec2& position, const vec2& size)
        void setAnchor(EntityHandle element, const vec2& anchor)
        void setPosition(EntityHandle element, const vec2& position)
        void setSize(EntityHandle element, const vec2& size)
        vec4 getRect(EntityHandle element) const
        void setVisible(EntityHandle element, cbool visible)
        cbool isVisible(EntityHandle element) const
        void setClip(EntityHandle element, cbool clip)

        # Layout groups
        EntityHandle createHorizontalLayout(EntityHandle parent, const vec2& size, float spacing,
                                            const vec4& padding)
        EntityHandle createVerticalLayout(EntityHandle parent, const vec2& size, float spacing,
                                          const vec4& padding)
        EntityHandle createGridLayout(EntityHandle parent, const vec2& size, const vec2& cellSize,
                                      int columns, const vec2& spacing, const vec4& padding)
        void setLayoutAlignment(EntityHandle group, const vec2& alignment)
        void setLayoutChildSizing(EntityHandle group, cbool controlWidth, cbool controlHeight,
                                  cbool expandWidth, cbool expandHeight)
        void setLayoutElement(EntityHandle element, const vec2& minSize, const vec2& preferredSize,
                              const vec2& flexibleSize, cbool ignoreLayout)
        void setSizeToContent(EntityHandle element, SizeFit horizontal, SizeFit vertical)
        vec2 getPreferredSize(EntityHandle element) const

        # Scroll views
        EntityHandle createScrollView(EntityHandle parent, const vec2& size, cbool horizontal, cbool vertical)
        EntityHandle getScrollContent(EntityHandle view) const
        vec2 getScrollPosition(EntityHandle view) const
        void setScrollPosition(EntityHandle view, const vec2& position)
        void scrollTo(EntityHandle view, EntityHandle element)
        void setScrollSettings(EntityHandle view, float wheelSpeed, cbool inertia)
        void setScrollElastic(EntityHandle view, cbool elastic, float elasticity)

        # Content
        void setText(EntityHandle element, const string& text)
        string getText(EntityHandle element) const
        void setFont(EntityHandle element, uint32_t font)
        void setFontSize(EntityHandle element, float fontSize)
        void setTextAlign(EntityHandle element, TextHAlign horizontal, TextVAlign vertical)
        void setTextDirection(EntityHandle element, TextDirection direction)
        void setTextWrap(EntityHandle element, cbool wrap)
        void setTextOrientation(EntityHandle element, TextOrientation orientation, cbool uprightDigits)
        void setAccessible(EntityHandle element, const string& name, const string& description, cbool hidden,
                           AccessibleRole role)
        void setRichText(EntityHandle element, cbool richText)
        void setLinkColor(EntityHandle element, const vec4& color)
        string clickedLink(EntityHandle element) const
        string hoveredLink(EntityHandle element) const
        void setLayoutDirection(EntityHandle element, LayoutDirection direction)
        cbool isRightToLeft(EntityHandle element) const
        void setMirrorInRightToLeft(EntityHandle element, cbool mirror)
        void setColor(EntityHandle element, const vec4& color)
        void setGeometry(EntityHandle element, const vector[vec2]& positions, const vector[uint32_t]& indices, const vector[vec4]& colors) except +
        void setWidgetPartColor(EntityHandle element, const string& part, const vec4& color) except +
        void setInteractionColors(EntityHandle element, const vec4& normal, const vec4& hover, const vec4& pressed, const vec4& focused, const vec4& disabled)
        cbool setTexture(EntityHandle element, const string& path)
        string setTextureData(EntityHandle element, const void* data, size_t size, uint32_t width,
                              uint32_t height, const string& format)
        void setPanelBorder(EntityHandle element, const vec4& border, float borderScale)
        void setRaycastTarget(EntityHandle element, cbool target)

        # Interaction
        void setInteractable(EntityHandle element, cbool enabled)
        void addTintTarget(EntityHandle interactable, EntityHandle target)
        void removeTintTarget(EntityHandle interactable, EntityHandle target)
        vector[EntityHandle] getTintTargets(EntityHandle interactable) const
        cbool isHovered(EntityHandle element) const
        cbool isPressed(EntityHandle element) const
        cbool wasClicked(EntityHandle element) const
        cbool valueChanged(EntityHandle element) const
        cbool getToggle(EntityHandle toggle) const
        void setToggle(EntityHandle toggle, cbool isOn)
        float getSliderValue(EntityHandle slider) const
        void setSliderValue(EntityHandle slider, float value)
        void setSliderRange(EntityHandle slider, float minValue, float maxValue, cbool wholeNumbers)
        void focus(EntityHandle element)
        EntityHandle getFocusedElement() const
        cbool isKeyboardCaptured() const
        void setNavigable(EntityHandle element, cbool navigable)
        cbool isPointerOverUI() const
        EntityHandle getPointerCanvas() const
        vec2 getPointerPosition() const
        EntityHandle getHoveredElement() const

        # Text fields
        cbool textSubmitted(EntityHandle field) const
        cbool textChanged(EntityHandle field) const
        void selectAll(EntityHandle field)
        void setTextFieldOptions(EntityHandle field, uint32_t maxLength, cbool password, cbool readOnly)
        void setTextFieldMultiLine(EntityHandle field, cbool multiLine, cbool submitOnEnter)
        string getTextFieldComposition(EntityHandle field) const


# ═══════════════════════════════════════════════════════════════
# EngineAPI — full declaration
# ═══════════════════════════════════════════════════════════════

cdef extern from "<array>" namespace "std" nogil:
    cdef cppclass ComputeUint3 "std::array<uint32_t, 3>":
        ComputeUint3()
        uint32_t& operator[](size_t)

cdef extern from "Compute/Compute.h" namespace "Shoonyakasha::Compute":
    cdef enum class ComputeBindingType "Shoonyakasha::Compute::KernelBinding::Type":
        StorageBuffer "Shoonyakasha::Compute::KernelBinding::Type::StorageBuffer"
        UniformBuffer "Shoonyakasha::Compute::KernelBinding::Type::UniformBuffer"
        StorageImage "Shoonyakasha::Compute::KernelBinding::Type::StorageImage"
        SampledImage "Shoonyakasha::Compute::KernelBinding::Type::SampledImage"

    cdef enum class ComputeBindingDim "Shoonyakasha::Compute::KernelBinding::Dim":
        Image2D
        Image3D
        Cube

    cdef cppclass ComputeKernelBinding "Shoonyakasha::Compute::KernelBinding":
        uint32_t binding
        ComputeBindingType type
        cbool readOnly
        string name
        ComputeBindingDim dim
        cbool arrayed
        string format

    cdef cppclass ComputeKernelInfo "Shoonyakasha::Compute::KernelInfo":
        vector[ComputeKernelBinding] bindings
        uint32_t pushConstantSize
        ComputeUint3 localSize

    cdef cppclass CppComputeBuffer "Shoonyakasha::Compute::Buffer":
        CppComputeBuffer()
        cbool valid()
        size_t size()
        void upload(const void* data, size_t bytes, size_t offset) except + nogil
        void download(void* out, size_t bytes, size_t offset) except + nogil

    cdef cppclass CppComputeImage "Shoonyakasha::Compute::Image":
        CppComputeImage()
        cbool valid()
        uint32_t width(uint32_t mip)
        uint32_t height(uint32_t mip)
        uint32_t depth(uint32_t mip)
        uint32_t layers()
        uint32_t mipLevels()
        const string& format()
        size_t texelSize()
        size_t byteSize(uint32_t mip)
        void upload(const void* data, size_t bytes, uint32_t mip) except + nogil
        void download(void* out, size_t bytes, uint32_t mip) except + nogil
        void generateMips() except + nogil

    cdef cppclass CppComputeBinding "Shoonyakasha::Compute::Binding":
        CppComputeBinding()
        CppComputeBinding(const CppComputeBuffer& buffer)
        CppComputeBinding(const CppComputeImage& image)
        CppComputeBinding(const CppComputeImage& image, uint32_t mip)

    cdef cppclass CppComputeKernel "Shoonyakasha::Compute::Kernel":
        CppComputeKernel()
        cbool valid()
        const ComputeKernelInfo& info() except +

    cdef cppclass CppComputeContext "Shoonyakasha::Compute::Context":
        CppComputeContext(cbool enableValidation) except +
        void shutdown()
        cbool isShutdown()
        CppComputeBuffer createBuffer(size_t bytes) except + nogil
        CppComputeImage createImage(const string& format, uint32_t width, uint32_t height, uint32_t depth,
                                    uint32_t layers, uint32_t mipLevels) except + nogil
        CppComputeKernel loadKernel(const string& spvPath) except +
        void run(const CppComputeKernel& kernel, const vector[CppComputeBinding]& bindings, ComputeUint3 groups,
                 const void* push, size_t pushBytes) except + nogil

cdef extern from "Compute/ImageFiles.h" namespace "Shoonyakasha::Compute":
    cdef enum class ExrPixelType "Shoonyakasha::Compute::ExrPixelType":
        Half
        Float

    void saveHdr(const string& path, const float* texels, uint32_t width, uint32_t height,
                 uint32_t channels) except + nogil
    void saveExr(const string& path, const float* texels, uint32_t width, uint32_t height, uint32_t channels,
                 ExrPixelType type) except + nogil
    void saveImage(const string& path, const CppComputeImage& image, uint32_t mip, uint32_t layer,
                   ExrPixelType type) except + nogil

cdef extern from "Facade/EngineAPI.h" namespace "Shoonyakasha::Facade":
    cdef cppclass Readback "Shoonyakasha::Facade::Readback":
        Readback()
        const void* data
        size_t size
        uint64_t frame
        cbool image
        uint32_t width
        uint32_t height
        string format
        uint32_t elementCount
        uint32_t elementStride

    cdef cppclass CppEngineAPI "Shoonyakasha::Facade::EngineAPI":
        CppEngineAPI(const EngineConfig& config) except +

        # Lifecycle
        void run() except + nogil
        void requestQuit()
        vec2 getFramebufferSize() const

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
        CppUIAPI& getUI() except +

        # Convenience helpers
        EntityHandle createCamera(const vec3& pos, float fov, float speed,
                                  float nearPlane, float farPlane)
        CppGltfResult loadGltfScene(const string& path, const GltfOptions& opts) except +
        pair[size_t, size_t] getTextureStreamingProgress()
        void setTextureUploadBudget(size_t bytes)
        void setTextureMemoryBudget(uint64_t bytes)
        TextureResidencyStats getTextureResidency()

        # Frame capture
        cbool captureScreenshot(const string& path) except +
        cbool setEnvironment(const string& path) except +
        cbool setEnvironmentColor(const vec3& color) except +
        cbool bakeEnvironment(const string& hdrPath, const string& outputPath) except +
        cbool startRecording(const string& path, const RecordingOptions& options) except +
        cbool stopRecording() except +
        cbool isRecording() except +
        unsigned long long getRecordedFrameCount() except +
        void setRecordingPaused(cbool paused)
        cbool isRecordingPaused() const
        void setRenderScale(float scale)
        float getRenderScale() const
        string reloadPipeline() except +
        EntityHandle createDirectionalLight(const vec3& direction,
                                            const vec3& color, float intensity)
        EntityHandle createPointLight(const vec3& position,
                                      const vec3& color, float intensity, float range)
        EntityHandle createMesh(const vector[float]& positions, const vector[uint32_t]& indices,
                                const vector[float]& normals, const vector[float]& uvs,
                                const vector[float]& colors, const vector[float]& tangents,
                                const string& name, string& error) except +
        string setMesh(EntityHandle entity, const vector[float]& positions, const vector[uint32_t]& indices,
                       const vector[float]& normals, const vector[float]& uvs,
                       const vector[float]& colors, const vector[float]& tangents) except +
        vector[EntityHandle] createPointLights(const vector[float]& positions, const vector[float]& colors,
                                               const vector[float]& intensities,
                                               const vector[float]& ranges) except +
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
        void setCustomMat4(const string& key, const mat4& value)
        void setCustomUint(const string& key, uint32_t value)
        void setCustomFloatArray(const string& key, const vector[float]& values)
        void setCustomVec2Array(const string& key, const vector[vec2]& values)
        void setCustomVec3Array(const string& key, const vector[vec3]& values)
        void setCustomVec4Array(const string& key, const vector[vec4]& values)
        void setCustomMat4Array(const string& key, const vector[mat4]& values)
        void setCustomUintArray(const string& key, const vector[uint32_t]& values)
        string setBuffer(const string& name, const void* data, size_t size, size_t offset)
        cbool requestReadback(const string& name)
        string setPipelineImageData(const string& name, const void* data, size_t size,
                                    uint32_t width, uint32_t height, const string& format, cbool mipmaps)
        cbool getReadback(const string& name, Readback& out)
        shared_ptr[CppComputeContext] compute() except +
        void setSunShadowSettings(uint32_t cascadeCount, float maxDistance,
                                  float splitLambda, uint32_t resolution, float casterExtension, uint32_t sun)
        mat4 getSunShadowCascade(uint32_t index, uint32_t sun)
        void setLocalShadowSettings(uint32_t spotCount, uint32_t pointCount,
                                    uint32_t spotResolution, uint32_t pointResolution,
                                    uint32_t atlasResolution)
        bint setPassEnabled(const string& passName, bint enabled)
        bint isPassEnabled(const string& passName)
        bint applyPipelinePreset(const string& name)
        bint setPipelineImage(const string& name, const string& path)
        bint rayQuerySupported() const
        vector[string] getPipelinePresets() const
        uint32_t getPassDrawnCount(const string& passName)
        uint32_t getPassCulledCount(const string& passName)
        uint32_t getPassOccludedCount(const string& passName)
        void setOcclusionCulling(bint enabled)
        bint isOcclusionCullingEnabled() const
        void setLodThreshold(float pixels)
        float getLodThreshold() const
        vector[uint32_t] getLodCounts() const
        void setRenderStatsEnabled(bint enabled, bint gpuTiming)
        bint isRenderStatsEnabled() const
        RenderStatsSnapshot getRenderStats() const

    # Free functions — available without an engine instance, so a script can
    # check for ffmpeg before it decides to record.
    cbool videoRecordingAvailable()
    string findFfmpeg()
    string checkPipelineJson(const string& text) except +
    string loadPipelineDocumentText(const string& path, string& error) except +


# ═══════════════════════════════════════════════════════════════
# Planet level of detail (include/Planet/PlanetLod.h)
# ═══════════════════════════════════════════════════════════════

cdef extern from "<array>" namespace "std" nogil:
    cdef cppclass LodVec3 "std::array<double, 3>":
        double& operator[](size_t)
    cdef cppclass LodVec4 "std::array<double, 4>":
        double& operator[](size_t)
    cdef cppclass LodFloat3 "std::array<float, 3>":
        float& operator[](size_t)
    cdef cppclass LodFaceColours "std::array<std::array<float, 3>, 6>":
        LodFloat3& operator[](size_t)

cdef extern from "Planet/PlanetLod.h" namespace "Shoonyakasha::Planet":
    cdef cppclass LodBody:
        int index
        LodVec3 centre
        double occluder
        cbool ocean

    cdef cppclass LodView:
        LodVec3 position
        LodVec3 forward
        double fovDegrees
        double aspect

    cdef cppclass LodNodeInfo:
        int face
        int level
        uint32_t x
        uint32_t y
        int slot
        LodVec3 centre
        double edge
        double low
        double high
        cbool measured
        LodVec3 boundCentre
        double boundRadius

    cdef cppclass LodJob:
        LodVec4 a
        LodVec4 b

    cdef cppclass CppPlanetLod "Shoonyakasha::Planet::PlanetLod":
        CppPlanetLod(int slots, int maxJobs, int verticesPerChunk, double skirtFraction, int evictFrames,
                     double mergeRatio) except +
        void createTree(int body, double radius, double low, double high, int maxLevel, double splitFactor,
                        double heightPad)
        void dropTree(int body)
        cbool hasTree(int body)
        double splitFactor(int body)
        void setSplitFactor(int body, double splitFactor)
        void update(const LodView& view, const vector[LodBody]& bodies, int64_t frame, cbool frozen, cbool requeue)
        void takeHeights(const float* rows, size_t count)
        void writeDraws(const LodView& view, const vector[LodBody]& bodies, int colourMode, double shadowReach,
                        uint32_t indexCount)
        void setFaceColours(const LodFaceColours& colours)
        const vector[LodJob]& jobs()
        void clearJobs()
        const vector[float]& instances()
        const vector[uint32_t]& chunkDraws()
        const vector[uint32_t]& oceanDraws()
        const vector[uint32_t]& shadowDraws()
        uint32_t culled()
        int slots()
        int used()
        size_t pending()
        size_t rebuildQueued()
        size_t missing(int body)
        size_t selectedCount(int body)
        vector[LodNodeInfo] selected(int body)
        vector[LodNodeInfo] nodes(int body)
        vector[pair[double, LodNodeInfo]] missingNodes(int body)
