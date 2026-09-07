# EngineAPI

Access: `EngineAPI(config)`. [Other language reference](../python/engine.md).

Include `Facade/EngineAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/EngineAPI.h).

Owns the application and provides callbacks and convenience constructors. `run()` blocks until the window closes. Keep sub-API access and scene setup in the engine's lifetime.

## Initialization and callbacks

| Callback | Arguments | When it runs |
|---|---|---|
| Init | none | Vulkan, assets, and ECS exist; before graph compilation |
| Post-init | none | After graph compilation and event setup |
| Update | `dt` in seconds | Application update, before standard buffer upload |
| Pre-render | `dt` in seconds | After standard buffers update; facade also updates skeletal animation here |
| Post-render | none | After presentation |
| Key pressed | integer key code | Key-press event |
| Resize | width, height | Window resize event |
| Cleanup | none | Application shutdown hook |

Input can be accessed immediately and input callbacks registered before `run`. Scene and ECS access require initialization. Physics exists before `run`, but native setters are no-ops until it is wired; configure it in init.

Use update for custom values that must enter the current frame's automatic buffers. Init is the right place for scene loading; post-init is the first hook with a compiled render graph.

## Configuration and results

C++ configuration is `EngineConfig` in [FacadeTypes.h](../../../include/Facade/FacadeTypes.h). Defaults: 1600×900, title `Shoonyakasha Application`, log file `application.log`, log level 1 (Info), two frames in flight, empty environment and pipeline paths. A pipeline path is required to run. Log levels are 0 Debug, 1 Info, 2 Warning, 3 Error. Render graph parameters are string→unsigned-integer values for allocation/count configuration.

Python exposes the constructor arguments listed in its [Engine reference](../python/engine.md). It does **not** expose C++ `enableValidation`; the C++ default is true with a warning/fallback when the layer is unavailable.

Creation helpers return entity handles. glTF loading returns a result whose `success` and `error` must be checked. Capture/start/stop return success booleans. Custom-value setters publish values under `scene.custom.<key>`; pass the key without that prefix.

See [loading scenes](../../guides/loading-scenes.md), [capture](../../guides/frame-capture.md), [sprites/UI/text](../../guides/sprites-ui-text.md), and [custom uniforms](../../guides/custom-shader-uniforms.md).

## Free functions

`assetExists(path)` returns a bool; `resolveAsset(path)` returns a resolved path or the original input; `describeAssetRoot()` describes discovery. `videoRecordingAvailable()` returns whether ffmpeg was found and `findFfmpeg()` returns its path or an empty string. All are declared in `EngineAPI.h`.

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
explicit EngineAPI(const EngineConfig& config);
~EngineAPI();
void run();
void setOnInit(VoidCallback cb);
void setOnPostInit(VoidCallback cb);
void setOnUpdate(UpdateCallback cb);
void setOnPreRender(UpdateCallback cb);
void setOnPostRender(VoidCallback cb);
void setOnKeyPressed(KeyCallback cb);
void setOnResize(ResizeCallback cb);
void setOnCleanup(VoidCallback cb);
SceneAPI& getScene();
InputAPI& getInput();
PhysicsAPI& getPhysics();
EcsAPI& getEcs();
EntityHandle createCamera(const glm::vec3& pos, float fov = 60.f, float speed = 8.f, float nearPlane = 0.1f, float farPlane = 1000.f);
bool captureScreenshot(const std::string& path);
bool startRecording(const std::string& path, const RecordingOptions& options = {});
bool stopRecording();
bool isRecording() const;
uint64_t getRecordedFrameCount() const;
GltfResult loadGltfScene(const std::string& path, const GltfOptions& opts = {});
EntityHandle createDirectionalLight(const glm::vec3& direction, const glm::vec3& color = glm::vec3(1.f), float intensity = 2.f);
EntityHandle createPointLight(const glm::vec3& position, const glm::vec3& color = glm::vec3(1.f), float intensity = 5.f, float range = 15.f);
EntityHandle createSprite(const glm::vec3& worldPos, const std::string& texturePath, const glm::vec2& size = glm::vec2(1.f), const glm::vec4& tint = glm::vec4(1.f));
EntityHandle createUIPanel(UIAnchor anchor, const glm::vec2& offsetPixels, const glm::vec2& sizePixels, const std::string& texturePath = "", const glm::vec4& color = glm::vec4(1.f));
EntityHandle createText(const std::string& text, UIAnchor anchor, const glm::vec2& offsetPixels, const std::string& fontPath, float fontSize = 24.f, const glm::vec4& color = glm::vec4(1.f));
EntityHandle getCameraEntity() const;
float getDeltaTime() const;
void setCustomFloat(const std::string& key, float value);
void setCustomVec2(const std::string& key, const glm::vec2& value);
void setCustomVec3(const std::string& key, const glm::vec3& value);
void setCustomVec4(const std::string& key, const glm::vec4& value);
void setCustomUint(const std::string& key, uint32_t value);
```

<!-- END SOURCE API -->
