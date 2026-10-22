# Split screen

Two cameras on one screen: the player's view, flown with the default camera controls, and a map from above that follows the player. It runs on its own [pipeline](pipeline/pipeline.json), the starter's forward shading with a second view.

```
python split_screen.py
```

| Key | |
|---|---|
| WASD/Q/E + right mouse | Fly the camera |
| **M** | Side by side, or the map in a corner |
| **P** | Screenshot into `split_screen_captures/` |

## How it works

- **The map camera** is an entity named `Map` with a camera component and no controller: orthographic, looking straight down, moved above the player every frame. `create_camera` makes the player's camera, the main one.
- **The pipeline** reads the main camera into `CameraUBO` and the map camera into `MapCameraUBO`, whose fields read `scene.cameras.Map.view`, `.projection` and so on. The two layouts are the same, so one pair of shaders serves both passes; each pass binds its own camera's set as set 0.
- **Two passes** draw the scene. `PlayerView` draws with the main camera. `MapView` binds the map camera's set and culls with `"view": "cameras.Map"`, so it draws what the map camera sees and sorts by distance from it.
- **Viewports:** each pass draws into the rectangle its `viewport` names, `scene.custom.split.player` and `scene.custom.split.map`, which the script sets every frame from the window's size. `scene.set_camera_aspect` gives each camera the shape of its rectangle.
- **The marker:** the arrow on the map is on render layer 2. `PlayerView` has `"renderLayerMask": 1`, so the player does not see it.

See [more cameras](../../../../docs/guides/cameras-and-controllers.md#more-cameras).
