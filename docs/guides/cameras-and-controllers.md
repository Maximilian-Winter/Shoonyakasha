# Cameras and controllers

Create the default perspective camera during init:

```python
camera = engine.create_camera((0.0, 2.0, 8.0), fov=60.0, speed=8.0,
                              near_plane=0.1, far_plane=1000.0)
```

```cpp
auto camera = engine.createCamera(glm::vec3(0.f, 2.f, 8.f), 60.f, 8.f, 0.1f, 1000.f);
```

The helper creates a camera with a controller. FOV is in degrees; transform rotations are Euler radians. Position and clipping ranges must suit the model's units; the Fox example uses a much larger scene scale than the starter box.

## Controls and API boundaries

Default bindings are WASD movement, E/Q up/down, left Shift sprint, and Escape capture toggle. Right mouse controls looking; orbit mode also uses middle mouse for panning. Examples may install additional controls, listed in their startup output.

Scene APIs expose camera type, FOV, near/far planes, orthographic size, main-camera selection, and ordinary transforms. Python can change these fields, but has no full camera-controller configuration wrapper.

Native `CameraControllerComponent` and [builder helpers](../../include/ECS/CameraControllerBuilders.h) support free, orbit, first-person, and third-person configurations. Use `ApplicationBase`/registry access for those settings. Do not invent Python methods such as an orbit-controller constructor by translating C++ names.

Camera matrices are made available to layouts under `scene.camera.*`; screen-space sprites instead use viewport information from their sprite pipeline.

References: [Python Scene](../api/python/scene.md), [C++ SceneAPI](../api/cpp/scene-api.md), [input](../api/python/input.md), [controller source](../../include/ECS/CameraController.h).
