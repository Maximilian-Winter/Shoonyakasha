//
// BloomTestApp.h - Bloom Post-Process with Async Compute
//
// 明暗相融  光影交織
// Light and dark merge — brightness and shadow interweave
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

struct BloomCameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 model;
};

struct BloomParamsUBO {
    float threshold;
    float softKnee;
    float intensity;
    float padding;
};

class BloomTestApp {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    BloomTestApp();
    ~BloomTestApp();

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

    // Uniform buffers
    std::unique_ptr<TypedUniformBuffer<BloomCameraUBO>> m_cameraUBO;
    std::unique_ptr<TypedUniformBuffer<BloomParamsUBO>> m_bloomParamsUBO;

    // Command buffers
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Synchronization
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;

    // Timing
    std::chrono::high_resolution_clock::time_point m_lastFrameTime;
    float m_elapsedTime = 0.0f;

    // Init
    void initVulkan();
    void initRenderGraph();
    void createSyncObjects();

    // Loop
    void update(float dt);
    void render();
    void handleResize();
};
