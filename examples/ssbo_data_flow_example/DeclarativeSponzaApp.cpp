#include "DeclarativeSponzaApp.h"

#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <filesystem>

#include "ECS/RenderComponents.h"

DeclarativeSponzaApp::DeclarativeSponzaApp() {
    m_startTime = std::chrono::high_resolution_clock::now();
    m_lastFrameTime = m_startTime;
}

DeclarativeSponzaApp::~DeclarativeSponzaApp() {
    cleanup();
    m_iblResources.destroy();
}

void DeclarativeSponzaApp::cleanup() {
    if (m_device) {
        vkDeviceWaitIdle(m_device->getLogicalDevice());

        for (auto semaphore : m_imageAvailableSemaphores) {
            vkDestroySemaphore(m_device->getLogicalDevice(), semaphore, nullptr);
        }
        for (auto semaphore : m_renderFinishedSemaphores) {
            vkDestroySemaphore(m_device->getLogicalDevice(), semaphore, nullptr);
        }
        for (auto fence : m_inFlightFences) {
            vkDestroyFence(m_device->getLogicalDevice(), fence, nullptr);
        }
    }

    if (m_logger) {
        m_logger->log(LogLevel::Info, "Declarative Sponza App cleaned up");
    }
}

void DeclarativeSponzaApp::init() {
    m_logger = std::make_unique<Logger>("declarative_sponza.log");
    m_logger->setLogLevel(LogLevel::Info);
    m_eventDispatcher = std::make_unique<EventDispatcher>();
    m_logger->log(LogLevel::Info, "Initializing Declarative Sponza App");

    try {
        initializeVulkan();
        initializeECS();
        loadIBLTextures();
        loadGltfScene();
        createLights();
        initializeRenderGraph();

        bindIBLTextures();
        createSyncObjects();
        setupEventHandlers();

        m_logger->log(LogLevel::Info, "Declarative Sponza App initialized successfully");
    } catch (const std::exception& e) {
        m_logger->log(LogLevel::Error, "Failed to initialize: %s", e.what());
        throw;
    }
}

void DeclarativeSponzaApp::initializeVulkan() {
    m_instance = std::make_unique<VulkanInstance>(true);
    m_window = std::make_unique<VulkanWindow>(1600, 900, "Declarative Sponza - v3 Frame Graph",
                                              *m_instance, m_eventDispatcher.get(), m_logger.get());
    m_device = std::make_unique<VulkanDevice>(*m_instance, m_window->getSurface());
    m_swapChain = std::make_unique<VulkanSwapChain>(*m_device, m_window->getSurface(),
                                                    m_window->getWindowExtent(m_device->getPhysicalDevice()));

    m_resourceManager = std::make_unique<ResourceManager>(*m_device, 2048 * 1024 * 1024);

    m_descriptorManager = std::make_unique<DescriptorManager>(*m_device);
    m_gltfLoader = std::make_unique<GltfSceneLoader>(*m_device);

    m_commandManager = std::make_unique<VulkanCommandManager>(*m_device);
    m_commandBuffers = m_commandManager->createCommandBuffers(m_swapChain->getImageCount());

    m_logger->log(LogLevel::Info, "Vulkan systems initialized");
}

