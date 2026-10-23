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
class UIAPI;

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

/// Load pipeline JSON text as the engine does, for a device with every
/// capability, without compiling it. Returns the loader's error message,
/// empty when it loads.
std::string checkPipelineJson(const std::string& text);


/// A completed readback (EngineAPI::getReadback). `data` stays valid until
/// the next frame begins.
struct Readback {
    const void* data = nullptr;
    size_t size = 0;
    uint64_t frame = 0;                 // the frame copied, counted from 0
    bool image = false;
    uint32_t width = 0, height = 0;     // an image's
    std::string format;                 // an image's, as pipeline JSON names it
    uint32_t elementCount = 0, elementStride = 0;   // a buffer's
};

/**
 * @brief Owns a callback-driven engine application and its facade APIs.
 *
 * Construction stores configuration; run() initializes Vulkan and scene
 * services and enters the frame loop. Register setOnInit() to create scene
 * content before render-graph compilation. References returned by getScene(),
 * getInput(), getPhysics(), getEcs(), and getUI() are borrowed and must not
 * outlive this object.
 *
 * @see EngineConfig, ApplicationBase
 */
class EngineAPI {
public:
    /// Store application configuration without starting the frame loop.
    /// @param config Window, rendering, asset, and validation settings.
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
    /// Request orderly exit after the current frame.
    void requestQuit();
    /// Actual framebuffer pixels, or (0,0) before initialization.
    glm::vec2 getFramebufferSize() const;

    // ═══════════════════════════════════════════════════════════
    // Callback Registration (set before or during run)
    // ═══════════════════════════════════════════════════════════

    /// Replace the callback run after scene services exist, before graph compilation.
    /// @param cb Initialization function; an empty function clears the callback.
    /// @see getScene(), setOnPostInit()
    void setOnInit(VoidCallback cb);
    /// Replace the callback run after render-graph compilation and event setup.
    /// @param cb Post-initialization function, or empty to clear it.
    void setOnPostInit(VoidCallback cb);
    /// Replace the per-frame application update callback, before standard GPU uploads.
    /// @param cb Receives elapsed frame time in seconds, or empty to clear it.
    void setOnUpdate(UpdateCallback cb);
    /// Replace the callback after standard GPU uploads and skeletal animation updates.
    /// @param cb Receives elapsed frame time in seconds, or empty to clear it.
    /// @note Use setOnUpdate() for values consumed by that frame's automatic uploads.
    void setOnPreRender(UpdateCallback cb);
    /// Replace the callback run after frame presentation.
    /// @param cb Post-presentation function, or empty to clear it.
    void setOnPostRender(VoidCallback cb);
    /// Replace the application's key-press event callback.
    /// @param cb Receives the integer key code, or empty to clear it.
    void setOnKeyPressed(KeyCallback cb);
    /// Replace the application's window-resize callback.
    /// @param cb Receives width and height, or empty to clear it.
    void setOnResize(ResizeCallback cb);
    /// Replace the callback invoked by the application's shutdown hook.
    /// @param cb Cleanup function, or empty to clear it.
    void setOnCleanup(VoidCallback cb);

    // ═══════════════════════════════════════════════════════════
    // Sub-API Access
    //
    // getInput() and getPhysics() are usable as soon as the EngineAPI exists —
    // register input callbacks before run(). Physics setters need a connected
    // physics system and should be called from onInit, after wiring.
    //
    // getScene() and getEcs() need a live ECS::Scene, which only exists once
    // run() has started, and throw std::logic_error before that. Do scene setup
    // from the setOnInit callback.
    //
    // (The previous contract comment claimed all four were "valid after
    // construction", and the getters dereferenced null unique_ptrs.)
    // ═══════════════════════════════════════════════════════════

    /// Access the initialized scene facade.
    /// @return Borrowed facade owned by this engine.
    /// @throws std::logic_error Before scene initialization; call from setOnInit().
    SceneAPI&   getScene();
    /// Access input; callbacks may be registered before run().
    /// @return Borrowed facade owned by this engine, valid immediately.
    InputAPI&   getInput();
    /// Access physics; configure it from setOnInit() once the system is wired.
    /// @return Borrowed facade owned by this engine, valid immediately.
    /// @note Native setters are no-ops before initialization.
    PhysicsAPI& getPhysics();
    /// Access the initialized script/component facade.
    /// @return Borrowed facade owned by this engine.
    /// @throws std::logic_error Before scene initialization; call from setOnInit().
    EcsAPI&     getEcs();
    /// Access the initialized canvas UI facade.
    /// @return Borrowed facade owned by this engine.
    /// @throws std::logic_error Before scene initialization; call from setOnInit().
    UIAPI&      getUI();

