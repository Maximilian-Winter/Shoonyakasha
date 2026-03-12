//
// BloomTestApp.cpp - Bloom Post-Process with Async Compute
//
// 朱雀之焰  煉其精純
// The Vermilion Bird's flame refines to purity
//
// Now fully declarative — samplers, descriptor bindings, and execution
// defined in bloom_pipeline.json
//

#include "BloomTestApp.h"

#include <cmath>

BloomTestApp::BloomTestApp() = default;
BloomTestApp::~BloomTestApp() = default;

void BloomTestApp::init() {
    m_logger = std::make_unique<Logger>("bloom_test.log");
    m_eventDispatcher = std::make_unique<EventDispatcher>();
    m_logger->log(LogLevel::Info, "Initializing Bloom Test App");

    initVulkan();
    initRenderGraph();
    createSyncObjects();

    m_lastFrameTime = std::chrono::high_resolution_clock::now();
    m_logger->log(LogLevel::Info, "Bloom Test App initialized");
}

void BloomTestApp::initVulkan() {
    m_instance = std::make_unique<VulkanInstance>(true);
    m_window = std::make_unique<VulkanWindow>(1280, 720, "Shoonyakasha - Bloom Test (Declarative)",
                                              *m_instance, m_eventDispatcher.get(), m_logger.get());
    m_device = std::make_unique<VulkanDevice>(*m_instance, m_window->getSurface());
    m_swapChain = std::make_unique<VulkanSwapChain>(*m_device, m_window->getSurface(),
                                                    m_window->getWindowExtent(m_device->getPhysicalDevice()));
    m_commandManager = std::make_unique<VulkanCommandManager>(*m_device);

    m_commandBuffers = m_commandManager->createCommandBuffers(MAX_FRAMES_IN_FLIGHT);

    // Create UBOs — still app-managed since they contain app-specific data
    m_cameraUBO = std::make_unique<TypedUniformBuffer<BloomCameraUBO>>(*m_device, MAX_FRAMES_IN_FLIGHT);
    m_bloomParamsUBO = std::make_unique<TypedUniformBuffer<BloomParamsUBO>>(*m_device, MAX_FRAMES_IN_FLIGHT);

    // Note: Samplers are now created by the frame graph from JSON configuration
    // No manual VkSampler creation needed!

    m_logger->log(LogLevel::Info, "Vulkan initialized (dedicated compute: %s)",
                  m_device->hasDedicatedComputeQueue() ? "yes" : "no");
}

void BloomTestApp::initRenderGraph() {
    m_renderGraph = std::make_unique<FrameGraph::RenderGraph>(*m_device, *m_commandManager);
    m_renderGraph->loadFromFile("bloom_pipeline.json");

    // Import swapchain images
    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    for (uint32_t i = 0; i < m_swapChain->getImageCount(); ++i) {
        m_renderGraph->importSwapchainImage(
            i, m_swapChain->getSwapChainImage(i), m_swapChain->getSwapChainImageView(i),
            m_swapChain->getSwapChainImageFormat(), extent
        );
    }

    // ALL passes are now fully declarative!
    // - ForwardPass:     execution.type = "fullscreen", push constants via setParameter()
    // - BrightExtract:   execution.type = "compute_dispatch" with resource-based dimensions
    // - BlurHorizontal:  execution.type = "compute_dispatch"
    // - BlurVertical:    execution.type = "compute_dispatch"
    // - Composite:       execution.type = "fullscreen"
    //
    // All passes use auto-execution from JSON:
    // - BrightExtract:    execution.type = "compute_dispatch" with resource-based dimensions
    // - BlurHorizontal:   execution.type = "compute_dispatch"
    // - BlurVertical:     execution.type = "compute_dispatch"
    // - Composite:        execution.type = "fullscreen"
    //
    // Descriptor sets are auto-bound via autoBindResource/autoBindSampler in JSON

    // Compile — samplers and descriptor bindings are created automatically
    VkExtent2D compileExtent = m_swapChain->getSwapChainExtent();
    m_renderGraph->compile(compileExtent, static_cast<uint32_t>(m_swapChain->getImageCount()), MAX_FRAMES_IN_FLIGHT);

    // Note: No bindDescriptorSets() call needed!
    // Auto-binding handles all resource→descriptor connections

    m_logger->log(LogLevel::Info, "Render graph compiled successfully (declarative mode)");
}

void BloomTestApp::createSyncObjects() {
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

void BloomTestApp::update(float dt) {
    m_elapsedTime += dt;

    // Update named parameters (for auto-push constants)
    m_renderGraph->setParameter("elapsedTime", m_elapsedTime);

    // Update camera — app-specific animation logic
    BloomCameraUBO cam;
    float camDist = 5.0f;
    glm::vec3 eye(
        std::sin(m_elapsedTime * 0.3f) * camDist,
        2.0f,
        std::cos(m_elapsedTime * 0.3f) * camDist
    );
    cam.view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    cam.proj = glm::perspective(glm::radians(60.0f),
        static_cast<float>(extent.width) / static_cast<float>(extent.height),
        0.1f, 100.0f);
    cam.proj[1][1] *= -1;
    cam.model = glm::mat4(1.0f);
    m_cameraUBO->update(m_currentFrame, cam);

    // Update bloom params — animate threshold for visual testing
    BloomParamsUBO bloomParams;
    bloomParams.threshold = 0.8f + std::sin(m_elapsedTime) * 0.2f;
    bloomParams.softKnee = 0.5f;
    bloomParams.intensity = 1.5f;
    bloomParams.padding = 0.0f;
    m_bloomParamsUBO->update(m_currentFrame, bloomParams);
}

void BloomTestApp::render() {
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

    VkExtent2D extent = m_swapChain->getSwapChainExtent();
    m_renderGraph->importSwapchainImage(
        imageIndex,
        m_swapChain->getSwapChainImage(imageIndex),
        m_swapChain->getSwapChainImageView(imageIndex),
        m_swapChain->getSwapChainImageFormat(),
        extent
    );

    // Record commands
    VkCommandBuffer cmdBuf = m_commandBuffers[m_currentFrame];
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

void BloomTestApp::run() {
    m_logger->log(LogLevel::Info, "Starting bloom test loop");

    while (!m_window->shouldClose()) {
        m_window->pollEvents();

        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - m_lastFrameTime).count();
        m_lastFrameTime = now;
        dt = std::min(dt, 0.033f);

        update(dt);
        render();
    }

    vkDeviceWaitIdle(m_device->getLogicalDevice());
}

void BloomTestApp::handleResize() {
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

    // Note: No rebindDescriptorSets() needed — auto-binding handles this
}

void BloomTestApp::cleanup() {
    vkDeviceWaitIdle(m_device->getLogicalDevice());

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(m_device->getLogicalDevice(), m_imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_device->getLogicalDevice(), m_renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_device->getLogicalDevice(), m_inFlightFences[i], nullptr);
    }

    // Note: No sampler cleanup needed — frame graph manages samplers from JSON

    m_renderGraph.reset();
    m_cameraUBO.reset();
    m_bloomParamsUBO.reset();
    m_commandManager.reset();
    m_swapChain.reset();
    m_device.reset();
    m_window.reset();
    m_instance.reset();

    m_logger->log(LogLevel::Info, "Bloom Test App cleaned up");
}