void DeclarativeSponzaApp::initializeECS() {
    m_sceneManager = std::make_unique<ECS::SceneManager>(*m_resourceManager, *m_device);
    m_activeScene = m_sceneManager->createScene("DeclarativeSponzaScene");

    // Systems must be added in correct order:
    // 1. TransformSystem - updates worldMatrix from position/rotation/scale
    // 2. CameraSystem - updates viewMatrix/projectionMatrix from camera's worldMatrix
    // 3. CameraControllerSystem - handles input to move the camera
    m_activeScene->addSystem<ECS::TransformSystem>();
    m_activeScene->addSystem<ECS::CameraSystem>();
    m_activeScene->addSystem<ECS::CameraControllerSystem>();

    m_inputHandler = std::make_unique<ECS::StandaloneInputHandler>(*m_eventDispatcher, *m_window);

    m_activeScene->createEntity("InputState")
        .with<ECS::InputStateComponent>()
        .build();

    m_activeScene->initialize();

    m_cameraEntity = m_activeScene->createEntity("MainCamera")
        .withTransform(glm::vec3(0.0f, 0.0f, 5.0f))  // Simple: 5 units in front of origin on +Z
        .withCamera(true)
        .withFreeCameraController(8.0f)
        .build();

    auto& camera = m_activeScene->getRegistry().get<ECS::CameraComponent>(m_cameraEntity);
    camera.fov = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 1000.0f;

    // No rotation - camera looks down -Z by default, which points at origin
    auto& cameraTransform = m_activeScene->getRegistry().get<ECS::TransformComponent>(m_cameraEntity);
    cameraTransform.rotation = glm::vec3(0.0f, 0.0f, 0.0f);  // No rotation
    cameraTransform.isDirty = true;

    auto& controller = m_activeScene->getRegistry().get<ECS::CameraControllerComponent>(m_cameraEntity);
    controller.moveSpeed = 8.0f;
    controller.smoothing = 12.0f;
    controller.mouseSensitivity = 0.003f;

    m_logger->log(LogLevel::Info, "ECS initialized with camera at (0, 0, 5) looking at origin");
}


void DeclarativeSponzaApp::loadIBLTextures() {
    m_logger->log(LogLevel::Info, "Generating IBL textures from HDR: %s", m_hdrEnvironmentPath.c_str());

    try {
        IBLGenerator iblGenerator(*m_device, "shaders/ibl/");

        IBLGenerationParams params;
        params.environmentSize = 1024;
        params.irradianceSize = 32;
        params.prefilterSize = 512;
        params.brdfLUTSize = 512;
        params.irradianceSamples = 2048;
        params.prefilterSamples = 1024;
        params.brdfSamples = 1024;

        m_iblResources = iblGenerator.generate(m_hdrEnvironmentPath, params);

        if (m_iblResources.isValid()) {
            m_logger->log(LogLevel::Info, "IBL textures generated successfully");
        } else {
            m_logger->log(LogLevel::Error, "Failed to generate IBL textures!");
        }
    } catch (const std::exception& e) {
        m_logger->log(LogLevel::Error, "IBL generation failed: %s", e.what());
    }
}

void DeclarativeSponzaApp::loadGltfScene() {
    m_logger->log(LogLevel::Info, "Loading glTF scene...");

    GltfLoadOptions options;
    options.loadTextures = true;
    options.createEntities = true;
    options.namePrefix = "sponza";
    options.maxTextureSize = 4096;

    auto result = m_gltfLoader->load(
        "Box.gltf",
        m_activeScene,
        options
    );

    if (!result.success) {
        throw std::runtime_error("Failed to load glTF: " + result.error);
    }

    m_logger->log(LogLevel::Info, "glTF loaded: %zu entities, %zu vertices, %zu indices",
                  result.entities.size(), result.totalVertices, result.totalIndices);

    // Count opaque vs transparent for logging
    auto& registry = m_activeScene->getRegistry();
    size_t opaqueCount = 0, transparentCount = 0;
    auto view = registry.view<Shoonyakasha::MaterialComponentV5>();
    for (auto entity : view) {
        const auto& mat = view.get<Shoonyakasha::MaterialComponentV5>(entity);
        if (mat.isTransparent()) {
            transparentCount++;
        } else {
            opaqueCount++;
        }
    }

    m_logger->log(LogLevel::Info, "Entities: %zu opaque, %zu transparent", opaqueCount, transparentCount);
}

