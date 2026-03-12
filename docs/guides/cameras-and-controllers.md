# Cameras and Controllers

How to create cameras, switch projection types, and use the built-in camera
controller system for interactive navigation. Python examples are shown first,
with C++ equivalents below.

> **Prerequisite:** This guide assumes you have completed the
> [Python Quickstart](../getting-started/python-quickstart.md) or
> [C++ Quickstart](../getting-started/cpp-quickstart.md) and understand how
> [entities and components](entities-and-components.md) work.

---

## Camera Types

Shoonyakasha supports two camera projection types:

| Type | Constant (Python) | Description |
|------|-------------------|-------------|
| **Perspective** | `sk.CAMERA_PERSPECTIVE` | Standard 3D projection with field of view, near plane, and far plane. Objects appear smaller with distance. |
| **Orthographic** | `sk.CAMERA_ORTHOGRAPHIC` | Flat projection with no foreshortening. Defined by `orthoSize` (half-height in world units). Useful for 2D games, UI overlays, and technical views. |

### Key Parameters

| Parameter | Applies To | Default | Description |
|-----------|-----------|---------|-------------|
| `fov` | Perspective | `45.0` | Vertical field of view in degrees |
| `nearPlane` | Both | `0.1` | Near clipping distance |
| `farPlane` | Both | `1000.0` | Far clipping distance |
| `orthoSize` | Orthographic | `10.0` | Half-height in world units |
| `aspectRatio` | Both | `16:9` | Automatically updated on window resize |

---

## Creating Cameras

### Python

The quickest way to create a camera is through the Engine helper, which creates
an entity with a `Transform`, `Camera`, and `CameraController` all wired up:

```python
import shoonyakasha as sk

engine = sk.Engine(pipeline_json_path="pipeline.json")

def on_init():
    # Create a camera at position (0, 5, 15), 60-degree FOV, speed 8
    engine.create_camera(pos=(0, 5, 15), fov=60, speed=8)

engine.set_on_init(on_init)
engine.run()
```

This creates a free-fly camera that you can control immediately with the
keyboard and mouse.

For more control, create the camera entity manually through the Scene:

```python
def on_init():
    scene = engine.scene

    # Create entity
    cam = scene.create_entity("MainCamera")

    # Configure as a camera
    scene.add_component(cam, "Camera")
    scene.set_camera_type(cam, sk.CAMERA_PERSPECTIVE)
    scene.set_camera_fov(cam, 60.0)
    scene.set_camera_near(cam, 0.1)
    scene.set_camera_far(cam, 2000.0)

    # Position it
    scene.set_position(cam, (0.0, 5.0, 15.0))

    # Mark as the active rendering camera
    scene.set_camera_main(cam, True)
```

### C++

Using the EntityBuilder:

```cpp
#include "ECS/Core.h"
#include "ECS/CameraController.h"
#include "ECS/CameraControllerBuilders.h"

// Free camera with controller
auto camera = ECS::EntityBuilder(registry)
    .withName("MainCamera")
    .withTransform(glm::vec3(0.0f, 5.0f, 15.0f))
    .withCamera(true)  // true = set as main camera
    .withFreeCameraController(8.0f)  // move speed
    .build();
```

Using the Facade API:

```cpp
#include "Facade/EngineAPI.h"

// Equivalent to engine.create_camera() in Python
engine.createCamera(glm::vec3(0, 5, 15), 60.0f, 8.0f);
```

---

## Main Camera

Only one camera can be the **main camera** at a time. The main camera is the
one whose view and projection matrices are used for rendering.

```python
scene = engine.scene

# Set a camera as main
scene.set_camera_main(cam_entity, True)

# Check if a camera is main
if scene.is_camera_main(cam_entity):
    print("This camera is active")

# Get the current main camera entity
main_cam = scene.get_main_camera()
```

When you mark a new camera as main, the previous main camera is automatically
demoted. There is always exactly one main camera if any camera exists.

### Switching Between Cameras

A common pattern is to create multiple cameras and swap between them:

