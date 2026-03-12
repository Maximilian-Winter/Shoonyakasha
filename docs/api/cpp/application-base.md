# ApplicationBase

`Shoonyakasha::ApplicationBase` -- inheritance-based application framework for C++ apps.

> **Direct C++ approach** -- derive from `ApplicationBase` and override virtual hooks. For the callback-based facade (used by Python), see [EngineAPI](engine-api.md).

**Header:** `#include "App/ApplicationBase.h"`
**Namespace:** `Shoonyakasha`

---

## Lifecycle Diagram

```
Constructor
    |
  run()
    |
    +-- initializeVulkan()
    +-- initializeECS()
    +-- loadIBLTextures()          (if hdrEnvironmentPath set)
    +-- registerSystems()          (virtual -- override to add custom systems)
    +-- onInit()                   (virtual -- load scenes, create entities)
    +-- initializeRenderGraph()    (compiles JSON pipeline)
    +-- bindIBLTextures()
    +-- createSyncObjects()
    +-- onPostInit()               (virtual -- post-compile setup)
    |
    +-- Main Loop ────────────────────────────────────
    |   |                                             |
    |   +-- onUpdate(dt)           (virtual)          |
    |   +-- ECS system update                         |
    |   +-- Scene context update                      |
    |   +-- onPreRender(dt)        (virtual)          |
    |   +-- Command recording + render                |
    |   +-- Present                                   |
    |   +-- onPostRender()         (virtual)          |
    |   |                                             |
    |   +---------------------------------------------+
    |
    +-- onCleanup()                (virtual)
    +-- cleanup()                  (destroy all Vulkan/ECS resources)
```

---

## ApplicationConfig

Defined in `App/ApplicationBase.h`. Configuration struct passed to the `ApplicationBase` constructor.

```cpp
struct ApplicationConfig {
    int width  = 1600;
    int height = 900;
    std::string title   = "Shoonyakasha Application";
    std::string logFile = "application.log";
    LogLevel logLevel   = LogLevel::Info;

    std::string hdrEnvironmentPath;       // empty = no IBL
    IBLGenerationParams iblParams{};      // IBL quality settings

    size_t resourceCacheSize = 2ULL * 1024 * 1024 * 1024;  // 2 GB

    uint32_t maxFramesInFlight = 2;
    std::string pipelineJsonPath;         // Required -- JSON render graph

    std::unordered_map<std::string, uint32_t> renderGraphParameters;
};
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `width` | `int` | `1600` | Window width in pixels. |
| `height` | `int` | `900` | Window height in pixels. |
| `title` | `std::string` | `"Shoonyakasha Application"` | Window title bar text. |
| `logFile` | `std::string` | `"application.log"` | Path to the log output file. |
| `logLevel` | `LogLevel` | `LogLevel::Info` | Log verbosity. Values: `Debug`, `Info`, `Warning`, `Error`. |
| `hdrEnvironmentPath` | `std::string` | `""` (empty) | Path to an HDR equirectangular environment map for IBL generation. Empty disables IBL. |
| `iblParams` | `IBLGenerationParams` | `{}` | IBL quality parameters (cubemap sizes, sample counts). See [IBLGenerationParams](#iblgenerationparams) below. |
| `resourceCacheSize` | `size_t` | `2 GB` | Maximum GPU resource cache size in bytes. |
| `maxFramesInFlight` | `uint32_t` | `2` | Number of Vulkan frames in flight (double/triple buffering). |
| `pipelineJsonPath` | `std::string` | `""` (empty) | **Required.** Path to the JSON render graph definition. |
| `renderGraphParameters` | `std::unordered_map<std::string, uint32_t>` | `{}` | Key-value pairs injected into the render graph before compilation (e.g. SSBO sizes, dispatch counts). |

### IBLGenerationParams

Defined in `IBL/IBLGenerator.h`. Controls IBL texture generation quality.

```cpp
struct IBLGenerationParams {
    uint32_t environmentSize   = 1024;  // Environment cubemap face size
    uint32_t irradianceSize    = 32;    // Irradiance cubemap face size
    uint32_t prefilterSize     = 512;   // Prefilter cubemap face size (max)
    uint32_t brdfLUTSize       = 512;   // BRDF LUT dimensions
    uint32_t irradianceSamples = 2048;  // Hemisphere samples for irradiance
    uint32_t prefilterSamples  = 1024;  // GGX samples per roughness level
    uint32_t brdfSamples       = 1024;  // BRDF integration samples
};
```

---

## Constructor / Destructor

### ApplicationBase (constructor)

```cpp
explicit ApplicationBase(const ApplicationConfig& config);
```

Stores the configuration. No Vulkan or ECS initialization happens until `run()` is called.

| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const ApplicationConfig&` | Application configuration. `pipelineJsonPath` is required. |