void DeclarativeSponzaApp::createLights() {
    auto& registry = m_activeScene->getRegistry();

    // Sun (directional light) — replaces old hardcoded vec3(0.5, 1.0, 0.3)
    {
        auto sun = registry.create();
        auto& transform = registry.emplace<ECS::TransformComponent>(sun);

        // Compute Euler angles so getForward() returns the direction FROM the sun
        // Old hardcoded L was normalize(vec3(0.5, 1.0, 0.3)) — pointing toward light
        // Sun direction (away from light, toward scene) = -normalize(0.5, 1.0, 0.3)
        glm::vec3 dir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
        float pitch = asinf(-dir.y);
        float yaw = atan2f(-dir.x, -dir.z);
        transform.rotation = glm::vec3(pitch, yaw, 0.0f);
        transform.position = glm::vec3(0.0f, 50.0f, 0.0f);

        auto& light = registry.emplace<ECS::LightComponent>(sun);
        light.type = ECS::LightComponent::Directional;
        light.color = glm::vec3(1.0f, 0.975f, 0.95f);
        light.intensity = 2.0f;

        m_logger->log(LogLevel::Info, "Created Sun (directional light)");
    }

    // Warm point light in the atrium
    {
        auto pointLight = registry.create();
        auto& transform = registry.emplace<ECS::TransformComponent>(pointLight);
        transform.position = glm::vec3(0.0f, 3.0f, 0.0f);

        auto& light = registry.emplace<ECS::LightComponent>(pointLight);
        light.type = ECS::LightComponent::Point;
        light.color = glm::vec3(1.0f, 0.8f, 0.6f);
        light.intensity = 5.0f;
        light.range = 15.0f;

        m_logger->log(LogLevel::Info, "Created atrium point light");
    }
}

void DeclarativeSponzaApp::updateParticleParams() {
    m_particleTime += m_deltaTime;

    // Set particle simulation parameters via scene.custom.* dot-paths
    // The JSON bufferLayout "particleSimParams" references these sources
    auto& ctx = m_renderGraph->getSceneContext();

    ctx.setCustom("particles.gravity", 1.5f);                    // Gentle downward pull
    ctx.setCustom("particles.count", PARTICLE_COUNT);
    ctx.setCustom("particles.boundaryRadius", 15.0f);

    // Attractor: warm point light in atrium center (matches createLights)
    ctx.setCustom("particles.attractorPos", glm::vec4(0.0f, 5.0f, 0.0f, 25.0f));

    // Wind: gentle swirl with slow rotation
    float windAngle = m_particleTime * 0.2f;
    ctx.setCustom("particles.wind", glm::vec4(
        sinf(windAngle) * 0.4f,    // x: rotating horizontal
        0.1f,                       // y: slight upward
        cosf(windAngle) * 0.4f,    // z: rotating horizontal
        0.3f                        // w: turbulence strength
    ));

    ctx.setCustom("particles.damping", 0.998f);      // Slow air resistance
    ctx.setCustom("particles.spawnHeight", 0.5f);     // Respawn near floor
}

void DeclarativeSponzaApp::initializeRenderGraph() {
    m_logger->log(LogLevel::Info, "Initializing declarative render graph...");

    // Phase 2: Create shared buffer registry for cross-graph SSBO sharing
    m_sharedBufferRegistry = std::make_unique<FrameGraph::SharedBufferRegistry>();

    m_renderGraph = std::make_unique<FrameGraph::RenderGraph>(*m_device, *m_commandManager);
    m_renderGraph->setSharedBufferRegistry(m_sharedBufferRegistry.get());

    m_renderGraph->loadFromFile("pbr_ibl_pipeline_v3.json");

    m_renderGraph->bindScene(m_activeScene.get(), m_resourceManager.get());
    m_renderGraph->setScreenExtent(m_swapChain->getSwapChainExtent());

    // Particle SSBO is now declarative — created by JSON bufferLayouts with source initializer
    // The particleSimParams UBO is also declarative with dot-path sources
    m_renderGraph->setParameter("particleCount", PARTICLE_COUNT);

    // The frame graph auto-registers callbacks for passes with entityDataBinding.
    // JSON declares "entityDataBinding" in pass execution config.

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

    // Phase 3: Register readback callback for particle SSBO
    m_renderGraph->registerReadbackCallback("particleSSBO",
        [this](const ReadbackResult& result) {
            m_logger->log(LogLevel::Info, "Particle readback: %u elements, frame %llu",
                          result.elementCount, static_cast<unsigned long long>(result.frameNumber));
        });

    m_logger->log(LogLevel::Info, "Declarative render graph compiled successfully");
    m_logger->log(LogLevel::Info, "  - Standard buffers: camera, time, screen (auto-managed from ECS)");
    m_logger->log(LogLevel::Info, "  - Dot-paths resolve from MaterialComponentV5");

}


