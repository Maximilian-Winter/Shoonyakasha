//
// Facade/EngineAPI.h - Python-friendly engine lifecycle wrapper
//
// No Vulkan, no EnTT, no ApplicationBase in this header.
// All internals hidden behind PIMPL.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <vector>

namespace Shoonyakasha {
namespace Facade {

// Forward declare sub-APIs
class SceneAPI;
class InputAPI;
class PhysicsAPI;
class EcsAPI;

// ═══════════════════════════════════════════════════════════════
// Assets
// ═══════════════════════════════════════════════════════════════
//
// The engine resolves asset paths against the shared assets/ directory, so an
// application names only "models/Box.gltf". These let it ask about an asset
// before trying to load one, which is what the graceful-fallback pattern needs:
// large assets like Sponza are not committed, and probing by attempting a load
// logs a failure that did not happen.

/// Is this asset present, either as given or under the shared asset root?
bool assetExists(const std::string& relativePath);

/// Where an asset actually is. Returns the input unchanged when it cannot be
/// found, so a failure names the path the caller wrote.
std::string resolveAsset(const std::string& relativePath);

/// Where the asset root came from, for startup logs and missing-asset reports.
std::string describeAssetRoot();


// ═══════════════════════════════════════════════════════════════
// Frame capture
// ═══════════════════════════════════════════════════════════════

/// Is video recording possible here? False when no ffmpeg can be found.
bool videoRecordingAvailable();

/// The ffmpeg that would be used, or empty if none was found.
std::string findFfmpeg();


class EngineAPI {
public:
    explicit EngineAPI(const EngineConfig& config);
    ~EngineAPI();

