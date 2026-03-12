# ECS Components Reference

Comprehensive reference for all Entity Component System data types, the `EntityBuilder` factory, and the `ComponentRegistry`.

All types live in the `Shoonyakasha::ECS` namespace (core/camera/physics) or the `Shoonyakasha` namespace (render/animation). A backward-compatibility alias `namespace ECS = Shoonyakasha::ECS` is provided.

**Headers:**

| Header | Contents |
|--------|----------|
| `include/ECS/Core.h` | Core components, EntityBuilder, ComponentRegistry, EntityHelper |
| `include/ECS/RenderComponents.h` | MeshComponent, MaterialComponentV5, RenderableTagComponent, SceneEnvironment |
| `include/ECS/SkeletonComponents.h` | SkeletonComponent, AnimationPlaybackComponent |
| `include/ECS/CameraController.h` | CameraControllerComponent, InputStateComponent, CameraControllerSystem |
| `include/Resources/AnimationData.h` | Joint, Skeleton, AnimationChannel, AnimationClip, AnimationInterpolation |
| `include/GPU/GPUTypes.h` | GPUBuffer, GPUTexture, MaterialParam, AlphaMode, IndexType |

---

## 1. Core Components

Defined in `include/ECS/Core.h`.

### TagComponent

A simple string tag for categorizing entities (e.g. `"Player"`, `"Enemy"`). Multiple entities can share the same tag.

```cpp
struct TagComponent {
    std::string tag = "Entity";

    TagComponent() = default;
    TagComponent(const std::string& t);
};
```

### NameComponent

A human-readable name for an entity. Unlike tags, names are typically unique but this is not enforced.

```cpp
struct NameComponent {
    std::string name = "Unnamed";

    NameComponent() = default;
    NameComponent(const std::string& n);
};
```

### HierarchyComponent

Establishes parent-child relationships between entities, forming a scene graph tree. Child transforms inherit their parent's world matrix.

```cpp
struct HierarchyComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children;

    void addChild(entt::entity child);     // Adds child (no-op if already present)
    void removeChild(entt::entity child);  // Removes child from the list
};
```

### TransformComponent

Position, rotation, and scale in 3D space. The transform system computes `localMatrix` and `worldMatrix` each frame when `isDirty` is true. Parent-child hierarchies propagate world matrices downward.

```cpp
struct TransformComponent {
    glm::vec3 position{0.0f};      // World/local position
    glm::vec3 rotation{0.0f};      // Euler angles in radians (pitch, yaw, roll)
    glm::vec3 scale{1.0f};         // Scale factor per axis

    glm::mat4 localMatrix{1.0f};   // Computed: T * R * S
    glm::mat4 worldMatrix{1.0f};   // Computed: parent.worldMatrix * localMatrix
    bool isDirty = true;           // Set true when position/rotation/scale change

    TransformComponent() = default;
    TransformComponent(const glm::vec3& pos);
    TransformComponent(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scl);

    glm::mat4 getLocalMatrix() const;  // Recomputes T * R * S on the fly
    glm::vec3 getForward() const;      // -Z direction from rotation
    glm::vec3 getRight() const;        // +X direction from rotation
    glm::vec3 getUp() const;           // +Y direction from rotation
};
```

**Rotation order:** Y (yaw) then X (pitch) then Z (roll). This is the standard FPS camera order that prevents the "tilted horizon" problem.

**Coordinate convention:** Vulkan-style: -Z is forward, +X is right, +Y is up.

### ActiveComponent

Enables/disables an entity without destroying it. Systems can check this to skip inactive entities.

```cpp
struct ActiveComponent {
    bool active = true;
};
```

### LifetimeComponent

Marks an entity for automatic destruction after a timer expires. Useful for particles, projectiles, and temporary effects.

```cpp
struct LifetimeComponent {
    float timeToLive;            // Seconds remaining (no default -- must be provided)
    bool destroyOnExpire = true; // If false, entity stays but timer reaches zero

    LifetimeComponent(float ttl);
};
```

---

## 2. Camera Components

### CameraComponent

Defined in `include/ECS/Core.h`.

