//
// ParticleTestApp.cpp - GPU Particle Simulation with Async Compute
//
// 萬粒如星  星如塵  塵歸虛
// Ten thousand particles like stars, stars like dust, dust returns to void
//

#include "ParticleTestApp.h"

#include <random>
#include <cmath>

ParticleTestApp::ParticleTestApp() = default;
ParticleTestApp::~ParticleTestApp() = default;

void ParticleTestApp::init() {
    m_logger = std::make_unique<Logger>("particle_test.log");
    m_eventDispatcher = std::make_unique<EventDispatcher>();
    m_logger->log(LogLevel::Info, "Initializing Particle Test App");

    initVulkan();
    initParticleBuffers();
    initRenderGraph();
    createSyncObjects();

    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_logger->log(LogLevel::Info, "Particle Test App initialized with %u particles", PARTICLE_COUNT);
}

void ParticleTestApp::initVulkan() {
    m_instance = std::make_unique<VulkanInstance>(true);
    m_window = std::make_unique<VulkanWindow>(3840, 2160, "Shoonyakasha - Particle Test (Async Compute)",
                                              *m_instance, m_eventDispatcher.get(), m_logger.get());
    m_device = std::make_unique<VulkanDevice>(*m_instance, m_window->getSurface());
    m_swapChain = std::make_unique<VulkanSwapChain>(*m_device, m_window->getSurface(),
                                                    m_window->getWindowExtent(m_device->getPhysicalDevice()));
    m_commandManager = std::make_unique<VulkanCommandManager>(*m_device);

    // Create command buffers for both queues
    m_graphicsCommandBuffers = m_commandManager->createCommandBuffers(MAX_FRAMES_IN_FLIGHT);
    if (m_device->hasDedicatedComputeQueue()) {
        // Allocate compute command buffers from compute command pool
        m_computeCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.commandPool = m_device->getComputeCommandPool();
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(m_device->getLogicalDevice(), &allocInfo, &m_computeCommandBuffers[i]);
        }
    }

    m_logger->log(LogLevel::Info, "Vulkan initialized (dedicated compute: %s)",
                  m_device->hasDedicatedComputeQueue() ? "yes" : "no");
}

void ParticleTestApp::initParticleBuffers() {
    VkDeviceSize bufferSize = sizeof(ParticleData) * PARTICLE_COUNT;

    // Create two SSBO buffers for ping-pong
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    for (int i = 0; i < 2; ++i) {
        m_particleBuffers[i] = std::make_unique<VulkanBuffer>(
            *m_device, bufferSize, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    }

    // Initialize particles with random data
    std::vector<ParticleData> particles(PARTICLE_COUNT);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> posDist(-5.0f, 5.0f);
    std::uniform_real_distribution<float> velDist(-2.0f, 2.0f);
    std::uniform_real_distribution<float> lifeDist(1.0f, 5.0f);

    for (auto& p : particles) {
        p.position = glm::vec4(posDist(rng), posDist(rng), posDist(rng), lifeDist(rng));
        p.velocity = glm::vec4(velDist(rng), velDist(rng) + 3.0f, velDist(rng), 1.0f);
    }

    // Upload to both buffers via staging
    auto staging = std::make_unique<VulkanBuffer>(
        *m_device, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    // Use VulkanBuffer's map/copyFrom/unmap API
    staging->map();
    staging->copyFrom(particles.data(), bufferSize);
    staging->unmap();

    m_device->copyBuffer(staging->getBuffer(), m_particleBuffers[0]->getBuffer(), bufferSize);
    m_device->copyBuffer(staging->getBuffer(), m_particleBuffers[1]->getBuffer(), bufferSize);

    // Create uniform buffers
    m_simParamsUBO = std::make_unique<TypedUniformBuffer<SimParamsUBO>>(*m_device, MAX_FRAMES_IN_FLIGHT);
    m_cameraUBO = std::make_unique<TypedUniformBuffer<CameraUBO>>(*m_device, MAX_FRAMES_IN_FLIGHT);

    m_logger->log(LogLevel::Info, "Particle buffers created: %u particles, %.1f MB each",
                  PARTICLE_COUNT, bufferSize / (1024.0f * 1024.0f));
}

void ParticleTestApp::initRenderGraph() {
    m_renderGraph = std::make_unique<FrameGraph::RenderGraph>(*m_device, *m_commandManager);
    m_renderGraph->loadFromFile("particle_pipeline.json");

    // Import swapchain images
    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    for (uint32_t i = 0; i < m_swapChain->getImageCount(); ++i) {
        m_renderGraph->importSwapchainImage(
            i, m_swapChain->getSwapChainImage(i), m_swapChain->getSwapChainImageView(i),
            m_swapChain->getSwapChainImageFormat(), extent
        );
    }

    // Register external buffers for auto-binding
    // Particle SSBOs — compute reads from [0], writes to [1]; render reads from [1]
    m_renderGraph->registerStorageBuffer("particlesIn",
        m_particleBuffers[0]->getBuffer(), sizeof(ParticleData) * PARTICLE_COUNT);
    m_renderGraph->registerStorageBuffer("particlesOut",
        m_particleBuffers[1]->getBuffer(), sizeof(ParticleData) * PARTICLE_COUNT);

    // Uniform buffers — per-frame
    m_renderGraph->registerUniformBuffer("simParams",
        m_simParamsUBO->getBuffers(), sizeof(SimParamsUBO));
    m_renderGraph->registerUniformBuffer("camera",
        m_cameraUBO->getBuffers(), sizeof(CameraUBO));

    // Set initial parameters
    m_renderGraph->setParameter("particleCount", PARTICLE_COUNT);
    m_renderGraph->setParameter("frameNumber", m_frameNumber);

    // All passes are now fully declarative!
    // - ParticleSimulate: execution.type = "compute_dispatch" with parameter-based dispatch
    // - ParticleRender:   execution.type = "draw" with parameter-based vertexCount
    // Descriptor sets are auto-bound via autoBindBuffer in JSON

    // Compile the render graph
    VkExtent2D compileExtent = m_swapChain->getSwapChainExtent();
    m_renderGraph->compile(compileExtent, static_cast<uint32_t>(m_swapChain->getImageCount()), MAX_FRAMES_IN_FLIGHT);

    m_logger->log(LogLevel::Info, "Render graph compiled successfully (declarative mode)");
}

void ParticleTestApp::createSyncObjects() {
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkCreateSemaphore(m_device->getLogicalDevice(), &semInfo, nullptr, &m_imageAvailableSemaphores[i]);
        vkCreateSemaphore(m_device->getLogicalDevice(), &semInfo, nullptr, &m_renderFinishedSemaphores[i]);
        vkCreateFence(m_device->getLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]);
    }
}

