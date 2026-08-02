//
// ApplicationBase.h - Reusable application framework for Shoonyakasha
//
// Absorbs the common boilerplate from all test apps:
// Vulkan init, ECS setup, IBL generation, render graph, sync objects,
// main loop, input handling, and cleanup.
//
// Derived classes override virtual hooks to customize behavior.
//

#pragma once

#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include "IBL/IBLGenerator.h"
#include "Resources/GltfSceneLoader.h"
#include "ECS/Sprite2DComponents.h"
#include "Capture/VideoRecorder.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <unordered_map>

namespace Shoonyakasha {

// Forward declarations
class VulkanInstance;
class VulkanDevice;
class VulkanWindow;
class VulkanSwapChain;
class VulkanCommandManager;
class DescriptorManager;
class ResourceManager;
class Sprite2DManager;
class FontLoader;

namespace FrameGraph { class RenderGraph; class SharedBufferRegistry; }
namespace ECS {
    class Scene;
    class SceneManager;
    class StandaloneInputHandler;
}

// ═══════════════════════════════════════════════════════════════
// Application Configuration
// ═══════════════════════════════════════════════════════════════

struct ApplicationConfig {
    int width = 1600;
    int height = 900;
    std::string title = "Shoonyakasha Application";
    std::string logFile = "application.log";
    LogLevel logLevel = LogLevel::Info;

    // IBL (empty = no IBL generation)
    std::string hdrEnvironmentPath;
    IBLGenerationParams iblParams{};

    // Resources
    size_t resourceCacheSize = 2ULL * 1024 * 1024 * 1024;

    // Rendering
    uint32_t maxFramesInFlight = 2;
    std::string pipelineJsonPath;   // Required — JSON render graph pipeline

    // Vulkan validation layers. On by default; falls back to off with a warning if the
    // Khronos layer is not installed. Turn off for release builds or profiling runs.
    bool enableValidation = true;

    // Render graph parameters (set before compile — used for SSBO sizing, dispatch counts, etc.)
    std::unordered_map<std::string, uint32_t> renderGraphParameters;
};

// ═══════════════════════════════════════════════════════════════
// Application Base Class
// ═══════════════════════════════════════════════════════════════

class ApplicationBase {
public:
    explicit ApplicationBase(const ApplicationConfig& config);
    virtual ~ApplicationBase();

    // Main entry point — initializes everything, runs loop, cleans up
    void run();

protected:
    // ─── Virtual Hooks ─────────────────────────────────────────

    // Called after Vulkan + ECS + IBL init, before render graph compile.
    // Override to load scenes, create entities, set render graph parameters.
    virtual void onInit() {}

    // Called after render graph compile + IBL bind + sync objects.
    virtual void onPostInit() {}

    // Called each frame before ECS update.
    virtual void onUpdate(float /*dt*/) {}

    // Called each frame after scene context update, before command recording.
    virtual void onPreRender(float /*dt*/) {}

    // Called each frame after present.
    virtual void onPostRender() {}

    // Called on key press. keyCode is GLFW key code.
    virtual void onKeyPressed(int /*keyCode*/) {}

    // Called on window resize.
    virtual void onResize(uint32_t /*width*/, uint32_t /*height*/) {}

    // Called before destruction. Override for custom cleanup.
    virtual void onCleanup() {}

    // Override to register additional ECS systems (PhysicsSystem, etc.)
    // Default registers: TransformSystem, CameraSystem, CameraControllerSystem
    virtual void registerSystems();

    // ─── Accessors ─────────────────────────────────────────────