Configures a camera's projection. Attach to an entity that also has a `TransformComponent` to define a viewpoint into the scene.

```cpp
struct CameraComponent {
    enum Type { Perspective, Orthographic };

    Type  type         = Perspective;
    float fov          = 45.0f;           // Field of view in degrees (Perspective only)
    float nearPlane    = 0.1f;
    float farPlane     = 1000.0f;
    float aspectRatio  = 16.0f / 9.0f;
    float orthoSize    = 10.0f;           // Half-height in world units (Orthographic only)

    glm::mat4 viewMatrix{1.0f};           // Computed by CameraSystem
    glm::mat4 projectionMatrix{1.0f};     // Computed by CameraSystem
    bool isMainCamera = false;            // The active rendering camera

    glm::mat4 getProjectionMatrix() const; // Recomputes projection on the fly
};
```

`getProjectionMatrix()` returns `glm::perspective(...)` for `Perspective` type or `glm::ortho(...)` for `Orthographic` type, based on the current field values.

### CameraControllerComponent

Defined in `include/ECS/CameraController.h`.

Configures how a camera responds to keyboard and mouse input. Four modes provide different spatial relationships.

```cpp
struct CameraControllerComponent {
    // ── Mode ──────────────────────────────────────────────────────
    enum class Mode { Free, Orbit, FirstPerson, ThirdPerson };

    Mode mode    = Mode::Free;
    bool enabled = true;

    // ── Movement ──────────────────────────────────────────────────
    float moveSpeed         = 5.0f;      // Units per second
    float sprintMultiplier  = 2.5f;      // Multiplier when holding sprint key
    float rotationSpeed     = 2.0f;      // Radians per second (keyboard rotation)
    float mouseSensitivity  = 0.003f;    // Radians per pixel
    float scrollSensitivity = 2.0f;      // Units per scroll tick
    float smoothing         = 10.0f;     // Interpolation factor (higher = snappier)

    // ── Orbit Mode ────────────────────────────────────────────────
    glm::vec3 orbitTarget{0.0f};         // Point to orbit around
    float orbitDistance      = 10.0f;    // Distance from target
    float orbitYaw           = 0.0f;     // Horizontal angle (radians)
    float orbitPitch         = 0.3f;     // Vertical angle (radians)
    float orbitMinDistance   = 1.0f;
    float orbitMaxDistance   = 100.0f;
    float orbitMinPitch      = -1.5f;    // Radians
    float orbitMaxPitch      = 1.5f;     // Radians
    bool  orbitAutoRotate    = false;
    float orbitAutoRotateSpeed = 0.3f;   // Radians per second

    // ── Free Mode ─────────────────────────────────────────────────
    bool horizonLocked = true;           // true = FPS-style, false = full 6DOF flight

    // ── First Person Mode ─────────────────────────────────────────
    float eyeHeight     = 1.7f;          // Camera height from ground
    float groundY       = 0.0f;          // Ground plane Y coordinate
    bool  constrainPitch = true;
    float maxPitch      = 1.5f;          // Max vertical look angle (radians)

    // ── Third Person Mode ─────────────────────────────────────────
    entt::entity followTarget = entt::null;
    glm::vec3 followOffset{0.0f, 2.0f, 5.0f};
    float followLag = 5.0f;              // How quickly camera catches up

    // ── Internal State (managed by system) ────────────────────────
    glm::vec3 velocity{0.0f};
    glm::vec2 angularVelocity{0.0f};     // (yaw, pitch)
    float currentYaw   = 0.0f;
    float currentPitch = 0.0f;
    bool  initialized  = false;

    // ── Key Bindings ──────────────────────────────────────────────
    int keyForward       = GLFW_KEY_W;
    int keyBackward      = GLFW_KEY_S;
    int keyLeft          = GLFW_KEY_A;
    int keyRight         = GLFW_KEY_D;
    int keyUp            = GLFW_KEY_E;
    int keyDown          = GLFW_KEY_Q;
    int keySprint        = GLFW_KEY_LEFT_SHIFT;
    int keyCaptureMouse  = GLFW_KEY_ESCAPE;
    int mouseButtonLook  = GLFW_MOUSE_BUTTON_RIGHT;
    int mouseButtonPan   = GLFW_MOUSE_BUTTON_MIDDLE;

    // ── Static Factories ──────────────────────────────────────────
    static CameraControllerComponent createFreeCamera(float speed = 5.0f);
    static CameraControllerComponent createOrbitCamera(
        const glm::vec3& target = glm::vec3(0.0f),
        float distance = 10.0f,
        bool autoRotate = false);
    static CameraControllerComponent createFirstPersonCamera(float eyeHeight = 1.7f);
    static CameraControllerComponent createThirdPersonCamera(
        entt::entity target,
        const glm::vec3& offset = glm::vec3(0.0f, 2.0f, 5.0f));
};
```

