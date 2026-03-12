# EngineAPI

`Shoonyakasha::Facade::EngineAPI` -- top-level engine lifecycle, callback registration, and convenience helpers.

> **Facade layer** -- this is the same API that Python wraps via Cython. All Vulkan/EnTT internals are hidden behind PIMPL. For the inheritance-based alternative, see [ApplicationBase](application-base.md).

**Header:** `#include "Facade/EngineAPI.h"`
**Namespace:** `Shoonyakasha::Facade`

**See also:** [Python Engine API](../python/engine.md)

---

## Callback Type Aliases

Defined in `Facade/FacadeTypes.h`:

```cpp
using VoidCallback   = std::function<void()>;
using UpdateCallback = std::function<void(float dt)>;
using KeyCallback    = std::function<void(int keyCode)>;
using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
```

| Alias | Signature | Used By |
|-------|-----------|---------|
| `VoidCallback` | `void()` | `setOnInit`, `setOnPostInit`, `setOnPostRender`, `setOnCleanup` |
| `UpdateCallback` | `void(float dt)` | `setOnUpdate`, `setOnPreRender` |
| `KeyCallback` | `void(int keyCode)` | `setOnKeyPressed` |
| `ResizeCallback` | `void(uint32_t width, uint32_t height)` | `setOnResize` |

---

## EngineConfig

Defined in `Facade/FacadeTypes.h`. Configuration struct passed to the `EngineAPI` constructor.

```cpp
struct EngineConfig {
    int width  = 1600;
    int height = 900;
    std::string title   = "Shoonyakasha Application";
    std::string logFile = "application.log";
    int logLevel = 1;   // 0=Debug, 1=Info, 2=Warning, 3=Error

    std::string hdrEnvironmentPath;   // empty = no IBL
    std::string pipelineJsonPath;     // Required -- JSON render graph

    uint32_t maxFramesInFlight = 2;

    // Render graph parameters (SSBO sizing, dispatch counts, etc.)
    std::vector<std::pair<std::string, uint32_t>> renderGraphParameters;
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `width` | `int` | `1600` | Window width in pixels. |
| `height` | `int` | `900` | Window height in pixels. |
| `title` | `std::string` | `"Shoonyakasha Application"` | Window title bar text. |
| `logFile` | `std::string` | `"application.log"` | Path to the log output file. |
| `logLevel` | `int` | `1` | Log verbosity: 0=Debug, 1=Info, 2=Warning, 3=Error. |
| `hdrEnvironmentPath` | `std::string` | `""` (empty) | Path to an HDR environment map for IBL. Empty disables IBL. |
| `pipelineJsonPath` | `std::string` | `""` (empty) | **Required.** Path to the JSON render graph definition. |
| `maxFramesInFlight` | `uint32_t` | `2` | Number of Vulkan frames in flight (double/triple buffering). |
| `renderGraphParameters` | `std::vector<std::pair<std::string, uint32_t>>` | `{}` | Key-value pairs injected into the render graph (e.g. SSBO sizes). |

---

## Constructor / Destructor

### EngineAPI (constructor)

```cpp
explicit EngineAPI(const EngineConfig& config);
```

Construct the engine from a configuration struct. Creates the window, Vulkan device, and all internal subsystems. Sub-APIs (`getScene()`, `getInput()`, `getPhysics()`) are valid immediately after construction, but the ECS scene is not fully populated until `onInit` fires.

| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const EngineConfig&` | Engine configuration. `pipelineJsonPath` is required. |

The class is **non-copyable and non-movable**.

### ~EngineAPI (destructor)

```cpp
~EngineAPI();
```

Tears down all engine resources (Vulkan device, window, ECS scene, physics world).

---

## Lifecycle

### run

```cpp
void run();
```

Enter the main loop. **Blocks** until the window is closed. Callbacks registered via `setOn*` methods fire during execution.

---

## Callback Registration

Register callbacks before or during `run()`. Each setter replaces any previously registered callback for that slot.

### setOnInit

```cpp
void setOnInit(VoidCallback cb);
```

Called once after all engine systems are initialized and the render graph is compiled. This is the place to load scenes, create entities, and set up initial state.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `VoidCallback` | `void()` callback. |

### setOnPostInit

```cpp
void setOnPostInit(VoidCallback cb);
```

Called once after `onInit` completes and the first frame is about to begin. Use for deferred setup that depends on entities created in `onInit`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `VoidCallback` | `void()` callback. |

### setOnUpdate

```cpp
void setOnUpdate(UpdateCallback cb);
```

Called every frame with the delta time. Use for game logic, input handling, and entity manipulation.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `UpdateCallback` | `void(float dt)` callback. `dt` is seconds since last frame. |

