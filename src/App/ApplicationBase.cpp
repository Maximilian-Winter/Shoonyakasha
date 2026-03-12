//
// ApplicationBase.cpp - Common application boilerplate for Shoonyakasha
//
// 黃帝司中  調和而統御
// The Yellow Emperor governs the center — harmonizing all subsystems
//

#include "App/ApplicationBase.h"

#include "Vulkan/VulkanInstance.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanWindow.h"
#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanCommandBuffer.h"
#include "Vulkan/VulkanDescriptorSystem.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "FrameGraph/SharedBufferRegistry.h"
#include "Resources/ResourceManager.h"
#include "Resources/GltfSceneLoader.h"
#include "ECS/Core.h"
#include "ECS/Scene.h"
#include "ECS/Systems.h"
#include "ECS/CameraController.h"
#include "ECS/CameraControllerBuilders.h"
#include "ECS/InputSystem.h"
#include "ECS/RenderComponents.h"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

ApplicationBase::ApplicationBase(const ApplicationConfig& config)
    : m_config(config)
{
    m_startTime = std::chrono::high_resolution_clock::now();
    m_lastFrameTime = m_startTime;
}

ApplicationBase::~ApplicationBase() {
    cleanup();
    m_iblResources.destroy();
}

// ═══════════════════════════════════════════════════════════════
// Main Entry Point
// ═══════════════════════════════════════════════════════════════

void ApplicationBase::run() {
    // ── Initialize ──
    m_logger = std::make_unique<Logger>(m_config.logFile.c_str());
    m_logger->setLogLevel(m_config.logLevel);
    m_eventDispatcher = std::make_unique<EventDispatcher>();
    m_logger->log(LogLevel::Info, "Initializing %s", m_config.title.c_str());

    try {
        initializeVulkan();
        initializeECS();
        loadIBLTextures();

        onInit();  // Derived class: load scenes, create entities, etc.

        initializeRenderGraph();
        bindIBLTextures();
        createSyncObjects();
        setupEventHandlers();

        onPostInit();  // Derived class: post-init setup

        m_logger->log(LogLevel::Info, "%s initialized successfully", m_config.title.c_str());
    } catch (const std::exception& e) {
        m_logger->log(LogLevel::Error, "Failed to initialize: %s", e.what());
        throw;
    }

    // ── Main Loop ──
    m_logger->log(LogLevel::Info, "Starting main loop...");
    m_logger->log(LogLevel::Info, "Controls: WASD=move, Q/E=down/up, RMB=look, ESC=capture, 1/2/3=mode");

    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        update();
        render();
    }

    vkDeviceWaitIdle(m_device->getLogicalDevice());

    // ── Cleanup ──
    onCleanup();
}

// ═══════════════════════════════════════════════════════════════
// Initialization
// ═══════════════════════════════════════════════════════════════

void ApplicationBase::initializeVulkan() {
    m_instance = std::make_unique<VulkanInstance>(true);
    m_window = std::make_unique<VulkanWindow>(
        m_config.width, m_config.height, m_config.title.c_str(),
        *m_instance, m_eventDispatcher.get(), m_logger.get());
    m_device = std::make_unique<VulkanDevice>(*m_instance, m_window->getSurface());
    m_swapChain = std::make_unique<VulkanSwapChain>(
        *m_device, m_window->getSurface(),
        m_window->getWindowExtent(m_device->getPhysicalDevice()));

    m_resourceManager = std::make_unique<ResourceManager>(*m_device, m_config.resourceCacheSize);
    m_descriptorManager = std::make_unique<DescriptorManager>(*m_device);
    m_gltfLoader = std::make_unique<GltfSceneLoader>(*m_device);

    m_commandManager = std::make_unique<VulkanCommandManager>(*m_device);
    m_commandBuffers = m_commandManager->createCommandBuffers(m_swapChain->getImageCount());

    m_logger->log(LogLevel::Info, "Vulkan systems initialized");
}

