//
// Declarative Sponza Test Application
//
// 虚空之道 — The Way of Emptiness
//
// This example demonstrates the declarative frame graph:
// - Dot-path UBOs (camera, lights) auto-filled from ECS via JSON sources
// - MaterialComponentV5 + MeshComponent for materials
// - Dot-path resolution for push constants and uniform buffers
// - Dynamic lights from LightComponent entities
// - Minimal C++ code - just load and render
//

#pragma once

#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanWindow.h"
#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "FrameGraph/SharedBufferRegistry.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Resources/ResourceManager.h"
#include "Resources/GltfSceneLoader.h"
#include "ECS/Scene.h"
#include "ECS/Systems.h"
#include "ECS/CameraController.h"
#include "ECS/RenderComponents.h"
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include "IBL/IBLGenerator.h"

#include <memory>
#include <vector>
#include <chrono>
#include "ECS/InputSystem.h"
#include "ECS/CameraControllerBuilders.h"

class DeclarativeSponzaApp {
public:
    DeclarativeSponzaApp();
    ~DeclarativeSponzaApp();

    void init();
    void run();
    void cleanup();

private:
    // ═══════════════════════════════════════════════════════════════
    // Vulkan Core
    // ═══════════════════════════════════════════════════════════════
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

    // ═══════════════════════════════════════════════════════════════
    // Frame Graph (Declarative v3)
    // ═══════════════════════════════════════════════════════════════
    std::unique_ptr<FrameGraph::RenderGraph> m_renderGraph;
    std::unique_ptr<FrameGraph::SharedBufferRegistry> m_sharedBufferRegistry;

    // NOTE: Camera UBO is now auto-managed by standardBuffers in JSON!
    // No manual m_cameraUniformBuffer needed - this is the v3 declarative approach

    // ═══════════════════════════════════════════════════════════════
    // ECS Scene
    // ═══════════════════════════════════════════════════════════════
    std::unique_ptr<ECS::SceneManager> m_sceneManager;
    std::shared_ptr<ECS::Scene> m_activeScene;
    entt::entity m_cameraEntity = entt::null;
    std::unique_ptr<ECS::StandaloneInputHandler> m_inputHandler;

    // ═══════════════════════════════════════════════════════════════
    // IBL Resources
    // ═══════════════════════════════════════════════════════════════
    IBLResources m_iblResources;
    std::string m_hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";

    // ═══════════════════════════════════════════════════════════════
    // Particle System — GPU compute + SSBO
    // ═══════════════════════════════════════════════════════════════
    static constexpr uint32_t PARTICLE_COUNT = 50000;
    float m_particleTime = 0.0f;

    // ═══════════════════════════════════════════════════════════════
    // Frame Synchronization
    // ═══════════════════════════════════════════════════════════════
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;

    // Timing
    std::chrono::high_resolution_clock::time_point m_startTime;
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_deltaTime = 0.0f;

    // ═══════════════════════════════════════════════════════════════
    // Initialization Methods
    // ═══════════════════════════════════════════════════════════════
    void initializeVulkan();
    void initializeECS();
    void loadIBLTextures();
    void loadGltfScene();
    void createLights();
    void initializeRenderGraph();
    void bindIBLTextures();
    void createSyncObjects();
    void setupEventHandlers();

    // ═══════════════════════════════════════════════════════════════
    // Rendering Methods
    // ═══════════════════════════════════════════════════════════════
    void update();
    void render();
    void presentFrame(uint32_t imageIndex);
    void updateParticleParams();

    // Scene rendering is automatic via JSON entityDataBinding configuration

    // Utility
    void handleSwapChainRecreation();
};