### InputStateComponent

Defined in `include/ECS/CameraController.h`.

Tracks continuous input device state across frames. Typically a singleton entity -- one per scene.

```cpp
struct InputStateComponent {
    std::array<bool, GLFW_KEY_LAST + 1> keys{};         // Keyboard state
    glm::vec2 mousePosition{0.0f};
    glm::vec2 lastMousePosition{0.0f};
    glm::vec2 mouseDelta{0.0f};
    glm::vec2 scrollDelta{0.0f};
    std::array<bool, GLFW_MOUSE_BUTTON_LAST + 1> mouseButtons{};
    bool mouseCaptured    = false;
    bool firstMouseCapture = true;

    bool isKeyDown(int key) const;
    bool isMouseButtonDown(int button) const;
    void beginFrame();   // Call at start of frame to compute deltas
    void endFrame();     // Call at end of frame to reset scroll
};
```

---

## 3. Light Component

Defined in `include/ECS/Core.h`.

### LightComponent

Configures a light source. Attach alongside a `TransformComponent` -- the transform's position and forward direction determine where the light shines.

```cpp
struct LightComponent {
    enum Type { Directional, Point, Spot };

    Type      type      = Point;
    glm::vec3 color{1.0f};              // RGB color
    float     intensity = 1.0f;

    // Point and Spot light range
    float range     = 10.0f;

    // Attenuation coefficients: 1 / (constant + linear*d + quadratic*d^2)
    float constant  = 1.0f;
    float linear    = 0.09f;
    float quadratic = 0.032f;

    // Spot light cone angles (degrees)
    float innerCone = 30.0f;
    float outerCone = 45.0f;

    // Shadow casting
    bool     castShadows   = false;
    uint32_t shadowMapSize = 1024;       // Shadow map resolution (pixels)
};
```

| Type | Uses position? | Uses direction? | Cone angles? |
|------|:-:|:-:|:-:|
| Directional | No | Yes (transform forward) | No |
| Point | Yes | No | No |
| Spot | Yes | Yes (transform forward) | Yes |

---

## 4. Physics Components

Defined in `include/ECS/Core.h`.

### RigidBodyComponent

Describes the physics simulation properties of an entity. The Bullet3 physics system reads these fields and manages the internal `bulletRigidBody` pointer.

```cpp
struct RigidBodyComponent {
    enum Type { Static, Kinematic, Dynamic };

    Type  type            = Dynamic;
    float mass            = 1.0f;        // kg (ignored for Static)
    glm::vec3 velocity{0.0f};           // Linear velocity (m/s)
    glm::vec3 angularVelocity{0.0f};    // Angular velocity (rad/s)
    float drag            = 0.1f;        // Linear drag
    float angularDrag     = 0.1f;        // Angular drag

    bool useGravity       = true;
    bool isKinematic      = false;
    bool freezeRotation   = false;       // Lock all rotational axes

    void* bulletRigidBody = nullptr;     // Opaque Bullet3 pointer (internal)
};
```

### ColliderComponent

Defines the collision shape attached to an entity. Combined with `RigidBodyComponent` for full physics simulation, or used standalone for trigger volumes.