void ApplicationBase::initializeECS() {
    m_sceneManager = std::make_unique<ECS::SceneManager>(*m_resourceManager, *m_device);
    m_activeScene = m_sceneManager->createScene("ApplicationScene");

    registerSystems();

    m_inputHandler = std::make_unique<ECS::StandaloneInputHandler>(*m_eventDispatcher, *m_window);

    m_activeScene->createEntity("InputState")
        .with<ECS::InputStateComponent>()
        .build();

    m_activeScene->initialize();

    m_logger->log(LogLevel::Info, "ECS initialized");
}

void ApplicationBase::registerSystems() {
    m_activeScene->addSystem<ECS::TransformSystem>();
    m_activeScene->addSystem<ECS::CameraSystem>();
    m_activeScene->addSystem<ECS::CameraControllerSystem>();
}

void ApplicationBase::loadIBLTextures() {
    if (m_config.hdrEnvironmentPath.empty()) {
        m_logger->log(LogLevel::Info, "No HDR environment path — skipping IBL generation");
        return;
    }

    m_logger->log(LogLevel::Info, "Generating IBL textures from HDR: %s",
                  m_config.hdrEnvironmentPath.c_str());

    try {
        IBLGenerator iblGenerator(*m_device, "shaders/ibl/");
        m_iblResources = iblGenerator.generate(m_config.hdrEnvironmentPath, m_config.iblParams);

        if (m_iblResources.isValid()) {
            m_logger->log(LogLevel::Info, "IBL textures generated successfully");
        } else {
            m_logger->log(LogLevel::Error, "Failed to generate IBL textures!");
        }
    } catch (const std::exception& e) {
        m_logger->log(LogLevel::Error, "IBL generation failed: %s", e.what());
    }
}

void ApplicationBase::initializeRenderGraph() {
    m_logger->log(LogLevel::Info, "Initializing render graph...");

    m_sharedBufferRegistry = std::make_unique<FrameGraph::SharedBufferRegistry>();

    m_renderGraph = std::make_unique<FrameGraph::RenderGraph>(*m_device, *m_commandManager);
    m_renderGraph->setSharedBufferRegistry(m_sharedBufferRegistry.get());

    m_renderGraph->loadFromFile(m_config.pipelineJsonPath);
    m_renderGraph->bindScene(m_activeScene.get(), m_resourceManager.get());
    m_renderGraph->setScreenExtent(m_swapChain->getSwapChainExtent());

    // Apply render graph parameters before compile (drives SSBO sizing, dispatch counts, etc.)
    for (const auto& [name, value] : m_config.renderGraphParameters) {
        m_renderGraph->setParameter(name, value);
        m_logger->log(LogLevel::Info, "RenderGraph parameter: %s = %u", name.c_str(), value);
    }

    uint32_t imageCount = static_cast<uint32_t>(m_swapChain->getImageCount());
    VkExtent2D extent = m_swapChain->getSwapChainExtent();

    for (uint32_t i = 0; i < imageCount; i++) {
        m_renderGraph->importSwapchainImage(
            i,
            m_swapChain->getSwapChainImage(i),
            m_swapChain->getSwapChainImageView(i),
            m_swapChain->getSwapChainImageFormat(),
            extent);
    }

    if (!m_renderGraph->compile(extent, imageCount)) {
        throw std::runtime_error("Failed to compile render graph: " + m_renderGraph->getLastError());
    }

    m_logger->log(LogLevel::Info, "Render graph compiled successfully");
}

void ApplicationBase::bindIBLTextures() {
    if (!m_iblResources.isValid()) {
        m_logger->log(LogLevel::Warning, "IBL resources not available — skipping texture binding");
        return;
    }

    // Bind to all IBL descriptor sets found in the pipeline
    const char* iblSetNames[] = { "iblSet", "transparentIBLSet" };

    for (const char* setName : iblSetNames) {
        auto iblSet = m_renderGraph->getDescriptorSet(setName);
        if (!iblSet) continue;

        for (uint32_t i = 0; i < m_swapChain->getImageCount(); i++) {
            iblSet->bindImage("irradianceMap", i,
                ImageResource{m_iblResources.irradianceMap->getCubeView(),
                             m_iblResources.irradianceMap->getSampler()});

            iblSet->bindImage("prefilterMap", i,
                ImageResource{m_iblResources.prefilterMap->getCubeView(),
                             m_iblResources.prefilterMap->getSampler()});

            iblSet->bindImage("brdfLUT", i,
                ImageResource{m_iblResources.brdfLUT->getImageView(),
                             m_iblResources.brdfLUT->getSampler()});

            iblSet->updateSet(i);
        }
        m_logger->log(LogLevel::Info, "IBL textures bound to %s", setName);
    }
}

