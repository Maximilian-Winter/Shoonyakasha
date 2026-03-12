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
    virtual void onUpdate(float dt) {}

    // Called each frame after scene context update, before command recording.
    virtual void onPreRender(float dt) {}

    // Called each frame after present.
    virtual void onPostRender() {}

    // Called on key press. keyCode is GLFW key code.
    virtual void onKeyPressed(int keyCode) {}

    // Called on window resize.
    virtual void onResize(uint32_t width, uint32_t height) {}

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

private:
    ApplicationConfig m_config;

    // ─── Vulkan Core ───────────────────────────────────────────
    std::unique_ptr<VulkanInstance> m_instance;
    std::unique_ptr<VulkanWindow> m_window;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanSwapChain> m_swapChain;
    std::unique_ptr<VulkanCommandManager> m_commandManager;
    std::unique_ptr<DescriptorManager> m_descriptorManager;
    std::unique_ptr<ResourceManager> m_resourceManager;
    std::unique_ptr<GltfSceneLoader> m_gltfLoader;
    std::unique_ptr<Logger> m_logger;
    std::unique_ptr<EventDispatcher> m_eventDispatcher;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // ─── Frame Graph ───────────────────────────────────────────
    std::unique_ptr<FrameGraph::RenderGraph> m_renderGraph;
    std::unique_ptr<FrameGraph::SharedBufferRegistry> m_sharedBufferRegistry;

    // ─── ECS ───────────────────────────────────────────────────
    std::unique_ptr<ECS::SceneManager> m_sceneManager;
    std::shared_ptr<ECS::Scene> m_activeScene;
    entt::entity m_cameraEntity = entt::null;
    std::unique_ptr<ECS::StandaloneInputHandler> m_inputHandler;

    // ─── IBL ───────────────────────────────────────────────────
    IBLResources m_iblResources;

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
    void initializeRenderGraph();
    void bindIBLTextures();
    void createSyncObjects();
    void setupEventHandlers();

    void update();
    void render();
    void presentFrame(uint32_t imageIndex);
    void handleSwapChainRecreation();
    void cleanup();
};

} // namespace Shoonyakasha

using Shoonyakasha::ApplicationConfig;
using Shoonyakasha::ApplicationBase;
