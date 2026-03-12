# C++ Quickstart

**Goal:** Render a 3D scene using ApplicationBase in roughly 50 lines of C++.

Shoonyakasha provides two C++ styles for building applications:

- **ApplicationBase** -- inheritance-based. Override virtual hooks for init, update, input, cleanup.
- **EngineAPI (Facade)** -- callback-based. Register lambdas instead of subclassing. This is the same API that the Python bindings wrap.

Both approaches expose the same features. Choose whichever style you prefer.

---

## ApplicationBase Example

```cpp
#include <Shoonyakasha.h>

class MyApp : public ApplicationBase {
public:
    MyApp() : ApplicationBase(buildConfig()) {}

private:
    static ApplicationConfig buildConfig() {
        ApplicationConfig cfg;
        cfg.title               = "My First Scene";
        cfg.width               = 1600;
        cfg.height              = 900;
        cfg.pipelineJsonPath    = "pipelines/pbr_ibl_pipeline_v3.json";
        cfg.hdrEnvironmentPath  = "environments/environment.hdr";
        return cfg;
    }

protected:
    void onInit() override {
        createCamera({0.f, 2.f, 5.f}, 60.f, 8.f);
        createDirectionalLight({1.f, -1.f, 0.5f}, {1.f, 1.f, 1.f}, 2.0f);

        GltfLoadOptions options;
        auto result = loadGltfScene("models/NewSponza.glb", options);
        LOG_INFO("Loaded {} vertices", result.totalVertices);
    }

    void onUpdate(float dt) override {
        // Game logic here -- called every frame before ECS systems update
    }

    void onKeyPressed(int keyCode) override {
        if (keyCode == GLFW_KEY_P) {
            // Toggle physics, spawn objects, etc.
        }
    }
};

int main() {
    MyApp app;
    app.run();
    return 0;
}
```

That is roughly 40 lines. The engine handles Vulkan initialization, window creation, swap chain management, ECS setup, IBL generation, render graph compilation, synchronization, and teardown.

---

## ApplicationBase Lifecycle

Understanding the lifecycle helps you know which hook to use and when.

### Startup sequence

1. **Constructor** -- pass an `ApplicationConfig` with your title, dimensions, pipeline JSON path, HDR environment path, and any other settings.
2. **`run()`** kicks off the full sequence:
   - Initialize Vulkan (instance, device, swap chain, command buffers)
   - Initialize ECS (scene manager, default systems via `registerSystems()`)
   - Generate IBL textures from the HDR environment (if configured)
   - **`onInit()`** -- your hook. Load scenes, create entities, set render graph parameters.
   - Compile the render graph from the pipeline JSON
   - Bind IBL textures into the render graph
   - Create synchronization objects
   - **`onPostInit()`** -- your hook. Everything is ready; do final setup.
   - Enter the main loop

### Main loop (each frame)

1. **`onUpdate(dt)`** -- your hook. Game logic, AI, spawning entities.
2. ECS systems update (transforms, cameras, camera controllers, physics if registered)
3. Scene context update (push per-frame data to GPU buffers)
4. **`onPreRender(dt)`** -- your hook. Last chance to modify state before rendering.
5. Command recording and submission (automatic)
6. Present frame
7. **`onPostRender()`** -- your hook. Post-frame bookkeeping.

### Shutdown

1. Window close event received
2. **`onCleanup()`** -- your hook. Release custom resources.
3. Engine destroys all Vulkan resources, ECS state, and internal subsystems.

---

## ApplicationConfig Fields

| Field | Type | Default | Description |
|---|---|---|---|
| `width` | `int` | `1600` | Window width in pixels |
| `height` | `int` | `900` | Window height in pixels |
| `title` | `std::string` | `"Shoonyakasha Application"` | Window title |
| `logFile` | `std::string` | `"application.log"` | Log output file |
| `logLevel` | `LogLevel` | `Info` | Minimum log level (Debug, Info, Warning, Error) |
| `hdrEnvironmentPath` | `std::string` | `""` | Path to HDR environment map for IBL. Empty disables IBL generation |
| `iblParams` | `IBLGenerationParams` | `{}` | IBL generation settings (cubemap size, mip levels, etc.) |
| `pipelineJsonPath` | `std::string` | `""` | **Required.** Path to the JSON render graph pipeline |
| `renderGraphParameters` | `unordered_map<string, uint32_t>` | `{}` | Parameters passed to render graph compilation (SSBO sizing, dispatch counts) |
| `maxFramesInFlight` | `uint32_t` | `2` | Number of frames that can be in flight simultaneously |
| `resourceCacheSize` | `size_t` | `2 GB` | Maximum size of the GPU resource cache |

---

## Virtual Hooks Reference