### setOnPreRender

```cpp
void setOnPreRender(UpdateCallback cb);
```

Called every frame after `onUpdate` and before GPU rendering begins. Use for last-minute uniform updates or visibility changes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `UpdateCallback` | `void(float dt)` callback. |

### setOnPostRender

```cpp
void setOnPostRender(VoidCallback cb);
```

Called every frame after GPU rendering completes and before the frame is presented.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `VoidCallback` | `void()` callback. |

### setOnKeyPressed

```cpp
void setOnKeyPressed(KeyCallback cb);
```

Called when a key-press event fires. The key code uses GLFW constants.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `KeyCallback` | `void(int keyCode)` callback. |

### setOnResize

```cpp
void setOnResize(ResizeCallback cb);
```

Called when the window is resized.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `ResizeCallback` | `void(uint32_t width, uint32_t height)` callback. |

### setOnCleanup

```cpp
void setOnCleanup(VoidCallback cb);
```

Called once when the engine is shutting down, before resources are destroyed. Use for saving state or final cleanup.

| Parameter | Type | Description |
|-----------|------|-------------|
| `cb` | `VoidCallback` | `void()` callback. |

---

## Sub-API Access

Valid after construction. The referenced objects remain alive for the lifetime of the `EngineAPI`.

### getScene

```cpp
SceneAPI& getScene();
```

Returns a reference to the scene/entity API.

**Returns:** `SceneAPI&` -- the scene sub-API.

### getInput

```cpp
InputAPI& getInput();
```

Returns a reference to the input polling/callback API.

**Returns:** `InputAPI&` -- the input sub-API.

### getPhysics

```cpp
PhysicsAPI& getPhysics();
```

Returns a reference to the physics simulation API.

**Returns:** `PhysicsAPI&` -- the physics sub-API.

---

## Convenience Helpers

### createCamera

```cpp
EntityHandle createCamera(const glm::vec3& pos,
                           float fov       = 60.f,
                           float speed     = 8.f,
                           float nearPlane = 0.1f,
                           float farPlane  = 1000.f);
```

Create a camera entity with a controller component, perspective projection, and the given position.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pos` | `const glm::vec3&` | -- | Initial world position. |
| `fov` | `float` | `60.f` | Vertical field of view in degrees. |
| `speed` | `float` | `8.f` | Camera movement speed (units/second). |
| `nearPlane` | `float` | `0.1f` | Near clipping plane distance. |
| `farPlane` | `float` | `1000.f` | Far clipping plane distance. |

**Returns:** `EntityHandle` -- the newly created camera entity.

### loadGltfScene

```cpp
GltfResult loadGltfScene(const std::string& path,
                          const GltfOptions& opts = {});
```

Load a glTF 2.0 file (.gltf or .glb) into the active ECS scene. Creates entities, meshes, materials, textures, skeletons, and animation clips as configured by `opts`.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `const std::string&` | -- | File system path to the glTF file. |
| `opts` | `const GltfOptions&` | `{}` | Loading options (see [GltfOptions](#gltfoptions) below). |

**Returns:** `GltfResult` -- result struct containing success status, entity handles, and statistics. See [GltfResult](#gltfresult) below.

### createDirectionalLight

```cpp
EntityHandle createDirectionalLight(const glm::vec3& direction,
                                     const glm::vec3& color     = glm::vec3(1.f),
                                     float intensity             = 2.f);
```

Create a directional light entity (e.g. sunlight).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `direction` | `const glm::vec3&` | -- | Light direction vector (will be normalized). |
| `color` | `const glm::vec3&` | `glm::vec3(1.f)` | RGB color (linear space). |
| `intensity` | `float` | `2.f` | Light intensity multiplier. |

**Returns:** `EntityHandle` -- the newly created light entity.

### createPointLight

```cpp
EntityHandle createPointLight(const glm::vec3& position,
                               const glm::vec3& color     = glm::vec3(1.f),
                               float intensity             = 5.f,
                               float range                 = 15.f);
```

Create a point light entity at the given position.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `position` | `const glm::vec3&` | -- | World position of the light. |
| `color` | `const glm::vec3&` | `glm::vec3(1.f)` | RGB color (linear space). |
| `intensity` | `float` | `5.f` | Light intensity multiplier. |
| `range` | `float` | `15.f` | Attenuation range in world units. |

**Returns:** `EntityHandle` -- the newly created light entity.

### getCameraEntity

```cpp
EntityHandle getCameraEntity() const;
```

Get the handle of the camera entity created by `createCamera()`.

**Returns:** `EntityHandle` -- the camera entity, or `NullEntity` if no camera has been created.

### getDeltaTime

```cpp
float getDeltaTime() const;
```

Get the frame delta time in seconds. Typically called inside `onUpdate` or `onPreRender`.

**Returns:** `float` -- seconds elapsed since the previous frame.

---

## Custom Uniform Values

Set named values that flow into shader uniforms through the render graph dot-path system. Keys correspond to dot-path identifiers in your JSON pipeline definition (e.g. `"custom.time"`, `"custom.windStrength"`).

### setCustomFloat

```cpp
void setCustomFloat(const std::string& key, float value);
```

Set a custom `float` uniform.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const std::string&` | Dot-path key name. |
| `value` | `float` | The value. |

