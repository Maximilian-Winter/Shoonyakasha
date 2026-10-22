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

## More cameras

Every camera entity with a name is also available to the pipeline as `scene.cameras.<name>.*`, with the same properties as `scene.camera`. A second camera, without a controller:

```python
from shoonyakasha.mathutil import look_rotation

overhead = engine.scene.create_entity("Overhead")
engine.scene.add_component(overhead, "Camera")
engine.scene.set_position(overhead, (0.0, 14.0, 0.01))
engine.scene.set_rotation(overhead, look_rotation((0.0, 14.0, 0.01), (0.0, 0.0, 0.0)))
engine.scene.set_camera_aspect(overhead, 1.0)   # the shape it renders into
```

Cameras project at the window's shape. `set_camera_aspect(entity, width / height)` sets another, for a camera that renders into an image or a viewport of a different shape; 0 follows the window again. The main camera's aspect can be set the same way.

The pipeline draws the view with passes of its own: a buffer layout whose fields read `scene.cameras.Overhead.view`, `.projection` and so on, bound in place of the main camera's, and geometry passes with `"view": "cameras.Overhead"`, which cull against that camera and sort by distance from it. They write an image a later pass samples, or the screen within a [`viewport`](../reference/pipeline-json.md#passes-and-pipeline-state) such as the right half for a split screen. While no camera has the name, its paths do not resolve, so fields keep their `default`, and its passes draw nothing. If several cameras share a name, one of them is used.

References: [Python Scene](../api/python/scene.md), [C++ SceneAPI](../api/cpp/scene-api.md), [input](../api/python/input.md), [controller source](../../include/ECS/CameraController.h).
