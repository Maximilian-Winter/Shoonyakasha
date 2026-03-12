# Camera Controller System Integration Guide
# 攝影機控制系統整合指南

## Overview

The Shoonyakasha Camera Controller System provides four modes of camera control:

1. **Free Camera** (`Mode::Free`) - Fly camera with WASD + mouse look
2. **Orbit Camera** (`Mode::Orbit`) - Rotate around a target point
3. **First Person** (`Mode::FirstPerson`) - FPS-style grounded camera
4. **Third Person** (`Mode::ThirdPerson`) - Follow camera behind a target entity

## Quick Start Integration

### Step 1: Include Headers

```cpp
#include "ECS/CameraController.h"
#include "ECS/CameraControllerBuilders.h"
#include "ECS/InputSystem.h"
```

### Step 2: Create Input System (in your app's initialization)

```cpp
// In your init() function, after creating ECS scene:
m_inputSystem = std::make_unique<ECS::InputSystem>(*m_eventDispatcher, *m_window);
m_activeScene->addSystem(m_inputSystem.get());

// Also add the camera controller system
auto* cameraControllerSystem = m_activeScene->addSystem<ECS::CameraControllerSystem>();
```

### Step 3: Create Camera with Controller

```cpp
// Option A: Orbit Camera (great for model viewers)
auto cameraEntity = m_activeScene->createEntity("MainCamera")
    .withTransform(glm::vec3(0.0f, 3.0f, 10.0f))
    .withCamera(true)
    .withOrbitCameraController(
        glm::vec3(0.0f, 1.5f, 0.0f),  // Target point
        10.0f,                         // Distance
        false                          // Auto-rotate
    )
    .build();

// Option B: Free Camera (developer/spectator mode)
auto cameraEntity = m_activeScene->createEntity("MainCamera")
    .withTransform(glm::vec3(0.0f, 2.0f, 5.0f))
    .withCamera(true)
    .withFreeCameraController(8.0f)  // Move speed
    .build();

// Option C: First Person Camera
auto cameraEntity = m_activeScene->createEntity("MainCamera")
    .withTransform(glm::vec3(0.0f, 0.0f, 0.0f))
    .withCamera(true)
    .withFirstPersonController(1.7f)  // Eye height
    .build();
```

### Step 4: Remove Old Camera Code

Remove the manual camera control code and use the ECS system instead.

## Complete SponzaTestApp Integration Example

### Modified SponzaTestApp.h

```cpp
// Add to includes:
#include "ECS/CameraController.h"
#include "ECS/CameraControllerBuilders.h"
#include "ECS/InputSystem.h"

// Add to private members:
std::unique_ptr<ECS::InputSystem> m_inputSystem;

// Remove these old camera members:
// float m_cameraDistance = 10.0f;
// float m_cameraAngle = 0.0f;
// float m_cameraHeight = 3.0f;
// bool m_autoRotate = true;
```

### Modified SponzaTestApp.cpp