    // Non-copyable, non-movable
    EngineAPI(const EngineAPI&) = delete;
    EngineAPI& operator=(const EngineAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Run the engine. Blocks until the window is closed.
    void run();

    // ═══════════════════════════════════════════════════════════
    // Callback Registration (set before or during run)
    // ═══════════════════════════════════════════════════════════

    void setOnInit(VoidCallback cb);
    void setOnPostInit(VoidCallback cb);
    void setOnUpdate(UpdateCallback cb);
    void setOnPreRender(UpdateCallback cb);
    void setOnPostRender(VoidCallback cb);
    void setOnKeyPressed(KeyCallback cb);
    void setOnResize(ResizeCallback cb);
    void setOnCleanup(VoidCallback cb);

    // ═══════════════════════════════════════════════════════════
    // Sub-API Access
    //
    // getInput() and getPhysics() are usable as soon as the EngineAPI exists —
    // register input callbacks or set gravity before run() if you like. They
    // become connected to the running engine during onInit.
    //
    // getScene() and getEcs() need a live ECS::Scene, which only exists once
    // run() has started, and throw std::logic_error before that. Do scene setup
    // from the setOnInit callback.
    //
    // (The previous contract comment claimed all four were "valid after
    // construction", and the getters dereferenced null unique_ptrs.)
    // ═══════════════════════════════════════════════════════════

    SceneAPI&   getScene();     ///< throws std::logic_error before run()
    InputAPI&   getInput();     ///< valid immediately
    PhysicsAPI& getPhysics();   ///< valid immediately
    EcsAPI&     getEcs();       ///< throws std::logic_error before run()

    // ═══════════════════════════════════════════════════════════
    // Convenience Helpers
    // ═══════════════════════════════════════════════════════════

    /// Create a camera entity with controller.
    EntityHandle createCamera(const glm::vec3& pos,
                              float fov = 60.f,
                              float speed = 8.f,
                              float nearPlane = 0.1f,
                              float farPlane = 1000.f);

    /// Load a glTF scene into the active ECS scene.
    // ═══════════════════════════════════════════════════════════
    // Frame capture
    // ═══════════════════════════════════════════════════════════
    //
    // Both capture the frame that was last presented, so what lands on disk is
    // what was on screen. Readback is synchronous, so recording costs frame
    // rate — right for capturing a clip, wrong for anything shipping.

    /// Write the last presented frame to disk. Format follows the extension:
    /// .png, .jpg, .bmp, .tga, .hdr.
    bool captureScreenshot(const std::string& path);

    /// Record every presented frame. Container follows the extension —
    /// .mkv, .mp4, .webm. Needs ffmpeg; see videoRecordingAvailable().
    bool startRecording(const std::string& path,
                        const RecordingOptions& options = {});

    /// Finish and finalise the file. Called automatically at shutdown.
    bool stopRecording();

    bool isRecording() const;
    uint64_t getRecordedFrameCount() const;

    /// While paused, presented frames are not written to the recording: for
    /// rendering several frames per recorded one, such as sub-frames averaged
    /// into motion blur. Set it before the frame renders.
    void setRecordingPaused(bool paused);
    bool isRecordingPaused() const;

    /// Render the scene at the window's size times `scale` and scale it to
    /// the window: 2 supersamples, four samples per pixel. Pipelines read the
    /// render size as scene.screen.renderResolution. Takes effect at the next
    /// frame, which recompiles the graph.
    void setRenderScale(float scale);
    float getRenderScale() const;

    GltfResult loadGltfScene(const std::string& path,
                             const GltfOptions& opts = {});

    /// Create a directional light entity.
    EntityHandle createDirectionalLight(const glm::vec3& direction,
                                        const glm::vec3& color = glm::vec3(1.f),
                                        float intensity = 2.f);

    /// Create a point light entity.
    EntityHandle createPointLight(const glm::vec3& position,
                                  const glm::vec3& color = glm::vec3(1.f),
                                  float intensity = 5.f,
                                  float range = 15.f);

    /// Create a world-space sprite (billboard quad in 3D world coordinates).
    EntityHandle createSprite(const glm::vec3& worldPos,
                              const std::string& texturePath,
                              const glm::vec2& size = glm::vec2(1.f),
                              const glm::vec4& tint = glm::vec4(1.f));

    /// Create a screen-space UI panel anchored to a viewport corner/edge/center.
    /// If texturePath is empty, the panel renders as a flat-colored rect.
    EntityHandle createUIPanel(UIAnchor anchor,
                               const glm::vec2& offsetPixels,
                               const glm::vec2& sizePixels,
                               const std::string& texturePath = "",
                               const glm::vec4& color = glm::vec4(1.f));

    /// Create a screen-space text label anchored to a viewport corner/edge/center.
    /// fontPath must point to a .ttf/.otf file.
    EntityHandle createText(const std::string& text,
                            UIAnchor anchor,
                            const glm::vec2& offsetPixels,
                            const std::string& fontPath,
                            float fontSize = 24.f,
                            const glm::vec4& color = glm::vec4(1.f));

    /// Get the camera entity handle.
    EntityHandle getCameraEntity() const;

    /// Get frame delta time (seconds).
    float getDeltaTime() const;

    // ═══════════════════════════════════════════════════════════
    // Scene Context Custom Values (for shader uniforms via dot-paths)
    // ═══════════════════════════════════════════════════════════

    void setCustomFloat(const std::string& key, float value);
    void setCustomVec2(const std::string& key, const glm::vec2& value);
    void setCustomVec3(const std::string& key, const glm::vec3& value);
    void setCustomVec4(const std::string& key, const glm::vec4& value);
    void setCustomMat4(const std::string& key, const glm::mat4& value);
    void setCustomUint(const std::string& key, uint32_t value);

    // ═══════════════════════════════════════════════════════════
    // Sun Shadow Cascades
    // ═══════════════════════════════════════════════════════════

    /// Cascade settings for the first directional light with castShadows.
    /// The engine refits the cascades to the camera every frame and publishes
    /// them as scene.shadows.sun.* dot-paths. `resolution` is the shadow map's
    /// size in texels and should match it.
    void setSunShadowSettings(uint32_t cascadeCount, float maxDistance,
                              float splitLambda, uint32_t resolution,
                              float casterExtension = 50.f);

    /// World-to-light-clip matrix of cascade `index` for the current frame,
    /// identity when no light casts sun shadows.
    glm::mat4 getSunShadowCascade(uint32_t index) const;

    /// Shadow slots for spot and point lights with castShadows. Each frame
    /// the engine gives the slots to the lights that matter most (bright,
    /// near the camera, able to reach the view) and publishes them as
    /// scene.shadows.spot* and scene.shadows.point* dot-paths. The counts
    /// should match the pipeline's passes. With `atlasResolution` > 0 every
    /// map is a tile of one atlas that size, sized each frame by how large
    /// the light looks, up to `spotResolution` (`pointResolution` per cube
    /// face); with 0 the maps are array layers of exactly those sizes. The
    /// defaults match the default pipeline: 8 spot and 4 point slots in a
    /// 4096 atlas.
    void setLocalShadowSettings(uint32_t spotCount = 8, uint32_t pointCount = 4,
                                uint32_t spotResolution = 2048, uint32_t pointResolution = 1024,
                                uint32_t atlasResolution = 4096);

    // ═══════════════════════════════════════════════════════════
    // Render Pipeline Passes
    // ═══════════════════════════════════════════════════════════

    /// Turn a pipeline pass on or off from the next frame. A disabled pass
    /// draws nothing but still clears its attachments, so a disabled shadow
    /// pass leaves everything lit. May be called from onInit, before the
    /// pipeline is loaded. The declared name of a repeated pass switches
    /// every instance. Returns false if the pipeline has no such pass.
    bool setPassEnabled(const std::string& passName, bool enabled);
    bool isPassEnabled(const std::string& passName) const;

    /// Apply one of the pipeline's "presets" (quality tiers, for the default
    /// pipeline "low", "medium" and "high"): switches its passes and sets its
    /// scene.custom values. May be called from onInit, before the pipeline is
    /// loaded. Returns false if the pipeline has no such preset.
    bool applyPipelinePreset(const std::string& name);

    /// Whether the device traces rays with ray queries, which passes that
    /// "require" them (the default pipeline's "raytraced" preset) need.
    /// Known once run() has created the device; false before.
    bool rayQuerySupported() const;
    std::vector<std::string> getPipelinePresets() const;

    /// Entities an entity geometry pass drew / culled as outside its view the
    /// last time it ran; 0 if it has not run.
    uint32_t getPassDrawnCount(const std::string& passName) const;
    uint32_t getPassCulledCount(const std::string& passName) const;

    // ═══════════════════════════════════════════════════════════
    // Render statistics
    // ═══════════════════════════════════════════════════════════

    /// Collect frame rate, frame times, draw counts and per-pass CPU and GPU
    /// times. `gpuTiming` times passes with GPU timestamp queries where the
    /// device has them. May be called before run(). SHOONYAKASHA_STATS=1
    /// turns stats on at startup and prints a summary every second.
    void setRenderStatsEnabled(bool enabled, bool gpuTiming = true);
    bool isRenderStatsEnabled() const;
    /// Over the last whole second; `enabled` is false while stats are off.
    RenderStatsSnapshot getRenderStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