    // ═══════════════════════════════════════════════════════════
    // Convenience Helpers
    // ═══════════════════════════════════════════════════════════

    /// Create a camera entity with controller.
    EntityHandle createCamera(const glm::vec3& pos,
                              float fov = 60.f,
                              float speed = 8.f,
                              float nearPlane = 0.1f,
                              float farPlane = 1000.f);

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

    /// Light the scene with an HDR environment map, or an .ibl file made by
    /// bakeEnvironment. An HDR map's IBL is saved beside it (sky.hdr.ibl,
    /// unless EngineConfig::iblCache is off) and loaded from there while the
    /// map is unchanged. From onInit on; false, keeping the current
    /// environment, when it cannot be loaded.
    bool setEnvironment(const std::string& path);
    /// An environment of one colour (linear RGB) in every direction.
    bool setEnvironmentColor(const glm::vec3& color);
    /// Write the IBL of an HDR map to an .ibl file to ship, without changing
    /// the scene's environment. From onInit on.
    bool bakeEnvironment(const std::string& hdrPath, const std::string& outputPath);

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

    /// Load the pipeline JSON and the SPIR-V its passes name again, and
    /// rebuild the render graph, waiting for the GPU. Parameters,
    /// scene.custom values, buffer data, pipeline images and passes switched
    /// with setPassEnabled or applyPipelinePreset stay. Call it from
    /// onUpdate, an ECS system, a UI callback or onPostRender, not from
    /// onInit or onPreRender. Returns an error message, empty on success; on
    /// failure the previous graph is compiled again.
    std::string reloadPipeline();

    /// Load a glTF scene into the active ECS scene and prepare loaded skin buffers.
    /// @param path Model path, resolved through the shared asset resolver.
    /// @param opts Loading flags; see GltfOptions and the scene-loading guide.
    /// @return Loading status, error text, created entities, and animation metadata.
    /// @pre Scene services are initialized; call from setOnInit() or later.
    GltfResult loadGltfScene(const std::string& path,
                             const GltfOptions& opts = {});

    /// A mesh entity drawing triangles from arrays, flat: `positions` x, y, z
    /// per vertex; `indices` three per triangle, counter-clockwise from the
    /// front, or empty for every three vertices; `normals` (x, y, z), `uvs`
    /// (u, v) and `colors` (r, g, b) empty or one per vertex. Without normals
    /// each vertex gets the area-weighted normal of its triangles. The
    /// entity has an identity transform, the default material (white,
    /// metallic 0, roughness 0.5) and casts and receives shadows; it renders,
    /// is culled and is ray traced like a loaded glTF mesh. Returns
    /// NullEntity with `error` set when the arrays do not fit.
    EntityHandle createMesh(const std::vector<float>& positions, const std::vector<uint32_t>& indices,
                            const std::vector<float>& normals, const std::vector<float>& uvs,
                            const std::vector<float>& colors, const std::vector<float>& tangents,
                            const std::string& name, std::string& error);

    /// Replace the triangles of a mesh entity, as createMesh takes them; its
    /// material and transform stay. The old geometry is freed once no frame
    /// in flight draws it. Returns an error message, empty on success.
    std::string setMesh(EntityHandle entity, const std::vector<float>& positions,
                        const std::vector<uint32_t>& indices, const std::vector<float>& normals,
                        const std::vector<float>& uvs, const std::vector<float>& colors,
                        const std::vector<float>& tangents);

    /// Create a directional light entity.
    EntityHandle createDirectionalLight(const glm::vec3& direction,
                                        const glm::vec3& color = glm::vec3(1.f),
                                        float intensity = 2.f);

    /// Create a point light entity.
    EntityHandle createPointLight(const glm::vec3& position,
                                  const glm::vec3& color = glm::vec3(1.f),
                                  float intensity = 5.f,
                                  float range = 15.f);

    /// Create one point light entity per position, in one call: `positions`
    /// holds x, y, z for each. `colors` holds one r, g, b for all of them or
    /// one for each, `intensities` and `ranges` one value for all or one for
    /// each. Throws std::invalid_argument when a list has another length.
    std::vector<EntityHandle> createPointLights(const std::vector<float>& positions,
                                                const std::vector<float>& colors,
                                                const std::vector<float>& intensities,
                                                const std::vector<float>& ranges);

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