### ~ApplicationBase (destructor)

```cpp
virtual ~ApplicationBase();
```

Virtual destructor. Cleanup is handled by `run()` internally; the destructor is safe to call even if `run()` was never invoked.

---

## Lifecycle

### run

```cpp
void run();
```

Main entry point. Initializes Vulkan, ECS, IBL, and the render graph, then enters the main loop. **Blocks** until the window is closed. Calls `onCleanup()` and destroys all resources before returning.

---

## Virtual Hooks

Override these `protected` methods in your derived class to customize behavior. All have empty default implementations except `registerSystems()`.

### onInit

```cpp
virtual void onInit();
```

Called after Vulkan + ECS + IBL initialization, **before** the render graph is compiled. This is the place to:
- Load glTF scenes
- Create camera and light entities
- Set render graph parameters via `getRenderGraph()`

### onPostInit

```cpp
virtual void onPostInit();
```

Called after the render graph is compiled, IBL textures are bound, and sync objects are created. Use for deferred setup that depends on compiled pipeline state.

### onUpdate

```cpp
virtual void onUpdate(float dt);
```

Called every frame **before** the ECS system update. Use for game logic, entity manipulation, and input response.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dt` | `float` | Seconds elapsed since the previous frame. |

### onPreRender

```cpp
virtual void onPreRender(float dt);
```

Called every frame after the scene context update, **before** Vulkan command recording begins. Use for last-minute uniform updates or visibility changes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `dt` | `float` | Seconds elapsed since the previous frame. |

### onPostRender

```cpp
virtual void onPostRender();
```

Called every frame after the frame has been presented.

### onKeyPressed

```cpp
virtual void onKeyPressed(int keyCode);
```

Called on key press events. Key codes use GLFW constants (e.g. `GLFW_KEY_ESCAPE`).

| Parameter | Type | Description |
|-----------|------|-------------|
| `keyCode` | `int` | GLFW key code. |

### onResize

```cpp
virtual void onResize(uint32_t width, uint32_t height);
```

Called when the window is resized. The swap chain is recreated automatically; use this for updating aspect ratios or resolution-dependent resources.

| Parameter | Type | Description |
|-----------|------|-------------|
| `width` | `uint32_t` | New window width in pixels. |
| `height` | `uint32_t` | New window height in pixels. |

### onCleanup

```cpp
virtual void onCleanup();
```

Called before destruction of Vulkan and ECS resources. Override for custom cleanup (e.g. releasing application-owned GPU resources).

### registerSystems

```cpp
virtual void registerSystems();
```

Override to register additional ECS systems (e.g. `PhysicsSystem`). The **default implementation** registers:
- `TransformSystem`
- `CameraSystem`
- `CameraControllerSystem`

If you override this, call the base implementation first to keep the default systems, then add your own.

---

## Accessor Methods

All accessors are `protected`. They return references to internal subsystems that remain valid for the lifetime of the application.

### getDevice

```cpp
VulkanDevice& getDevice();
```

Returns the Vulkan logical device.

### getWindow

```cpp
VulkanWindow& getWindow();
```

Returns the GLFW window wrapper.

### getSwapChain

```cpp
VulkanSwapChain& getSwapChain();
```

Returns the Vulkan swap chain.

### getRenderGraph

```cpp
FrameGraph::RenderGraph& getRenderGraph();
```

Returns the render graph. Useful in `onInit()` to set parameters before compilation.

### getScene

```cpp
ECS::Scene& getScene();
```

Returns the active ECS scene.

### getRegistry

```cpp
entt::registry& getRegistry();
```

Returns the EnTT registry directly (shortcut for `getScene().getRegistry()`).

### getResourceManager

```cpp
ResourceManager& getResourceManager();
```

Returns the GPU resource manager (buffer/texture allocation).

### getGltfLoader

```cpp
GltfSceneLoader& getGltfLoader();
```

Returns the glTF scene loader.

### getLogger

```cpp
Logger& getLogger();
```

Returns the logger instance.

### getEventDispatcher

```cpp
EventDispatcher& getEventDispatcher();
```

Returns the event dispatcher for pub/sub messaging.

### getInputHandler

```cpp
ECS::StandaloneInputHandler& getInputHandler();
```

Returns the input handler for polling key/mouse state.

### getCameraEntity

```cpp
entt::entity getCameraEntity() const;
```

Returns the camera entity created by `createCamera()`, or `entt::null` if no camera has been created.

### getDeltaTime

```cpp
float getDeltaTime() const;
```

Returns the frame delta time in seconds.

### getIBLResources

```cpp
IBLResources& getIBLResources();
```

Returns the IBL texture resources (environment map, irradiance, prefilter, BRDF LUT).

### getCurrentFrame

```cpp
uint32_t getCurrentFrame() const;
```

Returns the current frame-in-flight index (0 to `maxFramesInFlight - 1`).

---

## Convenience Helpers

### createCamera

```cpp
entt::entity createCamera(const glm::vec3& pos,
                           float fov       = 60.f,
                           float speed     = 8.f,
                           float nearPlane = 0.1f,
                           float farPlane  = 1000.f);