```python
def on_init():
    scene = engine.scene

    # Game camera
    game_cam = engine.create_camera(pos=(0, 5, 15), fov=60, speed=5)
    scene.set_name(game_cam, "GameCamera") if hasattr(scene, 'set_name') else None

    # Debug fly camera
    debug_cam = engine.create_camera(pos=(0, 20, 0), fov=90, speed=20)

def on_update(dt):
    # Press F1 to switch to debug camera
    if engine.input.is_key_pressed(sk.KEY_F1):
        cam = scene.find_entity_by_name("DebugCamera")
        if cam != sk.NULL_ENTITY:
            scene.set_camera_main(cam, True)
```

---

## Camera Controller Modes

The engine provides four built-in camera controller modes. Each mode defines how
keyboard and mouse input translate into camera movement.

### Free (Fly Camera)

The default mode. Full 3D movement in any direction -- ideal for scene
exploration, level editing, and spectator views.

```python
# Python: engine.create_camera uses Free mode by default
engine.create_camera(pos=(0, 5, 15), speed=8)
```

```cpp
// C++
auto cam = ECS::EntityBuilder(registry)
    .withName("FreeCamera")
    .withTransform({0, 5, 15})
    .withCamera(true)
    .withFreeCameraController(8.0f)
    .build();
```

### Orbit (Turntable Camera)

Rotates around a target point. Ideal for model viewers, object inspection, and
showcase scenes.

```cpp
auto cam = ECS::EntityBuilder(registry)
    .withName("OrbitCamera")
    .withTransform({0, 3, 10})
    .withCamera(true)
    .withOrbitCameraController(
        glm::vec3(0.0f, 1.5f, 0.0f),  // target point
        10.0f,                         // initial distance
        false                          // auto-rotate off
    )
    .build();
```

### First Person (Grounded FPS)

WASD movement locked to the ground plane. The camera stays at a fixed eye
height -- no flying. Good for FPS games and architectural walkthroughs.

```cpp
auto cam = ECS::EntityBuilder(registry)
    .withName("FPSCamera")
    .withTransform({0, 0, 0})
    .withCamera(true)
    .withFirstPersonController(1.7f)  // eye height in meters
    .build();
```

### Third Person (Follow Camera)

Automatically follows a target entity from behind at a configurable offset.
Good for action games and character-centric views.

```cpp
auto cam = ECS::EntityBuilder(registry)
    .withName("FollowCamera")
    .withTransform({0, 0, 0})
    .withCamera(true)
    .withThirdPersonController(
        playerEntity,                           // entity to follow
        glm::vec3(0.0f, 2.0f, 5.0f)           // offset behind and above
    )
    .build();
```

---

## Controls Reference

### Free Mode

| Action | Input |
|--------|-------|
| Move forward / back | W / S |
| Strafe left / right | A / D |
| Move up / down | E / Q |
| Look around | Hold Right Mouse Button + move mouse |
| Sprint (2.5x speed) | Hold Left Shift |
| Toggle mouse capture | Escape |
| Zoom | Mouse scroll wheel |

### Orbit Mode

| Action | Input |
|--------|-------|
| Rotate around target | Hold Right Mouse Button + drag |
| Pan target point | Hold Middle Mouse Button + drag |
| Zoom in / out | Mouse scroll wheel |
| Alternative rotation / zoom | Arrow keys |
| Toggle auto-rotate | Space |

### First Person Mode

| Action | Input |
|--------|-------|
| Walk forward / back | W / S |
| Strafe left / right | A / D |
| Look around | Hold Right Mouse Button + move mouse |
| Sprint | Hold Left Shift |
| Toggle mouse capture | Escape |

### Third Person Mode

| Action | Input |
|--------|-------|
| Camera follows target | Automatic |
| Rotate view | Hold Right Mouse Button + drag |
| Zoom in / out | Mouse scroll wheel |

---

## Customization

Camera controller behavior is configured through fields on the
`CameraControllerComponent`. In C++, access the component directly. In Python,
these are set at creation time through `engine.create_camera()` parameters.

### Movement and Sensitivity

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

ctrl.moveSpeed         = 10.0f;     // units per second (default: 5.0)
ctrl.sprintMultiplier  = 3.0f;      // sprint speed factor (default: 2.5)
ctrl.mouseSensitivity  = 0.005f;    // radians per pixel (default: 0.003)
ctrl.scrollSensitivity = 3.0f;      // units per scroll tick (default: 2.0)
ctrl.smoothing         = 15.0f;     // interpolation factor (default: 10.0)
                                    // higher = snappier, lower = smoother