    /// Arrays: key[0] .. key[n-1], read by a buffer field with arrayCount
    /// whose source ends in "[i]" (scene.custom.key[i]). Elements left from
    /// an earlier, longer array are removed; the field's elements past the
    /// end read as its fallback.
    void setCustomFloatArray(const std::string& key, const std::vector<float>& values);
    void setCustomVec2Array(const std::string& key, const std::vector<glm::vec2>& values);
    void setCustomVec3Array(const std::string& key, const std::vector<glm::vec3>& values);
    void setCustomVec4Array(const std::string& key, const std::vector<glm::vec4>& values);
    void setCustomMat4Array(const std::string& key, const std::vector<glm::mat4>& values);
    void setCustomUintArray(const std::string& key, const std::vector<uint32_t>& values);

    /// Write `size` bytes at `offset` into the buffer of the pipeline's
    /// bufferLayouts entry `name`, a uniform buffer or a storage buffer with
    /// an elementCount. The bytes stay until set again, over recompiles too.
    /// A uniform buffer gets them in each frame's copy, after its dot-path
    /// fields; a storage buffer gets the bytes that changed copied in before
    /// the next frame's passes. May be called before the pipeline is loaded;
    /// they are checked against it then. Returns an error message, empty
    /// when the bytes were taken.
    std::string setBuffer(const std::string& name, const void* data, size_t size, size_t offset = 0);

    /// Copy the buffer layout or image resource `name` back at the end of
    /// the next frame, whatever the readback frequency its pipeline JSON
    /// declares. False when it declares no readback.
    bool requestReadback(const std::string& name);

    /// The most recent completed readback of `name`: false when there is
    /// none yet. A buffer's is the whole buffer; an image's is mip 0, layer
    /// 0, as rows of tightly packed texels (the depth of a depth format).
    /// Results arrive as many frames after the copy as there are frames in
    /// flight, and are gone once the pipeline is compiled again.
    bool getReadback(const std::string& name, Readback& out) const;

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
    /// used are at most 16 spot and 8 point slots, and at most the slots the
    /// pipeline's shadow passes render. With `atlasResolution` > 0 every
    /// map is a tile of one atlas that size, sized each frame by how large
    /// the light looks, up to `spotResolution` (`pointResolution` per cube
    /// face); with 0 the maps are array layers of exactly those sizes. The
    /// defaults match the default pipeline: 16 spot and 8 point slots in a
    /// 4096 atlas.
    void setLocalShadowSettings(uint32_t spotCount = 16, uint32_t pointCount = 8,
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

    /// Load an image file (PNG, JPEG ..., found as assets are) for the
    /// pipeline's descriptor bindings with "externalImage": name, which
    /// sample white until one is set. Its colours are sRGB. May be called
    /// before the pipeline is loaded, and again to change it while it runs.
    /// Returns false if the file could not be loaded.
    bool setPipelineImage(const std::string& name, const std::string& path);

    /// The same from texels in memory: `size` bytes of `width` x `height`
    /// texels in `format` (a pipeline JSON format name, colour formats the
    /// device can sample), rows of tightly packed texels with the top row
    /// first. The engine keeps the image and frees it once a later one
    /// replaces it and no frame in flight samples it. Uploading waits for the
    /// GPU. May be called before the pipeline is loaded. Returns an error
    /// message, empty on success.
    std::string setPipelineImageData(const std::string& name, const void* data, size_t size,
                                     uint32_t width, uint32_t height, const std::string& format);

    /// Whether the device traces rays with ray queries, which passes that
    /// "require" them (the default pipeline's "raytraced" preset) need.
    /// Known once run() has created the device; false before.
    bool rayQuerySupported() const;
    std::vector<std::string> getPipelinePresets() const;

    /// Entities an entity geometry pass drew / culled as outside its view the
    /// last time it ran; 0 if it has not run.
    uint32_t getPassDrawnCount(const std::string& passName) const;
    uint32_t getPassCulledCount(const std::string& passName) const;
    /// Entities a pass with an execution "occlusion" image left out as
    /// hidden the last time it ran; 0 if it has not run.
    uint32_t getPassOccludedCount(const std::string& passName) const;

    /// Whether passes with an execution "occlusion" image leave out the
    /// entities it hides; on by default.
    void setOcclusionCulling(bool enabled);
    bool isOcclusionCullingEnabled() const;

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