void ParticleTestApp::update(float dt) {
    m_elapsedTime += dt;

    // Update named parameters for auto-push constants
    m_renderGraph->setParameter("frameNumber", m_frameNumber);

    // Update simulation parameters
    SimParamsUBO simParams;
    simParams.deltaTime = dt;
    simParams.gravity = 2.0f;
    simParams.particleCount = PARTICLE_COUNT;
    simParams.boundaryRadius = 15.0f;
    // Orbiting attractor
    simParams.attractorPos = glm::vec4(
        std::sin(m_elapsedTime * 0.5f) * 5.0f,
        std::cos(m_elapsedTime * 0.3f) * 3.0f,
        std::cos(m_elapsedTime * 0.5f) * 5.0f,
        50.0f  // strength
    );
    m_simParamsUBO->update(m_currentFrame, simParams);

    // Update camera
    CameraUBO cam;
    float camDist = 20.0f;
    glm::vec3 eye(
        std::sin(m_elapsedTime * 0.2f) * camDist,
        8.0f,
        std::cos(m_elapsedTime * 0.2f) * camDist
    );
    cam.view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    cam.proj = glm::perspective(glm::radians(60.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.1f, 100.0f);
    cam.proj[1][1] *= -1; // Vulkan Y-flip
    m_cameraUBO->update(m_currentFrame, cam);

    // Swap ping-pong buffers
    m_currentBuffer = 1 - m_currentBuffer;
    m_frameNumber++;
}

void ParticleTestApp::render() {
    vkWaitForFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device->getLogicalDevice(),
        m_swapChain->getSwapChain(), UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        handleResize();
        return;
    }

    vkResetFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame]);

    // Update per-frame swapchain image
    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    m_renderGraph->importSwapchainImage(
        imageIndex,
        m_swapChain->getSwapChainImage(imageIndex),
        m_swapChain->getSwapChainImageView(imageIndex),
        m_swapChain->getSwapChainImageFormat(),
        extent
    );

    // Record and execute — single queue path for now
    VkCommandBuffer cmdBuf = m_graphicsCommandBuffers[m_currentFrame];
    vkResetCommandBuffer(cmdBuf, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    m_renderGraph->execute(m_currentFrame, imageIndex, cmdBuf);

    vkEndCommandBuffer(cmdBuf);

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphores[m_currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, m_inFlightFences[m_currentFrame]);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = { m_swapChain->getSwapChain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        handleResize();
    }

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void ParticleTestApp::run() {
    m_logger->log(LogLevel::Info, "Starting particle simulation loop");

    while (!m_window->shouldClose()) {
        m_window->pollEvents();

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
        m_lastFrameTime = now;

        // Clamp delta time to prevent physics explosion
        dt = std::min(dt, 0.033f);

        update(dt);
        render();
    }

    vkDeviceWaitIdle(m_device->getLogicalDevice());
}

void ParticleTestApp::handleResize() {
    vkDeviceWaitIdle(m_device->getLogicalDevice());
    m_swapChain->recreate();

    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    for (uint32_t i = 0; i < m_swapChain->getImageCount(); ++i) {
        m_renderGraph->importSwapchainImage(
            i, m_swapChain->getSwapChainImage(i), m_swapChain->getSwapChainImageView(i),
            m_swapChain->getSwapChainImageFormat(), extent
        );
    }
    m_renderGraph->recompile(extent, static_cast<uint32_t>(m_swapChain->getImageCount()), MAX_FRAMES_IN_FLIGHT);
}

void ParticleTestApp::cleanup() {
    vkDeviceWaitIdle(m_device->getLogicalDevice());

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_device->getLogicalDevice(), m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device->getLogicalDevice(), m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_device->getLogicalDevice(), m_inFlightFences[i], nullptr);
    }

    m_renderGraph.reset();
    m_simParamsUBO.reset();
    m_cameraUBO.reset();
    m_particleBuffers[0].reset();
    m_particleBuffers[1].reset();
    m_commandManager.reset();
    m_swapChain.reset();
    m_device.reset();
    m_window.reset();
    m_instance.reset();

    m_logger->log(LogLevel::Info, "Particle Test App cleaned up");
}