void ApplicationBase::createSyncObjects() {
    m_imageAvailableSemaphores.resize(m_config.maxFramesInFlight);
    m_renderFinishedSemaphores.resize(m_config.maxFramesInFlight);
    m_inFlightFences.resize(m_config.maxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < m_config.maxFramesInFlight; i++) {
        vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
        vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
        vkCreateFence(m_device->getLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]);
    }
}

void ApplicationBase::setupEventHandlers() {
    m_eventDispatcher->subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event) {
        m_logger->log(LogLevel::Info, "Window resized to %dx%d", event.width, event.height);
        onResize(event.width, event.height);
        handleSwapChainRecreation();
    });

    m_eventDispatcher->subscribe<KeyEvent>([this](const KeyEvent& event) {
        if (!event.pressed) return;

        // Default camera mode switching (1/2/3)
        auto& registry = m_activeScene->getRegistry();
        auto* ctrl = registry.try_get<ECS::CameraControllerComponent>(m_cameraEntity);

        if (ctrl) {
            switch (event.keyCode) {
                case GLFW_KEY_1:
                    ctrl->mode = ECS::CameraControllerComponent::Mode::Free;
                    ctrl->initialized = false;
                    m_logger->log(LogLevel::Info, "Camera Mode: FREE");
                    break;
                case GLFW_KEY_2:
                    ctrl->mode = ECS::CameraControllerComponent::Mode::Orbit;
                    ctrl->initialized = false;
                    m_logger->log(LogLevel::Info, "Camera Mode: ORBIT");
                    break;
                case GLFW_KEY_3:
                    ctrl->mode = ECS::CameraControllerComponent::Mode::FirstPerson;
                    ctrl->initialized = false;
                    m_logger->log(LogLevel::Info, "Camera Mode: FIRST PERSON");
                    break;
            }
        }

        // Delegate to derived class
        onKeyPressed(event.keyCode);
    });
}

// ═══════════════════════════════════════════════════════════════
// Per-Frame Update
// ═══════════════════════════════════════════════════════════════

void ApplicationBase::update() {
    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;
    m_deltaTime = std::min(m_deltaTime, 0.1f);  // Clamp to prevent physics explosion

    m_inputHandler->beginFrame();

    // Copy input state to ECS
    auto& registry = m_activeScene->getRegistry();
    auto inputView = registry.view<ECS::InputStateComponent>();
    for (auto entity : inputView) {
        auto& inputComp = inputView.get<ECS::InputStateComponent>(entity);
        const auto& state = m_inputHandler->getInputState();
        inputComp.keys = state.keys;
        inputComp.mousePosition = state.mousePosition;
        inputComp.lastMousePosition = state.lastMousePosition;
        inputComp.mouseDelta = state.mouseDelta;
        inputComp.scrollDelta = state.scrollDelta;
        inputComp.mouseButtons = state.mouseButtons;
        inputComp.mouseCaptured = state.mouseCaptured;
        inputComp.firstMouseCapture = state.firstMouseCapture;
    }

    onUpdate(m_deltaTime);

    m_activeScene->update();

    m_inputHandler->endFrame();
    m_resourceManager->update();
}

// ═══════════════════════════════════════════════════════════════
// Rendering
// ═══════════════════════════════════════════════════════════════