```cpp
struct ColliderComponent {
    enum Shape { Box, Sphere, Capsule, Mesh, Plane };

    Shape     shape   = Box;
    glm::vec3 size{1.0f};       // Box: half-extents; Sphere: radius in x; Capsule: radius in x, height in y
    glm::vec3 center{0.0f};     // Local offset from entity origin

    bool  isTrigger   = false;  // true = no physics response, only overlap events
    float friction    = 0.5f;
    float restitution = 0.0f;   // Bounciness (0 = no bounce, 1 = perfect bounce)

    void* bulletShape = nullptr; // Opaque Bullet3 pointer (internal)
};
```

---

## 5. Render Components

Defined in `include/ECS/RenderComponents.h`. These are in the `Shoonyakasha` namespace (not `ECS`).

### MeshComponent

Holds GPU vertex and index buffers for a piece of renderable geometry. Populated by the glTF loader or manually during mesh construction.

```cpp
struct MeshComponent {
    GPUBuffer vertexBuffer;              // Vertex data on GPU
    GPUBuffer indexBuffer;               // Index data on GPU (optional)

    IndexType indexType   = IndexType::UInt32;  // UInt16 or UInt32
    uint32_t  vertexCount = 0;
    uint32_t  indexCount  = 0;
    uint32_t  vertexStride = 0;          // Bytes per vertex

    bool hasIndices() const;   // true if indexBuffer is valid AND indexCount > 0
    bool isValid() const;      // true if vertexBuffer is valid AND vertexCount > 0
};
```

### MaterialComponentV5

A generic material using string-keyed parameter and texture maps. The dot-path resolver reads values by name at render time (e.g. `entity.material.params.baseColorFactor`).

```cpp
struct MaterialComponentV5 {
    // Generic parameter map (baseColorFactor, metallicFactor, roughnessFactor, etc.)
    std::unordered_map<std::string, MaterialParam> params;

    // Texture slots (albedoMap, normalMap, metallicRoughnessMap, aoMap, emissiveMap, etc.)
    std::unordered_map<std::string, GPUTexture> textures;

    // Transparency
    AlphaMode alphaMode   = AlphaMode::Opaque;   // Opaque, Mask, or Blend
    float     alphaCutoff = 0.5f;                 // Discard threshold for Mask mode

    // Rasterization
    bool doubleSided = false;

    // ── Parameter access ──────────────────────────────────────────
    template<typename T>
    T getParam(const std::string& name, const T& defaultValue = T{}) const;

    template<typename T>
    void setParam(const std::string& name, const T& value);

    bool hasParam(const std::string& name) const;

    // ── Texture access ────────────────────────────────────────────
    const GPUTexture* getTexture(const std::string& name) const;  // nullptr if absent
    GPUTexture*       getTexture(const std::string& name);
    bool hasTexture(const std::string& name) const;  // true only if exists AND is a real texture

    // ── Sorting helpers ───────────────────────────────────────────
    bool isOpaque() const;          // alphaMode == Opaque
    bool isMasked() const;          // alphaMode == Mask
    bool isTransparent() const;     // alphaMode == Blend
    bool isOpaqueOrMasked() const;  // alphaMode != Blend
};
```

**Common parameter names** (PBR workflow):

| Parameter | Type | Typical Default |
|-----------|------|-----------------|
| `baseColorFactor` | `vec4` | `(1, 1, 1, 1)` |
| `metallicFactor` | `float` | `1.0` |
| `roughnessFactor` | `float` | `1.0` |
| `emissiveFactor` | `vec3` | `(0, 0, 0)` |
| `normalScale` | `float` | `1.0` |
| `occlusionStrength` | `float` | `1.0` |

**Common texture slot names:**

| Slot | Purpose |
|------|---------|
| `albedoMap` | Base color / diffuse texture |
| `normalMap` | Tangent-space normal map |
| `metallicRoughnessMap` | Metallic (B) + Roughness (G) packed |
| `aoMap` | Ambient occlusion |
| `emissiveMap` | Emissive glow texture |

### RenderableTagComponent

Marker component that makes an entity eligible for rendering. An entity needs all four of `MeshComponent`, `MaterialComponentV5`, `TransformComponent`, and `RenderableTagComponent` to be drawn by the frame graph.