void DeclarativeSponzaApp::bindIBLTextures() {
    if (!m_iblResources.isValid()) {
        m_logger->log(LogLevel::Warning, "IBL resources not generated");
        return;
    }

    auto iblSet = m_renderGraph->getDescriptorSet("iblSet");
    if (iblSet) {
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
        m_logger->log(LogLevel::Info, "IBL textures bound to iblSet");
    } else {
        m_logger->log(LogLevel::Warning, "iblSet not found");
    }

    auto transparentIBLSet = m_renderGraph->getDescriptorSet("transparentIBLSet");
    if (transparentIBLSet) {
        for (uint32_t i = 0; i < m_swapChain->getImageCount(); i++) {
            transparentIBLSet->bindImage("irradianceMap", i,
                ImageResource{m_iblResources.irradianceMap->getCubeView(),
                             m_iblResources.irradianceMap->getSampler()});

            transparentIBLSet->bindImage("prefilterMap", i,
                ImageResource{m_iblResources.prefilterMap->getCubeView(),
                             m_iblResources.prefilterMap->getSampler()});

            transparentIBLSet->bindImage("brdfLUT", i,
                ImageResource{m_iblResources.brdfLUT->getImageView(),
                             m_iblResources.brdfLUT->getSampler()});

            transparentIBLSet->updateSet(i);
        }
        m_logger->log(LogLevel::Info, "IBL textures bound to transparentIBLSet");
    } else {
        m_logger->log(LogLevel::Warning, "transparentIBLSet not found");
    }
}

void DeclarativeSponzaApp::createSyncObjects() {
    const size_t MAX_FRAMES_IN_FLIGHT = 2;

    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]);
        vkCreateSemaphore(m_device->getLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]);
        vkCreateFence(m_device->getLogicalDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]);
    }
}

void DeclarativeSponzaApp::setupEventHandlers() {
    m_eventDispatcher->subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event) {
        m_logger->log(LogLevel::Info, "Window resized to %dx%d", event.width, event.height);
        handleSwapChainRecreation();
    });

    m_eventDispatcher->subscribe<KeyEvent>([this](const KeyEvent& event) {
        if (!event.pressed) return;

        auto& registry = m_activeScene->getRegistry();
        auto* ctrl = registry.try_get<ECS::CameraControllerComponent>(m_cameraEntity);
        if (!ctrl) return;

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
            case GLFW_KEY_SPACE:
                if (ctrl->mode == ECS::CameraControllerComponent::Mode::Free) {
                    ctrl->horizonLocked = !ctrl->horizonLocked;
                    m_logger->log(LogLevel::Info, "Horizon Lock: %s", ctrl->horizonLocked ? "ON" : "OFF");
                }
                break;
        }
    });
}

void DeclarativeSponzaApp::run() {
    m_logger->log(LogLevel::Info, "Starting main loop...");
    m_logger->log(LogLevel::Info, "Controls: WASD=move, Q/E=down/up, RMB=look, ESC=capture, 1/2/3=mode");

    while (!m_window->shouldClose()) {
        m_window->pollEvents();
        update();
        render();
    }

    vkDeviceWaitIdle(m_device->getLogicalDevice());
}