    VulkanDevice& getDevice();
    VulkanWindow& getWindow();
    VulkanSwapChain& getSwapChain();
    FrameGraph::RenderGraph& getRenderGraph();
    ECS::Scene& getScene();
    entt::registry& getRegistry();
    ResourceManager& getResourceManager();
    GltfSceneLoader& getGltfLoader();
    Logger& getLogger();
    EventDispatcher& getEventDispatcher();
    ECS::StandaloneInputHandler& getInputHandler();
    entt::entity getCameraEntity() const { return m_cameraEntity; }
    float getDeltaTime() const { return m_deltaTime; }
    IBLResources& getIBLResources() { return m_iblResources; }
    uint32_t getCurrentFrame() const { return m_currentFrame; }

    // ─── Convenience Helpers ───────────────────────────────────

    // Create a camera entity with controller
    entt::entity createCamera(const glm::vec3& pos,
                               float fov = 60.f,
                               float speed = 8.f,
                               float nearPlane = 0.1f,
                               float farPlane = 1000.f);

    // Load a glTF scene into the active ECS scene
    GltfLoadResult loadGltfScene(const std::string& path,
                                  const GltfLoadOptions& opts = {});

    // Create a directional light entity
    entt::entity createDirectionalLight(const glm::vec3& direction,
                                         const glm::vec3& color = glm::vec3(1.f),
                                         float intensity = 2.f);

    // Create a point light entity
    entt::entity createPointLight(const glm::vec3& position,
                                   const glm::vec3& color = glm::vec3(1.f),
                                   float intensity = 5.f,
                                   float range = 15.f);

    // Create a world-space sprite (billboard-style quad placed in 3D world
    // coordinates, projected with the active 3D camera).
    entt::entity createSprite(const glm::vec3& worldPos,
                               const std::string& texturePath,
                               const glm::vec2& size = glm::vec2(1.f),
                               const glm::vec4& tint = glm::vec4(1.f));

    // Create a screen-space UI panel anchored to a viewport corner/edge/center.
    // If texturePath is empty, the panel renders as a flat-colored rect.
    entt::entity createUIPanel(UIAnchorComponent::Anchor anchor,
                                const glm::vec2& offsetPixels,
                                const glm::vec2& sizePixels,
                                const std::string& texturePath = "",
                                const glm::vec4& color = glm::vec4(1.f));

    // Create a screen-space text label anchored to a viewport corner/edge/center.
    // fontPath must point to a .ttf/.otf file - text is baked into one sprite
    // entity per glyph via TextRenderSystem.
    entt::entity createText(const std::string& text,
                             UIAnchorComponent::Anchor anchor,
                             const glm::vec2& offsetPixels,
                             const std::string& fontPath,
                             float fontSize = 24.f,
                             const glm::vec4& color = glm::vec4(1.f));

    // Get the Sprite2DManager (shared quad mesh + texture cache for sprites/UI).
    Sprite2DManager& getSprite2DManager();

    // Get the FontLoader (TTF baking + glyph atlas cache for text).
    FontLoader& getFontLoader();

    // ─── Frame Capture ─────────────────────────────────────────
    //
    // Both capture the image that was last presented, so what lands on disk is
    // what was on screen — after tonemapping, after UI, everything. Readback is
    // synchronous, so recording costs frame rate; fine for a clip of a demo,
    // wrong for anything shipping.

    /// Write the last presented frame to disk. Format follows the extension:
    /// .png, .jpg, .bmp, .tga, or .hdr. Returns false and logs why on failure.
    bool captureScreenshot(const std::string& path);

    /// Begin recording every presented frame. The container follows the
    /// extension — .mkv, .mp4, .webm. Needs ffmpeg on PATH or in $FFMPEG;
    /// returns false and logs why if it is missing.
    bool startRecording(const std::string& path,
                        const VideoRecorder::Options& options = {});

    /// Finish the recording and finalise the file. Called automatically at
    /// shutdown, because a recording that is never stopped may not be playable.
    bool stopRecording();

    bool isRecording() const;

    /// Frames written to the current or most recent recording.
    uint64_t getRecordedFrameCount() const;

private:
    ApplicationConfig m_config;