```cpp
struct RenderableTagComponent {
    bool     visible        = true;
    bool     castShadows    = true;
    bool     receiveShadows = true;
    uint8_t  renderLayerMask = 0xFF;  // Bitfield, 8 layers
    uint32_t sortKey        = 0;      // Lower = rendered first (useful for batching)

    bool shouldRender() const;  // Returns visible
};
```

### SceneEnvironment

Scene-wide environment data for Image-Based Lighting (IBL). This is **not** a per-entity ECS component -- it is a singleton or scene-level object accessed via `scene.environment.*` dot-paths.

```cpp
struct SceneEnvironment {
    GPUTexture irradianceMap;    // Diffuse IBL (low-res convolved cubemap)
    GPUTexture prefilterMap;     // Specular IBL (mip-mapped roughness cubemap)
    GPUTexture brdfLUT;          // BRDF integration lookup texture
    GPUTexture environmentMap;   // Original HDR environment map (for skybox)

    bool hasIBL() const;  // true if irradianceMap, prefilterMap, AND brdfLUT are all valid
};
```

---

## 6. Animation Components

### SkeletonComponent

Defined in `include/ECS/SkeletonComponents.h` (namespace `Shoonyakasha`).

Attached to every skinned mesh entity. Multiple entities can share the same `Skeleton` (via `shared_ptr`), but each gets its own `boneMatrices` and `boneSSBO` because they may be at different animation frames.

```cpp
struct SkeletonComponent {
    std::shared_ptr<Skeleton> skeleton;       // Shared skeleton definition
    std::vector<glm::mat4>    boneMatrices;   // Final bone matrices per frame
    GPUBuffer                 boneSSBO;       // GPU storage buffer for shader upload
    bool                      dirty = true;   // Needs re-upload to GPU

    void     allocate();          // Resize boneMatrices to skeleton->jointCount()
    uint32_t jointCount() const;  // skeleton ? skeleton->jointCount() : 0
    uint32_t ssboSize() const;    // jointCount() * sizeof(glm::mat4)
};
```

The `boneMatrices` array is computed each frame by the `SkeletalAnimationSystem`:

```
boneMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
```

### AnimationPlaybackComponent

Defined in `include/ECS/SkeletonComponents.h` (namespace `Shoonyakasha`).

Controls animation playback for a skinned mesh. The `SkeletalAnimationSystem` reads this component, advances time, evaluates keyframes, and writes results into `SkeletonComponent::boneMatrices`.

```cpp
struct AnimationPlaybackComponent {
    std::vector<std::shared_ptr<AnimationClip>> clips;  // Available animation clips

    int   currentClipIndex = -1;       // -1 = no clip selected
    float currentTime      = 0.0f;     // Current playback position (seconds)
    float speed            = 1.0f;     // Playback speed multiplier
    bool  playing          = false;
    bool  loop             = true;

    // Per-joint intermediate transforms for the current frame
    std::vector<glm::vec3> jointTranslations;   // Sized to jointCount
    std::vector<glm::quat> jointRotations;      // Sized to jointCount
    std::vector<glm::vec3> jointScales;          // Sized to jointCount

    void allocate(uint32_t jointCount);  // Pre-allocate intermediate arrays
    void play(int clipIndex);            // Start playing a clip (resets time)
    void stop();                         // Stop and reset time to 0

    const AnimationClip* getCurrentClip() const;  // nullptr if not playing
};
```

---

## 7. Animation Data Types

Defined in `include/Resources/AnimationData.h` (namespace `Shoonyakasha`).

These are pure data structures that describe a skeleton and its animations. They are loaded from glTF files and shared across entities.

### Joint

A single bone in a skeleton hierarchy.

```cpp
struct Joint {
    std::string name;
    int parentIndex = -1;                    // -1 = root joint (no parent)
    glm::mat4 inverseBindMatrix = glm::mat4(1.0f);  // Mesh space -> joint local space

    // Default local transform (used when no animation channel targets this joint)
    glm::vec3 defaultTranslation = glm::vec3(0.0f);
    glm::quat defaultRotation    = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity (w,x,y,z)
    glm::vec3 defaultScale       = glm::vec3(1.0f);
};
```