void ApplicationBase::render() {
    vkWaitForFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device->getLogicalDevice(),
        m_swapChain->getSwapChain(),
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        handleSwapChainRecreation();
        return;
    }

    m_renderGraph->setScreenExtent(m_swapChain->getSwapChainExtent());
    m_renderGraph->updateSceneContext(m_deltaTime);
    m_renderGraph->updateStandardBuffers(m_deltaTime, m_currentFrame);

    onPreRender(m_deltaTime);

    vkResetFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame]);

    m_renderGraph->importSwapchainImage(
        imageIndex,
        m_swapChain->getSwapChainImage(imageIndex),
        m_swapChain->getSwapChainImageView(imageIndex),
        m_swapChain->getSwapChainImageFormat(),
        m_swapChain->getSwapChainExtent());

    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    m_commandManager->record(commandBuffer);
    m_renderGraph->execute(m_currentFrame, imageIndex, commandBuffer);
    m_commandManager->endRecording(commandBuffer);

    m_commandManager->submitCommandBuffers(
        {commandBuffer},
        m_device->getGraphicsQueue(),
        {m_imageAvailableSemaphores[m_currentFrame]},
        {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        {m_renderFinishedSemaphores[m_currentFrame]},
        m_inFlightFences[m_currentFrame]);

    presentFrame(imageIndex);
    m_currentFrame = (m_currentFrame + 1) % m_config.maxFramesInFlight;

    onPostRender();
}