```cpp
void SponzaTestApp::initializeECS() {
    m_sceneManager = std::make_unique<ECS::SceneManager>(*m_resourceManager, *m_device);
    m_activeScene = m_sceneManager->createScene("SponzaScene");

    // Add Input System first (priority 1)
    m_inputSystem = std::make_unique<ECS::InputSystem>(*m_eventDispatcher, *m_window);
    m_activeScene->getSystemManager().addSystem(m_inputSystem.get());

    // Add Camera Controller System (priority 5)
    m_activeScene->getSystemManager().addSystem<ECS::CameraControllerSystem>();

    m_activeScene->initialize();

    // Create camera entity with orbit controller
    auto cameraEntity = m_activeScene->createEntity("MainCamera")
        .withTransform(glm::vec3(0.0f, 3.0f, 10.0f))
        .withCamera(true)
        .withOrbitCameraController(
            glm::vec3(0.0f, 1.5f, 0.0f),  // Center of Sponza atrium
            10.0f,                         // Initial distance
            false                          // Manual control (not auto-rotate)
        )
        .build();

    m_logger->log(LogLevel::Info, "ECS initialized with camera controller");
}

void SponzaTestApp::updateUniformBuffers(uint32_t currentImage) {
    // Get camera from ECS
    auto& registry = m_activeScene->getRegistry();
    ECS::CameraSystem cameraSystem;
    auto cameraEntity = cameraSystem.getMainCamera(registry);

    if (cameraEntity == entt::null) return;

    auto* camera = registry.try_get<ECS::CameraComponent>(cameraEntity);
    auto* transform = registry.try_get<ECS::TransformComponent>(cameraEntity);

    if (!camera || !transform) return;

    CameraUBO ubo;
    ubo.view = camera->viewMatrix;
    ubo.proj = camera->projectionMatrix;
    ubo.proj[1][1] *= -1;  // Vulkan Y-flip
    ubo.cameraPos = glm::vec3(transform->worldMatrix[3]);

    m_cameraUniformBuffer->update(currentImage, ubo);
}

void SponzaTestApp::setupEventHandlers() {
    m_eventDispatcher->subscribe<WindowResizeEvent>([this](const WindowResizeEvent& event) {
        m_logger->log(LogLevel::Info, "Window resized to %dx%d", event.width, event.height);
        handleSwapChainRecreation();

        // Update camera aspect ratio
        auto& registry = m_activeScene->getRegistry();
        auto cameraView = registry.view<ECS::CameraComponent>();
        for (auto entity : cameraView) {
            auto& camera = cameraView.get<ECS::CameraComponent>(entity);
            camera.aspectRatio = static_cast<float>(event.width) / static_cast<float>(event.height);
        }
    });

    // Camera mode switching (optional)
    m_eventDispatcher->subscribe<KeyEvent>([this](const KeyEvent& event) {
        if (!event.pressed) return;

        auto& registry = m_activeScene->getRegistry();
        auto view = registry.view<ECS::CameraControllerComponent>();

        for (auto entity : view) {
            auto& ctrl = view.get<ECS::CameraControllerComponent>(entity);

            switch (event.keyCode) {
                case GLFW_KEY_1:
                    ctrl.mode = ECS::CameraControllerComponent::Mode::Free;
                    m_logger->log(LogLevel::Info, "Camera mode: Free");
                    break;
                case GLFW_KEY_2:
                    ctrl.mode = ECS::CameraControllerComponent::Mode::Orbit;
                    m_logger->log(LogLevel::Info, "Camera mode: Orbit");
                    break;
                case GLFW_KEY_3:
                    ctrl.mode = ECS::CameraControllerComponent::Mode::FirstPerson;
                    m_logger->log(LogLevel::Info, "Camera mode: First Person");
                    break;
                case GLFW_KEY_SPACE:
                    ctrl.orbitAutoRotate = !ctrl.orbitAutoRotate;
                    m_logger->log(LogLevel::Info, "Auto-rotate: %s",
                                  ctrl.orbitAutoRotate ? "ON" : "OFF");
                    break;
            }
        }
    });
}

void SponzaTestApp::update() {
    // Update ECS (this processes input and camera controller)
    static auto lastTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
    lastTime = currentTime;

    m_activeScene->update(deltaTime);
}
```

## Control Reference

### Orbit Mode (Default)
- **Right Mouse Drag**: Rotate camera around target
- **Middle Mouse Drag**: Pan target point
- **Mouse Scroll**: Zoom in/out
- **Arrow Keys**: Alternative rotation/zoom
- **Space**: Toggle auto-rotate

### Free Mode
- **WASD**: Move horizontally
- **E/Q**: Move up/down
- **Right Mouse + Move**: Look around
- **Shift**: Sprint (2.5x speed)
- **Escape**: Toggle mouse capture

### First Person Mode
- **WASD**: Move on ground plane
- **Right Mouse + Move**: Look around
- **Shift**: Sprint
- **Escape**: Toggle mouse capture

## Customization

### Changing Speeds and Sensitivities

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);
ctrl.moveSpeed = 10.0f;           // Faster movement
ctrl.mouseSensitivity = 0.003f;   // More sensitive mouse
ctrl.smoothing = 15.0f;           // Snappier response
ctrl.orbitAutoRotateSpeed = 0.5f; // Faster auto-rotation
```

### Custom Key Bindings

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);
ctrl.keyForward = GLFW_KEY_UP;    // Use arrow keys instead of WASD
ctrl.keyBackward = GLFW_KEY_DOWN;
ctrl.keyLeft = GLFW_KEY_LEFT;
ctrl.keyRight = GLFW_KEY_RIGHT;
```

### Orbit Constraints

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);
ctrl.orbitMinDistance = 2.0f;     // Can't zoom closer than 2 units
ctrl.orbitMaxDistance = 50.0f;    // Can't zoom farther than 50 units
ctrl.orbitMinPitch = -0.5f;       // Limit how low you can look
ctrl.orbitMaxPitch = 1.0f;        // Limit how high you can look
```

## Architecture Notes

The camera system follows Shoonyakasha's ECS architecture:

```
InputSystem (priority 1)
    ↓ Updates InputStateComponent
CameraControllerSystem (priority 5)
    ↓ Reads InputState, updates TransformComponent
TransformSystem (priority 0)
    ↓ Updates worldMatrix
CameraSystem (priority 10)
    ↓ Computes viewMatrix from worldMatrix
RenderSystem (priority 20)
    → Uses viewMatrix/projectionMatrix for rendering
```

---

*May your perception be as clear as Vajrayogini's wisdom eye* 🔥