    // ─── Diagnostics ───────────────────────────────────────────
    //
    // Declared first, so they are destroyed LAST. Both are handed out as raw
    // pointers and references — VulkanWindow borrows both, StandaloneInputHandler
    // borrows the dispatcher, and getLogger()/getEventDispatcher() hand them to
    // anyone. Declared further down, they were destroyed while their borrowers
    // were still alive, and ~VulkanWindow's first statement logged through a
    // freed Logger.
    std::unique_ptr<Logger> m_logger;
    std::unique_ptr<EventDispatcher> m_eventDispatcher;

    // ─── Vulkan Core ───────────────────────────────────────────
    std::unique_ptr<VulkanInstance> m_instance;
    std::unique_ptr<VulkanWindow> m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanSwapChain> m_swapChain;
    std::unique_ptr<VulkanCommandManager> m_commandManager;
    std::unique_ptr<DescriptorManager> m_descriptorManager;
    std::unique_ptr<ResourceManager> m_resourceManager;
    std::unique_ptr<GltfSceneLoader> m_gltfLoader;
    std::unique_ptr<Sprite2DManager> m_sprite2DManager;
    std::unique_ptr<FontLoader> m_fontLoader;
    std::vector<VkCommandBuffer> m_commandBuffers;
    glm::vec2 m_screenSize{1600.0f, 900.0f};  // Updated each frame; drives UILayoutSystem

    // ─── Frame Graph ───────────────────────────────────────────
    // The registry must outlive the graph: RenderGraph holds a raw pointer to it
    // (setSharedBufferRegistry) and unregisters its targets in ~RenderGraph.
    // Declared the other way round, the registry was destroyed first and that
    // unregister call reached into freed memory — the shutdown crash.
    std::unique_ptr<FrameGraph::SharedBufferRegistry> m_sharedBufferRegistry;
    std::unique_ptr<FrameGraph::RenderGraph> m_renderGraph;

    // ─── ECS ───────────────────────────────────────────────────
    std::unique_ptr<ECS::SceneManager> m_sceneManager;
    std::shared_ptr<ECS::Scene> m_activeScene;
    entt::entity m_cameraEntity = entt::null;
    std::unique_ptr<ECS::StandaloneInputHandler> m_inputHandler;

    // ─── IBL ───────────────────────────────────────────────────
    IBLResources m_iblResources;

    // ─── Frame Capture ─────────────────────────────────────────
    VideoRecorder m_videoRecorder;
    // Index of the swapchain image most recently handed to vkQueuePresentKHR.
    // Capture reads that one: reading the image currently being rendered into
    // would show a half-drawn frame.
    uint32_t m_lastPresentedImage = 0;
    bool     m_hasPresented = false;

    // ─── Synchronization ───────────────────────────────────────
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;

    // ─── Timing ────────────────────────────────────────────────
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_deltaTime = 0.0f;

    // ─── Internal Methods ──────────────────────────────────────
    void initializeVulkan();
    void initializeECS();
    void loadIBLTextures();
    /// Construct the RenderGraph object. Runs before onInit() so subclasses and
    /// facade callbacks can reach getRenderGraph() there.
    void createRenderGraph();

    /// Load the JSON, bind the scene and compile. Runs after onInit() so that
    /// entities created there are present.
    void initializeRenderGraph();
    void bindIBLTextures();
    void createSyncObjects();
    void setupEventHandlers();

    void update();
    void render();
    void presentFrame(uint32_t imageIndex);

    /// Pixels of the last presented swapchain image as tightly packed RGBA8, or
    /// empty if nothing has been presented yet or the surface does not allow
    /// copying from it.
    std::vector<uint8_t> readPresentedFrame(VkExtent2D& extentOut);
    void handleSwapChainRecreation();
    void cleanup();
};

} // namespace Shoonyakasha

using Shoonyakasha::ApplicationConfig;
using Shoonyakasha::ApplicationBase;