void DeclarativeSponzaApp::update() {

    auto currentTime = std::chrono::high_resolution_clock::now();
    m_deltaTime = std::chrono::duration<float>(currentTime - m_lastFrameTime).count();
    m_lastFrameTime = currentTime;
    m_deltaTime = std::min(m_deltaTime, 0.1f);

    m_inputHandler->beginFrame();

    auto& registry = m_activeScene->getRegistry();
    auto inputView = registry.view<ECS::InputStateComponent>();
    for (auto entity : inputView) {
        auto& inputComp = inputView.get<ECS::InputStateComponent>(entity);
        inputComp.keys = m_inputHandler->getInputState().keys;
        inputComp.mousePosition = m_inputHandler->getInputState().mousePosition;
        inputComp.lastMousePosition = m_inputHandler->getInputState().lastMousePosition;
        inputComp.mouseDelta = m_inputHandler->getInputState().mouseDelta;
        inputComp.scrollDelta = m_inputHandler->getInputState().scrollDelta;
        inputComp.mouseButtons = m_inputHandler->getInputState().mouseButtons;
        inputComp.mouseCaptured = m_inputHandler->getInputState().mouseCaptured;
        inputComp.firstMouseCapture = m_inputHandler->getInputState().firstMouseCapture;
    }

    m_activeScene->update();

    m_inputHandler->endFrame();
    m_resourceManager->update();
}

void DeclarativeSponzaApp::render() {
    vkWaitForFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device->getLogicalDevice(),
                                            m_swapChain->getSwapChain(),
                                            UINT64_MAX,
                                            m_imageAvailableSemaphores[m_currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        handleSwapChainRecreation();
        return;
    }

    m_renderGraph->setScreenExtent(m_swapChain->getSwapChainExtent());

    // Update scene context FIRST — populates SceneContext (camera, time, etc.)
    // Then update buffers — fills dot-path UBOs from the populated SceneContext
    m_renderGraph->updateSceneContext(m_deltaTime);
    m_renderGraph->updateStandardBuffers(m_deltaTime, m_currentFrame);
    updateParticleParams();

    vkResetFences(m_device->getLogicalDevice(), 1, &m_inFlightFences[m_currentFrame]);

    m_renderGraph->importSwapchainImage(
        imageIndex,
        m_swapChain->getSwapChainImage(imageIndex),
        m_swapChain->getSwapChainImageView(imageIndex),
        m_swapChain->getSwapChainImageFormat(),
        m_swapChain->getSwapChainExtent());

    VkCommandBuffer commandBuffer = m_commandBuffers[imageIndex];
    auto cmd = m_commandManager->record(commandBuffer);

    m_renderGraph->execute(m_currentFrame, imageIndex, commandBuffer);

    m_commandManager->endRecording(commandBuffer);

    m_commandManager->submitCommandBuffers(
        {commandBuffer},
        m_device->getGraphicsQueue(),
        {m_imageAvailableSemaphores[m_currentFrame]},
        {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        {m_renderFinishedSemaphores[m_currentFrame]},
        m_inFlightFences[m_currentFrame]
    );

    presentFrame(imageIndex);
    m_currentFrame = (m_currentFrame + 1) % 2;
}

void DeclarativeSponzaApp::presentFrame(uint32_t imageIndex) {
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

// Automatic rendering via JSON entityDataBinding configuration

void DeclarativeSponzaApp::handleSwapChainRecreation() {
    vkDeviceWaitIdle(m_device->getLogicalDevice());

    m_swapChain->recreate();

    m_commandManager->freeCommandBuffers(m_commandBuffers);
    m_commandBuffers = m_commandManager->createCommandBuffers(static_cast<uint32_t>(m_swapChain->getImageCount()));

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