### Skeleton

Complete bone hierarchy for a skinned mesh.

```cpp
struct Skeleton {
    std::string name;
    std::vector<Joint> joints;
    int rootJointIndex = 0;

    // Fast lookup: joint name -> index in joints vector
    std::unordered_map<std::string, int> jointNameToIndex;

    uint32_t jointCount() const;          // joints.size()
    void buildNameLookup();               // Rebuild jointNameToIndex from joints
    int findJoint(const std::string& name) const;  // -1 if not found
};
```

### AnimationInterpolation

How values are interpolated between keyframes.

```cpp
enum class AnimationInterpolation {
    Step,         // Constant value until next keyframe (no interpolation)
    Linear,       // Linear interpolation (lerp for vectors, slerp for quaternions)
    CubicSpline   // Hermite cubic spline with in/out tangents
};
```

### AnimationChannel

Keyframed data targeting one property of one joint.

```cpp
struct AnimationChannel {
    int targetJointIndex = -1;

    enum class Property { Translation, Rotation, Scale };
    Property property = Property::Translation;

    AnimationInterpolation interpolation = AnimationInterpolation::Linear;

    std::vector<float>     timestamps;  // Keyframe times (seconds, ascending)
    std::vector<glm::vec4> values;      // Keyframe values (see encoding below)

    uint32_t keyframeCount() const;     // timestamps.size()
};
```

**Value encoding:**

| Property | Layout | Notes |
|----------|--------|-------|
| Translation | `vec4(x, y, z, 0)` | Position offset |
| Rotation | `vec4(x, y, z, w)` | Quaternion |
| Scale | `vec4(x, y, z, 0)` | Scale factor |

For `CubicSpline` interpolation, values are stored as `[inTangent, value, outTangent]` triples, so `values.size() == timestamps.size() * 3`.

### AnimationClip

A named animation sequence (e.g. "Walk", "Idle", "Attack").

```cpp
struct AnimationClip {
    std::string name;
    float duration = 0.0f;               // Max timestamp across all channels (seconds)
    std::vector<AnimationChannel> channels;
};
```

---

## 8. EntityBuilder

Defined in `include/ECS/Core.h`.

A fluent factory for creating entities with a builder pattern. Automatically adds `TransformComponent` and `ActiveComponent` if not explicitly provided.

```cpp
class EntityBuilder {
public:
    EntityBuilder(entt::registry& registry);   // Creates a new entity immediately

    // Generic: attach any component with constructor args
    template<typename T, typename... Args>
    EntityBuilder& with(Args&&... args);

    // Convenience methods
    EntityBuilder& withName(const std::string& name);
    EntityBuilder& withTag(const std::string& tag);
    EntityBuilder& withTransform(const glm::vec3& pos = {0,0,0},
                                 const glm::vec3& rot = {0,0,0},
                                 const glm::vec3& scale = {1,1,1});
    EntityBuilder& withParent(entt::entity parent);
    EntityBuilder& withCamera(bool isMain = false);
    EntityBuilder& withLight(LightComponent::Type type,
                             const glm::vec3& color = {1,1,1},
                             float intensity = 1.0f);
    EntityBuilder& withRigidBody(RigidBodyComponent::Type type = RigidBodyComponent::Dynamic,
                                 float mass = 1.0f);
    EntityBuilder& withCollider(ColliderComponent::Shape shape,
                                const glm::vec3& size = {1,1,1});
    EntityBuilder& withLifetime(float seconds);

    // Camera controller convenience (implemented after CameraController.h is included)
    EntityBuilder& withFreeCameraController(float moveSpeed = 5.0f);
    EntityBuilder& withOrbitCameraController(const glm::vec3& target = glm::vec3(0.0f),
                                             float distance = 10.0f,
                                             bool autoRotate = false);
    EntityBuilder& withFirstPersonController(float eyeHeight = 1.7f);
    EntityBuilder& withThirdPersonController(entt::entity target,
                                             const glm::vec3& offset = glm::vec3(0.0f, 2.0f, 5.0f));

    entt::entity build();  // Finalize and return the entity handle

private:
    entt::registry& m_registry;
    entt::entity    m_entity;
};
```