### setCustomVec2

```cpp
void setCustomVec2(const std::string& key, const glm::vec2& value);
```

Set a custom `vec2` uniform.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const std::string&` | Dot-path key name. |
| `value` | `const glm::vec2&` | The value. |

### setCustomVec3

```cpp
void setCustomVec3(const std::string& key, const glm::vec3& value);
```

Set a custom `vec3` uniform.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const std::string&` | Dot-path key name. |
| `value` | `const glm::vec3&` | The value. |

### setCustomVec4

```cpp
void setCustomVec4(const std::string& key, const glm::vec4& value);
```

Set a custom `vec4` uniform.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const std::string&` | Dot-path key name. |
| `value` | `const glm::vec4&` | The value. |

### setCustomUint

```cpp
void setCustomUint(const std::string& key, uint32_t value);
```

Set a custom `uint32_t` uniform.

| Parameter | Type | Description |
|-----------|------|-------------|
| `key` | `const std::string&` | Dot-path key name. |
| `value` | `uint32_t` | The value. |

---

## Supporting Types

### GltfOptions

Defined in `Facade/FacadeTypes.h`. Controls what gets loaded from a glTF file.

```cpp
struct GltfOptions {
    bool loadTextures     = true;
    bool loadMaterials    = true;
    bool createEntities   = true;
    bool loadSkins        = true;
    bool loadAnimations   = true;
    bool flattenHierarchy = true;
    int  maxTextureSize   = 0;
    bool generateMipmaps  = true;
    bool srgbAlbedo       = true;
    std::string namePrefix;
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `loadTextures` | `bool` | `true` | Load and upload textures to GPU. |
| `loadMaterials` | `bool` | `true` | Parse PBR material parameters. |
| `createEntities` | `bool` | `true` | Create ECS entities for each mesh node. |
| `loadSkins` | `bool` | `true` | Load skeleton/skin data for animated meshes. |
| `loadAnimations` | `bool` | `true` | Load animation clips. |
| `flattenHierarchy` | `bool` | `true` | Flatten the node tree into world-space transforms. |
| `maxTextureSize` | `int` | `0` | Max texture dimension (0 = unlimited). |
| `generateMipmaps` | `bool` | `true` | Generate mipmap chains for textures. |
| `srgbAlbedo` | `bool` | `true` | Treat albedo textures as sRGB. |
| `namePrefix` | `std::string` | `""` | Prefix prepended to all entity names (e.g. `"sponza_"`). |

### GltfResult

Defined in `Facade/FacadeTypes.h`. Returned by `loadGltfScene()`.

```cpp
struct GltfResult {
    bool success = false;
    std::string error;
    std::vector<EntityHandle> entities;
    size_t totalVertices  = 0;
    size_t totalIndices   = 0;
    size_t totalTextures  = 0;
    size_t totalMaterials = 0;

    struct ClipInfo {
        std::string name;
        float duration = 0.0f;
    };
    std::vector<ClipInfo> animationClips;
    size_t skeletonCount = 0;
};
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | `bool` | `true` if loading completed without errors. |
| `error` | `std::string` | Error message (empty on success). |
| `entities` | `std::vector<EntityHandle>` | Handles of all created entities. |
| `totalVertices` | `size_t` | Total vertex count across all meshes. |
| `totalIndices` | `size_t` | Total index count across all meshes. |
| `totalTextures` | `size_t` | Number of textures loaded. |
| `totalMaterials` | `size_t` | Number of materials loaded. |
| `animationClips` | `std::vector<ClipInfo>` | Metadata for each animation clip (name + duration). |
| `skeletonCount` | `size_t` | Number of skeletons loaded. |

---

## EntityHandle and NullEntity

Defined in `Facade/FacadeTypes.h`:

```cpp
using EntityHandle = uint32_t;
static constexpr EntityHandle NullEntity = UINT32_MAX;
```

`EntityHandle` is an opaque identifier wrapping `entt::entity`'s underlying `uint32_t`. `NullEntity` is the sentinel value indicating "no entity."