| Hook | Signature | When Called |
|---|---|---|
| `onInit` | `void onInit()` | After Vulkan + ECS init, before render graph compile. Load scenes here. |
| `onPostInit` | `void onPostInit()` | After render graph compile + IBL bind + sync objects created. |
| `onUpdate` | `void onUpdate(float dt)` | Each frame, before ECS systems update. `dt` is seconds since last frame. |
| `onPreRender` | `void onPreRender(float dt)` | Each frame, after scene context update, before command recording. |
| `onPostRender` | `void onPostRender()` | Each frame, after present. |
| `onKeyPressed` | `void onKeyPressed(int keyCode)` | On key press. `keyCode` is a GLFW key constant. |
| `onResize` | `void onResize(uint32_t w, uint32_t h)` | On window resize. |
| `onCleanup` | `void onCleanup()` | Before engine destruction. Release custom resources. |
| `registerSystems` | `void registerSystems()` | During ECS init. Default registers Transform, Camera, CameraController systems. Override to add Physics, custom systems, etc. |

---

## Convenience Helpers

These methods are available inside any `ApplicationBase` subclass:

| Method | Signature | Description |
|---|---|---|
| `createCamera` | `entt::entity createCamera(vec3 pos, float fov=60, float speed=8, float near=0.1, float far=1000)` | Create a camera entity with a controller attached |
| `createDirectionalLight` | `entt::entity createDirectionalLight(vec3 dir, vec3 color={1,1,1}, float intensity=2)` | Create a directional light entity |
| `createPointLight` | `entt::entity createPointLight(vec3 pos, vec3 color={1,1,1}, float intensity=5, float range=15)` | Create a point light entity |
| `loadGltfScene` | `GltfLoadResult loadGltfScene(string path, GltfLoadOptions opts={})` | Load a glTF file into the active ECS scene |
| `getCameraEntity` | `entt::entity getCameraEntity() const` | Get the camera entity created by `createCamera` |
| `getDeltaTime` | `float getDeltaTime() const` | Get the time delta for the current frame (seconds) |

Additional accessors provide access to internal subsystems: `getDevice()`, `getWindow()`, `getSwapChain()`, `getRenderGraph()`, `getScene()`, `getRegistry()`, `getResourceManager()`, `getGltfLoader()`, `getLogger()`, `getEventDispatcher()`, `getInputHandler()`, `getIBLResources()`, `getCurrentFrame()`.

---

## Facade Alternative: EngineAPI

If you prefer a callback-based style (or want the same API that the Python bindings use), use `EngineAPI` from the Facade layer:

```cpp
#include <Facade/EngineAPI.h>

using namespace Shoonyakasha::Facade;

int main() {
    EngineConfig config;
    config.title            = "Facade Example";
    config.width            = 1600;
    config.height           = 900;
    config.pipelineJsonPath = "pipelines/pbr_ibl_pipeline_v3.json";
    config.hdrEnvironmentPath = "environments/environment.hdr";

    EngineAPI engine(config);

    engine.setOnInit([&]() {
        engine.createCamera({0.f, 2.f, 5.f}, 60.f, 8.f);
        engine.createDirectionalLight({1.f, -1.f, 0.5f}, {1.f, 1.f, 1.f}, 2.0f);

        GltfOptions opts;
        auto result = engine.loadGltfScene("models/scene.glb", opts);
    });

    engine.setOnUpdate([](float dt) {
        // Game logic
    });

    engine.setOnKeyPressed([&](int keyCode) {
        // Handle input
    });

    engine.run();
    return 0;
}
```

### ApplicationBase vs EngineAPI

| | ApplicationBase | EngineAPI (Facade) |
|---|---|---|
| **Style** | Inheritance -- override virtual methods | Callbacks -- register lambdas |
| **Entity type** | `entt::entity` | `EntityHandle` (uint32_t) |
| **glTF types** | `GltfLoadOptions` / `GltfLoadResult` | `GltfOptions` / `GltfResult` |
| **Namespace** | `Shoonyakasha` | `Shoonyakasha::Facade` |
| **Header** | `<Shoonyakasha.h>` or `<App/ApplicationBase.h>` | `<Facade/EngineAPI.h>` |
| **Python wraps** | No (C++ only) | Yes -- this is what the Python bindings call |
| **Sub-APIs** | Direct accessors (`getScene()`, etc.) | `getScene()`, `getInput()`, `getPhysics()` returning facade sub-APIs |
| **Custom uniforms** | Via render graph / scene context directly | `setCustomFloat()`, `setCustomVec3()`, etc. |

EngineAPI also exposes the same convenience helpers (`createCamera`, `createDirectionalLight`, `createPointLight`, `loadGltfScene`, `getCameraEntity`, `getDeltaTime`) with identical signatures and defaults.

Choose **ApplicationBase** when you want full access to engine internals (registry, render graph, resource manager). Choose **EngineAPI** when you want a simpler, PIMPL-isolated interface or need the same API surface as Python.

---

## Next Steps

- [ApplicationBase Reference](../api/cpp/application-base.md) -- full API documentation
- [ECS Components](../api/cpp/ecs-components.md) -- transform, mesh, material, light, camera components
- [Frame Graph](../api/cpp/frame-graph.md) -- how the JSON-driven render pipeline works
- [Loading Scenes](../guides/loading-scenes.md) -- glTF loading options, skinned meshes, animations