### Usage example

```cpp
auto camera = ECS::EntityBuilder(registry)
    .withName("MainCamera")
    .withTag("Camera")
    .withTransform({0, 2, 10})
    .withCamera(true)
    .withFreeCameraController(8.0f)
    .build();

auto light = ECS::EntityBuilder(registry)
    .withName("Sun")
    .withTransform({0, 50, 0}, {-0.8f, 0, 0})
    .withLight(ECS::LightComponent::Directional, {1, 0.95f, 0.9f}, 2.0f)
    .build();

auto box = ECS::EntityBuilder(registry)
    .withName("PhysicsBox")
    .withTransform({0, 5, 0})
    .withRigidBody(ECS::RigidBodyComponent::Dynamic, 10.0f)
    .withCollider(ECS::ColliderComponent::Box, {1, 1, 1})
    .withLifetime(30.0f)
    .build();
```

### What `build()` guarantees

- If no `TransformComponent` was added, one is emplaced with default values.
- If no `ActiveComponent` was added, one is emplaced with `active = true`.

### Using `with<T>()` for custom components

The generic `with<T>(args...)` method can attach any component type, including your own custom structs:

```cpp
auto entity = ECS::EntityBuilder(registry)
    .withName("SkinnedCharacter")
    .with<MeshComponent>()
    .with<MaterialComponentV5>()
    .with<RenderableTagComponent>()
    .with<SkeletonComponent>()
    .with<AnimationPlaybackComponent>()
    .build();
```

---

## 9. ComponentRegistry

Defined in `include/ECS/Core.h`.

A runtime registry that allows creating, removing, and checking components by string name. This is primarily used by the Python binding layer (Cython/Facade) to manipulate components without compile-time type information.

```cpp
class ComponentRegistry {
public:
    using ComponentFactory = std::function<void(entt::registry&, entt::entity)>;
    using ComponentRemover = std::function<void(entt::registry&, entt::entity)>;
    using ComponentChecker = std::function<bool(const entt::registry&, entt::entity)>;

    // Register a component type with a string name
    template<typename T>
    void registerComponent(const std::string& name);

    // Create a default-constructed component on an entity (returns false if name unknown)
    bool createComponent(const std::string& name, entt::registry& registry, entt::entity entity);

    // Remove a component from an entity (returns false if name unknown)
    bool removeComponent(const std::string& name, entt::registry& registry, entt::entity entity);

    // Check if an entity has a component (returns false if name unknown)
    bool hasComponent(const std::string& name, const entt::registry& registry, entt::entity entity) const;

    // Reverse lookup: get the registered name for a C++ type
    const std::string& getComponentName(std::type_index type) const;

    // List all registered component names
    std::vector<std::string> getAllComponentNames() const;
};
```

### Pre-registered components

The function `registerAllComponents()` in `Core.h` registers the core set:

| String Name | C++ Type |
|-------------|----------|
| `"Tag"` | `TagComponent` |
| `"Name"` | `NameComponent` |
| `"Hierarchy"` | `HierarchyComponent` |
| `"Transform"` | `TransformComponent` |
| `"Camera"` | `CameraComponent` |
| `"Light"` | `LightComponent` |
| `"RigidBody"` | `RigidBodyComponent` |
| `"Collider"` | `ColliderComponent` |
| `"Active"` | `ActiveComponent` |

The function `registerCameraControllerComponents()` in `CameraController.h` adds:

| String Name | C++ Type |
|-------------|----------|
| `"InputState"` | `InputStateComponent` |
| `"CameraController"` | `CameraControllerComponent` |

### Usage example

```cpp
ECS::ComponentRegistry registry;
ECS::registerAllComponents(registry);
ECS::registerCameraControllerComponents(registry);

// List all registered components
for (const auto& name : registry.getAllComponentNames()) {
    std::cout << name << "\n";
}

// Dynamic component manipulation
entt::registry ecsRegistry;
auto entity = ecsRegistry.create();

registry.createComponent("Transform", ecsRegistry, entity);
registry.createComponent("Camera", ecsRegistry, entity);

if (registry.hasComponent("Camera", ecsRegistry, entity)) {
    // Entity has a camera
}

registry.removeComponent("Camera", ecsRegistry, entity);
```