```

### Orbit Constraints

Limit how far the camera can zoom and how high/low it can pitch:

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

ctrl.orbitMinDistance     = 2.0f;    // closest zoom (default: 1.0)
ctrl.orbitMaxDistance     = 50.0f;   // farthest zoom (default: 100.0)
ctrl.orbitMinPitch       = -0.5f;   // min vertical angle in radians (default: -1.5)
ctrl.orbitMaxPitch       = 1.0f;    // max vertical angle in radians (default: 1.5)
ctrl.orbitAutoRotateSpeed = 0.5f;   // radians per second when auto-rotating
```

### First Person Settings

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

ctrl.eyeHeight     = 1.7f;    // camera height from ground (meters)
ctrl.groundY       = 0.0f;    // Y coordinate of the ground plane
ctrl.constrainPitch = true;   // prevent looking straight up/down
ctrl.maxPitch      = 1.5f;    // max vertical look angle (radians)
```

### Third Person Settings

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

ctrl.followTarget = playerEntity;
ctrl.followOffset = glm::vec3(0.0f, 3.0f, 8.0f);  // behind and above
ctrl.followLag    = 5.0f;   // how quickly camera catches up (higher = snappier)
```

### Custom Key Bindings

Override default WASD controls:

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

ctrl.keyForward      = GLFW_KEY_UP;
ctrl.keyBackward     = GLFW_KEY_DOWN;
ctrl.keyLeft         = GLFW_KEY_LEFT;
ctrl.keyRight        = GLFW_KEY_RIGHT;
ctrl.keyUp           = GLFW_KEY_SPACE;
ctrl.keyDown         = GLFW_KEY_LEFT_CONTROL;
ctrl.keySprint       = GLFW_KEY_LEFT_SHIFT;
ctrl.mouseButtonLook = GLFW_MOUSE_BUTTON_RIGHT;
ctrl.mouseButtonPan  = GLFW_MOUSE_BUTTON_MIDDLE;
```

---

## Switching Modes at Runtime

### C++

In C++, change the `mode` field on the `CameraControllerComponent` directly.
The controller system picks up the change on the next frame.

```cpp
auto& ctrl = registry.get<ECS::CameraControllerComponent>(cameraEntity);

// Switch to orbit mode
ctrl.mode = ECS::CameraControllerComponent::Mode::Orbit;
ctrl.orbitTarget = glm::vec3(0.0f, 1.5f, 0.0f);
ctrl.orbitDistance = 10.0f;
```

A common pattern is to bind mode switching to number keys:

```cpp
m_eventDispatcher->subscribe<KeyEvent>([this](const KeyEvent& event) {
    if (!event.pressed) return;

    auto& registry = m_activeScene->getRegistry();
    auto view = registry.view<ECS::CameraControllerComponent>();

    for (auto entity : view) {
        auto& ctrl = view.get<ECS::CameraControllerComponent>(entity);

        switch (event.keyCode) {
            case GLFW_KEY_1:
                ctrl.mode = ECS::CameraControllerComponent::Mode::Free;
                break;
            case GLFW_KEY_2:
                ctrl.mode = ECS::CameraControllerComponent::Mode::Orbit;
                break;
            case GLFW_KEY_3:
                ctrl.mode = ECS::CameraControllerComponent::Mode::FirstPerson;
                break;
        }
    }
});
```

### Python

In Python, switching modes at runtime requires creating a new camera entity with
the desired mode, since the controller component fields are not individually
exposed. Destroy the old camera and create a new one:

```python
def switch_to_orbit(scene, target_pos):
    old_cam = scene.get_main_camera()
    pos = scene.get_world_position(old_cam)
    scene.destroy_entity(old_cam)

    # create_camera creates a free camera; for orbit you would need
    # to set up the entity manually or use a C++ helper
    new_cam = engine.create_camera(pos=pos, fov=60, speed=5)
    scene.set_camera_main(new_cam, True)
```

---

## System Architecture

The camera system integrates with the ECS update loop through a chain of
systems that run in priority order each frame:

```
InputSystem (priority 1)
    | Updates InputStateComponent with keyboard/mouse state
    v