void ApplicationBase::presentFrame(uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

    VkSwapchainKHR swapChains[] = {m_swapChain->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(m_device->getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        handleSwapChainRecreation();
    }
}

void ApplicationBase::handleSwapChainRecreation() {
    vkDeviceWaitIdle(m_device->getLogicalDevice());

    m_swapChain->recreate();

    m_commandManager->freeCommandBuffers(m_commandBuffers);
    m_commandBuffers = m_commandManager->createCommandBuffers(
        static_cast<uint32_t>(m_swapChain->getImageCount()));

    VkExtent2D newExtent = m_swapChain->getSwapChainExtent();
    uint32_t imageCount = static_cast<uint32_t>(m_swapChain->getImageCount());

    for (uint32_t i = 0; i < imageCount; i++) {
        m_renderGraph->importSwapchainImage(
            i,
            m_swapChain->getSwapChainImage(i),
            m_swapChain->getSwapChainImageView(i),
            m_swapChain->getSwapChainImageFormat(),
            newExtent);
    }

    if (!m_renderGraph->recompile(newExtent, imageCount)) {
        throw std::runtime_error("Failed to recompile render graph: " + m_renderGraph->getLastError());
    }

    bindIBLTextures();

    m_logger->log(LogLevel::Info, "Swap chain recreated: %ux%u", newExtent.width, newExtent.height);
}

void ApplicationBase::cleanup() {
    if (m_device) {
        vkDeviceWaitIdle(m_device->getLogicalDevice());

        for (auto semaphore : m_imageAvailableSemaphores) {
            vkDestroySemaphore(m_device->getLogicalDevice(), semaphore, nullptr);
        }
        m_imageAvailableSemaphores.clear();

        for (auto semaphore : m_renderFinishedSemaphores) {
            vkDestroySemaphore(m_device->getLogicalDevice(), semaphore, nullptr);
        }
        m_renderFinishedSemaphores.clear();

        for (auto fence : m_inFlightFences) {
            vkDestroyFence(m_device->getLogicalDevice(), fence, nullptr);
        }
        m_inFlightFences.clear();
    }

    if (m_logger) {
        m_logger->log(LogLevel::Info, "%s cleaned up", m_config.title.c_str());
    }
}

// ═══════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════

VulkanDevice& ApplicationBase::getDevice() { return *m_device; }
VulkanWindow& ApplicationBase::getWindow() { return *m_window; }
VulkanSwapChain& ApplicationBase::getSwapChain() { return *m_swapChain; }
FrameGraph::RenderGraph& ApplicationBase::getRenderGraph() { return *m_renderGraph; }
ECS::Scene& ApplicationBase::getScene() { return *m_activeScene; }
entt::registry& ApplicationBase::getRegistry() { return m_activeScene->getRegistry(); }
ResourceManager& ApplicationBase::getResourceManager() { return *m_resourceManager; }
GltfSceneLoader& ApplicationBase::getGltfLoader() { return *m_gltfLoader; }
Logger& ApplicationBase::getLogger() { return *m_logger; }
EventDispatcher& ApplicationBase::getEventDispatcher() { return *m_eventDispatcher; }
ECS::StandaloneInputHandler& ApplicationBase::getInputHandler() { return *m_inputHandler; }

// ═══════════════════════════════════════════════════════════════
// Convenience Helpers
// ═══════════════════════════════════════════════════════════════

entt::entity ApplicationBase::createCamera(const glm::vec3& pos, float fov,
                                            float speed, float nearPlane, float farPlane) {
    m_cameraEntity = m_activeScene->createEntity("MainCamera")
        .withTransform(pos)
        .withCamera(true)
        .withFreeCameraController(speed)
        .build();

    auto& registry = m_activeScene->getRegistry();

    auto& camera = registry.get<ECS::CameraComponent>(m_cameraEntity);
    camera.fov = fov;
    camera.nearPlane = nearPlane;
    camera.farPlane = farPlane;

    auto& transform = registry.get<ECS::TransformComponent>(m_cameraEntity);
    transform.rotation = glm::vec3(0.0f);
    transform.isDirty = true;

    auto& controller = registry.get<ECS::CameraControllerComponent>(m_cameraEntity);
    controller.moveSpeed = speed;
    controller.smoothing = 12.0f;
    controller.mouseSensitivity = 0.003f;

    m_logger->log(LogLevel::Info, "Camera created at (%.1f, %.1f, %.1f) FOV=%.0f",
                  pos.x, pos.y, pos.z, fov);

    return m_cameraEntity;
}

GltfLoadResult ApplicationBase::loadGltfScene(const std::string& path,
                                                const GltfLoadOptions& opts) {
    m_logger->log(LogLevel::Info, "Loading glTF scene: %s", path.c_str());

    auto result = m_gltfLoader->load(path, m_activeScene, opts);

    if (result.success) {
        m_logger->log(LogLevel::Info, "glTF loaded: %zu entities, %zu vertices, %zu indices",
                      result.entities.size(), result.totalVertices, result.totalIndices);
    } else {
        m_logger->log(LogLevel::Error, "Failed to load glTF: %s", result.error.c_str());
    }

    return result;
}

entt::entity ApplicationBase::createDirectionalLight(const glm::vec3& direction,
                                                      const glm::vec3& color,
                                                      float intensity) {
    auto& registry = m_activeScene->getRegistry();
    auto entity = registry.create();

    auto& transform = registry.emplace<ECS::TransformComponent>(entity);
    glm::vec3 dir = glm::normalize(direction);
    transform.rotation = glm::vec3(asinf(-dir.y), atan2f(-dir.x, -dir.z), 0.0f);
    transform.position = glm::vec3(0.0f, 50.0f, 0.0f);

    auto& light = registry.emplace<ECS::LightComponent>(entity);
    light.type = ECS::LightComponent::Directional;
    light.color = color;
    light.intensity = intensity;

    m_logger->log(LogLevel::Info, "Created directional light (intensity=%.1f)", intensity);
    return entity;
}

entt::entity ApplicationBase::createPointLight(const glm::vec3& position,
                                                const glm::vec3& color,
                                                float intensity, float range) {
    auto& registry = m_activeScene->getRegistry();
    auto entity = registry.create();

    auto& transform = registry.emplace<ECS::TransformComponent>(entity);
    transform.position = position;

    auto& light = registry.emplace<ECS::LightComponent>(entity);
    light.type = ECS::LightComponent::Point;
    light.color = color;
    light.intensity = intensity;
    light.range = range;

    m_logger->log(LogLevel::Info, "Created point light at (%.1f, %.1f, %.1f) range=%.1f",
                  position.x, position.y, position.z, range);
    return entity;
}

} // namespace Shoonyakasha