---

## 10. EntityHelper

Defined in `include/ECS/Core.h`.

Static utility methods for common entity operations. These are convenience wrappers around direct EnTT registry access.

```cpp
class EntityHelper {
public:
    // Name access
    static std::string getName(const entt::registry& registry, entt::entity entity);
    static void setName(entt::registry& registry, entt::entity entity, const std::string& name);

    // World position (reads from worldMatrix[3])
    static glm::vec3 getWorldPosition(const entt::registry& registry, entt::entity entity);
    static void setWorldPosition(entt::registry& registry, entt::entity entity, const glm::vec3& position);

    // Entity destruction (recursively destroys children, removes from parent)
    static void destroyEntity(entt::registry& registry, entt::entity entity);

    // Tag queries
    static std::vector<entt::entity> findEntitiesWithTag(const entt::registry& registry, const std::string& tag);
    static entt::entity findFirstWithTag(const entt::registry& registry, const std::string& tag);

    // Active state
    static bool isActive(const entt::registry& registry, entt::entity entity);
    static void setActive(entt::registry& registry, entt::entity entity, bool active);
};
```

`setWorldPosition()` handles parent-child hierarchies correctly -- if the entity has a parent, it converts the world position to local space using the parent's inverse world matrix.

`destroyEntity()` recursively destroys all children in the hierarchy before destroying the entity itself, and removes the entity from its parent's children list.

---

## GPU Support Types

These types from `include/GPU/GPUTypes.h` are referenced by render and animation components.

### GPUBuffer

Thin wrapper around a Vulkan buffer and its VMA allocation.

```cpp
struct GPUBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkDeviceSize  size       = 0;

    bool isValid() const;   // buffer != VK_NULL_HANDLE
    void reset();           // Sets all fields to null/zero (does NOT free GPU memory)
};
```

### GPUTexture

A complete texture bundle with image, view, and sampler -- ready for shader binding.

```cpp
struct GPUTexture {
    VkImage       image      = VK_NULL_HANDLE;
    VkImageView   view       = VK_NULL_HANDLE;
    VkSampler     sampler    = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat      format     = VK_FORMAT_UNDEFINED;
    uint32_t      width      = 0;
    uint32_t      height     = 0;
    uint32_t      mipLevels  = 1;
    bool          exists     = false;   // true if loaded from real data (not a fallback)

    bool isValid() const;   // All three handles (image, view, sampler) are non-null
    void reset();           // Sets all fields to defaults (does NOT free GPU memory)
};
```

### MaterialParam

Type-safe parameter storage for material values. Stores up to 64 bytes (enough for `glm::mat4`).

```cpp
struct MaterialParam {
    enum class Type : uint8_t {
        Float, Vec2, Vec3, Vec4, Mat3, Mat4, Int, UInt
    };

    Type type = Type::Float;
    std::array<uint8_t, 64> data = {};

    template<typename T> T as() const;         // Extract typed value
    static MaterialParam from(float v);        // Factory methods for each type
    static MaterialParam from(const glm::vec2& v);
    static MaterialParam from(const glm::vec3& v);
    static MaterialParam from(const glm::vec4& v);
    static MaterialParam from(const glm::mat3& m);
    static MaterialParam from(const glm::mat4& m);
    static MaterialParam from(int32_t v);
    static MaterialParam from(uint32_t v);

    size_t byteSize() const;                   // Size of the stored type in bytes
    const void* rawData() const;
    void* rawData();
};
```

### AlphaMode

```cpp
enum class AlphaMode : uint8_t {
    Opaque = 0,   // Fully opaque, no alpha testing
    Mask   = 1,   // Discard fragments below alphaCutoff threshold
    Blend  = 2    // Alpha blending (requires back-to-front sorting)
};
```

### IndexType

```cpp
enum class IndexType : uint8_t {
    UInt16 = 0,
    UInt32 = 1
};
```
