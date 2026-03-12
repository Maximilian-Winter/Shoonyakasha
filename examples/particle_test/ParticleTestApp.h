//
// ParticleTestApp.h - GPU Particle Simulation with Async Compute
//
// 朱雀司變  萬粒並行
// The Vermilion Bird governs transformation — ten thousand particles in parallel
//

#pragma once
#define GLM_ENABLE_EXPERIMENTAL

#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanWindow.h"
#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanPipeline.h"
#include "Vulkan/VulkanComputePipeline.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanBuffer.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Core/EventSystem.h"
#include "Core/Logger.h"
#include "Vulkan/UniformBuffer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <chrono>

struct ParticleData {
    glm::vec4 position;   // xyz = pos, w = life
    glm::vec4 velocity;   // xyz = vel, w = mass
};

struct SimParamsUBO {
    float deltaTime;
    float gravity;
    uint32_t particleCount;
    float boundaryRadius;
    glm::vec4 attractorPos;   // xyz = pos, w = strength
};

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
};

class ParticleTestApp {
public:
    static constexpr uint32_t PARTICLE_COUNT = 100000;
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    ParticleTestApp();
    ~ParticleTestApp();

    void init();
    void run();
    void cleanup();

private:
    // Core
    std::unique_ptr<Logger> m_logger;
    std::unique_ptr<EventDispatcher> m_eventDispatcher;

    // Vulkan
    std::unique_ptr<VulkanInstance> m_instance;
    std::unique_ptr<VulkanDevice> m_device;
    std::unique_ptr<VulkanWindow> m_window;
    std::unique_ptr<VulkanSwapChain> m_swapChain;
    std::unique_ptr<VulkanCommandManager> m_commandManager;

    // Frame Graph
    std::unique_ptr<FrameGraph::RenderGraph> m_renderGraph;

    // Particle buffers (double-buffered for ping-pong)
    std::unique_ptr<VulkanBuffer> m_particleBuffers[2];
    uint32_t m_currentBuffer = 0;  // ping-pong index

    // Uniform buffers
    std::unique_ptr<TypedUniformBuffer<SimParamsUBO>> m_simParamsUBO;
    std::unique_ptr<TypedUniformBuffer<CameraUBO>> m_cameraUBO;

    // Command buffers
    std::vector<VkCommandBuffer> m_graphicsCommandBuffers;
    std::vector<VkCommandBuffer> m_computeCommandBuffers;

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;
    uint32_t m_frameNumber = 0;

    // Timing
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_elapsedTime = 0.0f;

    // Init
    void initVulkan();
    void initParticleBuffers();
    void initRenderGraph();
    void createSyncObjects();

    // Loop
    void update(float dt);
    void render();
    void handleResize();
};