```

Create a camera entity with a controller component, perspective projection, and the given position. The created entity is stored internally and returned by `getCameraEntity()`.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `pos` | `const glm::vec3&` | -- | Initial world position. |
| `fov` | `float` | `60.f` | Vertical field of view in degrees. |
| `speed` | `float` | `8.f` | Camera movement speed (units/second). |
| `nearPlane` | `float` | `0.1f` | Near clipping plane distance. |
| `farPlane` | `float` | `1000.f` | Far clipping plane distance. |

**Returns:** `entt::entity` -- the newly created camera entity.

### loadGltfScene

```cpp
GltfLoadResult loadGltfScene(const std::string& path,
                              const GltfLoadOptions& opts = {});
```

Load a glTF 2.0 file (.gltf or .glb) into the active ECS scene. Creates entities with mesh, material, transform, and optionally skeleton/animation components.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `path` | `const std::string&` | -- | File system path to the glTF file. |
| `opts` | `const GltfLoadOptions&` | `{}` | Loading options (textures, materials, mipmaps, etc.). |

**Returns:** `GltfLoadResult` -- result struct with success status, entity list, and statistics.

### createDirectionalLight

```cpp
entt::entity createDirectionalLight(const glm::vec3& direction,
                                     const glm::vec3& color = glm::vec3(1.f),
                                     float intensity = 2.f);
```

Create a directional light entity (e.g. sunlight).

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `direction` | `const glm::vec3&` | -- | Light direction vector (will be normalized). |
| `color` | `const glm::vec3&` | `glm::vec3(1.f)` | RGB color (linear space). |
| `intensity` | `float` | `2.f` | Light intensity multiplier. |

**Returns:** `entt::entity` -- the newly created light entity.

### createPointLight

```cpp
entt::entity createPointLight(const glm::vec3& position,
                               const glm::vec3& color = glm::vec3(1.f),
                               float intensity = 5.f,
                               float range = 15.f);
```

Create a point light entity at the given position.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `position` | `const glm::vec3&` | -- | World position of the light. |
| `color` | `const glm::vec3&` | `glm::vec3(1.f)` | RGB color (linear space). |
| `intensity` | `float` | `5.f` | Light intensity multiplier. |
| `range` | `float` | `15.f` | Attenuation range in world units. |

**Returns:** `entt::entity` -- the newly created light entity.

---

## Minimal Example

```cpp
#include "App/ApplicationBase.h"

class MyApp : public ApplicationBase {
public:
    MyApp() : ApplicationBase({
        .width  = 1920,
        .height = 1080,
        .title  = "My App",
        .hdrEnvironmentPath = "assets/environment.hdr",
        .pipelineJsonPath   = "pipelines/pbr.json"
    }) {}

protected:
    void onInit() override {
        createCamera({0.f, 2.f, 5.f});
        createDirectionalLight({-0.5f, -1.f, -0.3f});
        loadGltfScene("assets/scene.glb");
    }

    void onUpdate(float dt) override {
        // Game logic here
    }
};

int main() {
    MyApp app;
    app.run();
    return 0;
}
```

---

## See Also

- [EngineAPI](engine-api.md) -- callback-based facade alternative (used by Python)
- [ECS Components](ecs-components.md) -- components attached to entities
- [GPU Types](gpu-types.md) -- foundational graphics structures