CameraControllerSystem (priority 5)
    | Reads InputState, computes new position/rotation
    | Writes to TransformComponent
    v
TransformSystem (priority 0)
    | Recomputes localMatrix and worldMatrix
    v
CameraSystem (priority 10)
    | Computes viewMatrix from worldMatrix
    | Computes projectionMatrix from camera parameters
    v
RenderSystem (priority 20)
    | Uses viewMatrix and projectionMatrix for rendering
```

The `CameraSystem` automatically handles the Vulkan Y-flip when computing the
projection matrix.

### Setting Up the System (C++)

If you are building a C++ application with `ApplicationBase`, you need to
register the input and camera controller systems in your initialization:

```cpp
void MyApp::initializeECS() {
    // Create scene
    m_activeScene = m_sceneManager->createScene("MainScene");

    // Add Input System (reads keyboard/mouse events)
    m_inputSystem = std::make_unique<ECS::InputSystem>(
        *m_eventDispatcher, *m_window);
    m_activeScene->getSystemManager().addSystem(m_inputSystem.get());

    // Add Camera Controller System (processes input into camera movement)
    m_activeScene->getSystemManager().addSystem<ECS::CameraControllerSystem>();

    m_activeScene->initialize();

    // Create camera entity
    auto cam = ECS::EntityBuilder(m_activeScene->getRegistry())
        .withName("MainCamera")
        .withTransform({0, 3, 10})
        .withCamera(true)
        .withOrbitCameraController({0, 1.5f, 0}, 10.0f, false)
        .build();
}
```

When using the Python `Engine` class, these systems are set up automatically --
you do not need to register them.

---

## Orthographic Cameras

For 2D games, UI overlays, or technical top-down views, use an orthographic
camera:

```python
scene = engine.scene

cam = scene.create_entity("OrthoCamera")
scene.add_component(cam, "Camera")
scene.set_camera_type(cam, sk.CAMERA_ORTHOGRAPHIC)
scene.set_camera_ortho_size(cam, 10.0)   # half-height in world units
scene.set_camera_near(cam, -100.0)
scene.set_camera_far(cam, 100.0)
scene.set_position(cam, (0, 50, 0))
scene.set_rotation(cam, (-1.5708, 0, 0))  # look straight down (-pi/2)
scene.set_camera_main(cam, True)
```

```cpp
auto cam = ECS::EntityBuilder(registry)
    .withName("OrthoCamera")
    .withTransform({0, 50, 0}, {-glm::half_pi<float>(), 0, 0})
    .withCamera(true)
    .build();

auto& camera = registry.get<ECS::CameraComponent>(cam);
camera.type = ECS::CameraComponent::Orthographic;
camera.orthoSize = 10.0f;
camera.nearPlane = -100.0f;
camera.farPlane  = 100.0f;
```

---

## Reading Camera Matrices

The `CameraSystem` computes the view and projection matrices each frame. In C++
you can read them directly from the `CameraComponent`:

```cpp
auto& camera = registry.get<ECS::CameraComponent>(cameraEntity);

glm::mat4 view = camera.viewMatrix;
glm::mat4 proj = camera.projectionMatrix;

// Vulkan Y-flip (if not already handled by the system)
proj[1][1] *= -1;

// Camera world position from the transform
auto& transform = registry.get<ECS::TransformComponent>(cameraEntity);
glm::vec3 cameraPos = glm::vec3(transform.worldMatrix[3]);
```

In Python, use the Scene transform methods to read the camera's position and
orientation:

```python
cam = scene.get_main_camera()
pos = scene.get_world_position(cam)
fwd = scene.get_forward(cam)
```

---

## See Also

- [Python Scene API -- Camera section](../api/python/scene.md#camera) --
  Full method reference for camera getters/setters
- [CameraControllerComponent](../api/cpp/ecs-components.md#cameracontrollercomponent) --
  Complete struct definition with all fields
- [CameraComponent](../api/cpp/ecs-components.md#cameracomponent) --
  Projection parameters and computed matrices
- [Constants](../api/python/constants.md) -- `CAMERA_PERSPECTIVE`,
  `CAMERA_ORTHOGRAPHIC`, and other module-level constants
- [Entities and Components](entities-and-components.md) -- ECS fundamentals
