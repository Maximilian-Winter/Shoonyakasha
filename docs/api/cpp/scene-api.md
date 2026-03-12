# SceneAPI

`Shoonyakasha::Facade::SceneAPI` -- entity lifecycle, component management, transform/camera/light/material access, hierarchy, animation, and serialization.

> **Facade layer** -- this is the same API that Python wraps via Cython. All Vulkan/EnTT internals are hidden behind PIMPL. For the inheritance-based alternative, see [ApplicationBase](application-base.md).

**Header:** `#include "Facade/SceneAPI.h"`
**Namespace:** `Shoonyakasha::Facade`

**See also:** [Python Scene API](../python/scene.md)

---

## Obtaining a SceneAPI

You do not construct `SceneAPI` directly. Access it through `EngineAPI::getScene()`:

```cpp
auto& scene = engine.getScene();
EntityHandle e = scene.createEntity("Player");
```

The `SceneAPI` wraps an internal `ECS::Scene` and is valid for the lifetime of the `EngineAPI`.

---

## Entity Lifecycle

### createEntity

```cpp
EntityHandle createEntity(const std::string& name = "");
```

Create a new entity. Automatically adds `Transform` and `Active` components.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `const std::string&` | `""` | Optional entity name. Empty = unnamed. |

**Returns:** `EntityHandle` -- the newly created entity.

### destroyEntity

```cpp
void destroyEntity(EntityHandle entity);
```

Destroy an entity and remove it from any parent hierarchy.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | The entity to destroy. |

### isValid

```cpp
bool isValid(EntityHandle entity) const;
```

Check whether an entity handle refers to a live entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | The entity to check. |

**Returns:** `bool` -- `true` if the entity exists and is valid.

### getEntityCount

```cpp
size_t getEntityCount() const;
```

Get the total number of entities in the scene.

**Returns:** `size_t` -- entity count.

---

## Entity Queries

### findEntityByName

```cpp
EntityHandle findEntityByName(const std::string& name) const;
```

Find the first entity with the given name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `const std::string&` | The name to search for. |

**Returns:** `EntityHandle` -- the matching entity, or `NullEntity` if not found.

### findEntitiesWithTag

```cpp
std::vector<EntityHandle> findEntitiesWithTag(const std::string& tag) const;
```

Find all entities with a given tag string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `tag` | `const std::string&` | The tag to search for. |

**Returns:** `std::vector<EntityHandle>` -- matching entities (may be empty).

### getMainCamera

```cpp
EntityHandle getMainCamera();
```

Get the entity marked as the main camera.

**Returns:** `EntityHandle` -- the main camera entity, or `NullEntity` if none exists.

### getAllEntities

```cpp
std::vector<EntityHandle> getAllEntities() const;
```

Get handles for all entities in the scene. Intended for iteration.

**Returns:** `std::vector<EntityHandle>` -- all entity handles.

---

## Component Management

String-based component access. Component names match the registered type names (e.g. `"Transform"`, `"Light"`, `"Camera"`, `"RigidBody"`).

### addComponent

```cpp
bool addComponent(EntityHandle entity, const std::string& componentName);
```

Add a component to an entity by registered name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `componentName` | `const std::string&` | Registered component type name. |

**Returns:** `bool` -- `true` if the component was added successfully, `false` if the entity already has it or the name is unknown.

### removeComponent

```cpp
bool removeComponent(EntityHandle entity, const std::string& componentName);
```

Remove a component from an entity by name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `componentName` | `const std::string&` | Registered component type name. |

**Returns:** `bool` -- `true` if the component was removed.

### hasComponent

```cpp
bool hasComponent(EntityHandle entity, const std::string& componentName) const;
```

Check if an entity has a component by name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `componentName` | `const std::string&` | Registered component type name. |

**Returns:** `bool` -- `true` if the component is present.

### getComponentNames

```cpp
std::vector<std::string> getComponentNames() const;
```

List all registered component type names.

**Returns:** `std::vector<std::string>` -- registered names (e.g. `{"Transform", "Camera", "Light", ...}`).

---

## Name / Tag / Active

### getName

```cpp
std::string getName(EntityHandle entity) const;
```

Get the entity's name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `std::string` -- the entity name (empty if unnamed).

### setName

```cpp
void setName(EntityHandle entity, const std::string& name);
```

Set the entity's name.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `name` | `const std::string&` | New name. |

### getTag

```cpp
std::string getTag(EntityHandle entity) const;
```

Get the entity's tag string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `std::string` -- the tag (empty if no tag).

### setTag

```cpp
void setTag(EntityHandle entity, const std::string& tag);
```

Set the entity's tag string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `tag` | `const std::string&` | Tag string. |

### isActive

```cpp
bool isActive(EntityHandle entity) const;
```

Check whether the entity is active (will be processed by systems).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `bool` -- `true` if active.

### setActive

```cpp
void setActive(EntityHandle entity, bool active);
```

Enable or disable the entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `active` | `bool` | `true` to activate, `false` to deactivate. |

---

## Transform Access

All transform methods operate on the entity's `Transform` component. Entities created via `createEntity()` automatically have one.

### getPosition

```cpp
glm::vec3 getPosition(EntityHandle entity) const;
```

Get the entity's local position.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- local position.

### setPosition

```cpp
void setPosition(EntityHandle entity, const glm::vec3& pos);
```

Set the entity's local position.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `pos` | `const glm::vec3&` | New local position. |

### getRotation

```cpp
glm::vec3 getRotation(EntityHandle entity) const;
```

Get the entity's local rotation as Euler angles in radians.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- Euler angles (pitch, yaw, roll) in radians.

### setRotation

```cpp
void setRotation(EntityHandle entity, const glm::vec3& eulerRadians);
```

Set the entity's local rotation from Euler angles.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `eulerRadians` | `const glm::vec3&` | Euler angles (pitch, yaw, roll) in radians. |

### getScale

```cpp
glm::vec3 getScale(EntityHandle entity) const;
```

Get the entity's local scale.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- scale on each axis.

### setScale

```cpp
void setScale(EntityHandle entity, const glm::vec3& scale);
```

Set the entity's local scale.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `scale` | `const glm::vec3&` | Scale on each axis. |

### getWorldPosition

```cpp
glm::vec3 getWorldPosition(EntityHandle entity) const;
```

Get the entity's world-space position (accounting for parent hierarchy).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- world-space position.

### getWorldMatrix

```cpp
glm::mat4 getWorldMatrix(EntityHandle entity) const;
```

Get the full world-space transform matrix.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::mat4` -- 4x4 world transform matrix.

### getForward

```cpp
glm::vec3 getForward(EntityHandle entity) const;
```

Get the entity's forward direction vector (derived from rotation).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- normalized forward vector.

### getRight

```cpp
glm::vec3 getRight(EntityHandle entity) const;
```

Get the entity's right direction vector.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- normalized right vector.

### getUp

```cpp
glm::vec3 getUp(EntityHandle entity) const;
```

Get the entity's up direction vector.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `glm::vec3` -- normalized up vector.

---

## Camera Access

Methods for reading and writing camera component properties. The entity must have a `Camera` component.

### getCameraType

```cpp
CameraType getCameraType(EntityHandle entity) const;
```

Get the camera projection type.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `CameraType` -- `Perspective` or `Orthographic`.

### setCameraType

```cpp
void setCameraType(EntityHandle entity, CameraType type);
```

Set the camera projection type.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `type` | `CameraType` | `CameraType::Perspective` or `CameraType::Orthographic`. |

### getCameraFov

```cpp
float getCameraFov(EntityHandle entity) const;
```

Get the vertical field of view (perspective cameras).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `float` -- FOV in degrees.

### setCameraFov

```cpp
void setCameraFov(EntityHandle entity, float fov);
```

Set the vertical field of view.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `fov` | `float` | FOV in degrees. |

### getCameraNear

```cpp
float getCameraNear(EntityHandle entity) const;
```

Get the near clipping plane distance.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `float` -- near plane distance.

### setCameraNear

```cpp
void setCameraNear(EntityHandle entity, float nearPlane);
```

Set the near clipping plane distance.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `nearPlane` | `float` | Near plane distance. |

### getCameraFar

```cpp
float getCameraFar(EntityHandle entity) const;
```

Get the far clipping plane distance.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `float` -- far plane distance.

### setCameraFar

```cpp
void setCameraFar(EntityHandle entity, float farPlane);
```

Set the far clipping plane distance.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `farPlane` | `float` | Far plane distance. |

### getCameraOrthoSize

```cpp
float getCameraOrthoSize(EntityHandle entity) const;
```

Get the orthographic camera half-height.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `float` -- orthographic size.

### setCameraOrthoSize

```cpp
void setCameraOrthoSize(EntityHandle entity, float size);
```

Set the orthographic camera half-height.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `size` | `float` | Orthographic half-height in world units. |

### isCameraMain

```cpp
bool isCameraMain(EntityHandle entity) const;
```

Check if this camera is the main (active) camera.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |

**Returns:** `bool` -- `true` if this is the main camera.

### setCameraMain

```cpp
void setCameraMain(EntityHandle entity, bool isMain);
```

Set whether this camera is the main camera. Setting `true` on one camera will typically deactivate the previous main camera.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Camera entity. |
| `isMain` | `bool` | `true` to make this the main camera. |

---

## Light Access

Methods for reading and writing light component properties. The entity must have a `Light` component.

### getLightType

```cpp
LightType getLightType(EntityHandle entity) const;
```

Get the light type.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |

**Returns:** `LightType` -- `Directional`, `Point`, or `Spot`.

### setLightType

```cpp
void setLightType(EntityHandle entity, LightType type);
```

Set the light type.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |
| `type` | `LightType` | The light type. |

### getLightColor

```cpp
glm::vec3 getLightColor(EntityHandle entity) const;
```

Get the light's RGB color (linear space).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |

**Returns:** `glm::vec3` -- RGB color.

### setLightColor

```cpp
void setLightColor(EntityHandle entity, const glm::vec3& color);
```

Set the light's RGB color.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |
| `color` | `const glm::vec3&` | RGB color (linear space). |

### getLightIntensity

```cpp
float getLightIntensity(EntityHandle entity) const;
```

Get the light intensity multiplier.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |

**Returns:** `float` -- intensity.

### setLightIntensity

```cpp
void setLightIntensity(EntityHandle entity, float intensity);
```

Set the light intensity multiplier.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |
| `intensity` | `float` | Intensity value. |

### getLightRange

```cpp
float getLightRange(EntityHandle entity) const;
```

Get the light's attenuation range (point/spot lights).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |

**Returns:** `float` -- range in world units.

### setLightRange

```cpp
void setLightRange(EntityHandle entity, float range);
```

Set the light's attenuation range.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |
| `range` | `float` | Range in world units. |

### getLightCastShadows

```cpp
bool getLightCastShadows(EntityHandle entity) const;
```

Check if the light casts shadows.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |

**Returns:** `bool` -- `true` if shadow casting is enabled.

### setLightCastShadows

```cpp
void setLightCastShadows(EntityHandle entity, bool castShadows);
```

Enable or disable shadow casting for the light.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Light entity. |
| `castShadows` | `bool` | `true` to enable shadows. |

---

## Material Access

Typed getter/setter methods for material parameters. The entity must have a renderable with an associated material. Parameter names are material-specific strings (e.g. `"roughness"`, `"metallic"`, `"baseColor"`).

### setMaterialFloat

```cpp
void setMaterialFloat(EntityHandle entity, const std::string& param, float value);
```

Set a float material parameter.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `param` | `const std::string&` | Material parameter name. |
| `value` | `float` | Value to set. |

### getMaterialFloat

```cpp
float getMaterialFloat(EntityHandle entity, const std::string& param,
                       float defaultVal = 0.0f) const;
```

Get a float material parameter.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `EntityHandle` | -- | Target entity. |
| `param` | `const std::string&` | -- | Material parameter name. |
| `defaultVal` | `float` | `0.0f` | Returned if the parameter does not exist. |

**Returns:** `float` -- the parameter value, or `defaultVal`.

### setMaterialVec3

```cpp
void setMaterialVec3(EntityHandle entity, const std::string& param,
                     const glm::vec3& value);
```

Set a `vec3` material parameter (e.g. base color, emissive).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `param` | `const std::string&` | Material parameter name. |
| `value` | `const glm::vec3&` | Value to set. |

### getMaterialVec3

```cpp
glm::vec3 getMaterialVec3(EntityHandle entity, const std::string& param,
                          const glm::vec3& defaultVal = glm::vec3(0.0f)) const;
```

Get a `vec3` material parameter.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `EntityHandle` | -- | Target entity. |
| `param` | `const std::string&` | -- | Material parameter name. |
| `defaultVal` | `const glm::vec3&` | `glm::vec3(0.0f)` | Returned if the parameter does not exist. |

**Returns:** `glm::vec3` -- the parameter value, or `defaultVal`.

### setMaterialVec4

```cpp
void setMaterialVec4(EntityHandle entity, const std::string& param,
                     const glm::vec4& value);
```

Set a `vec4` material parameter.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `param` | `const std::string&` | Material parameter name. |
| `value` | `const glm::vec4&` | Value to set. |

### getMaterialVec4

```cpp
glm::vec4 getMaterialVec4(EntityHandle entity, const std::string& param,
                          const glm::vec4& defaultVal = glm::vec4(0.0f)) const;
```

Get a `vec4` material parameter.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `entity` | `EntityHandle` | -- | Target entity. |
| `param` | `const std::string&` | -- | Material parameter name. |
| `defaultVal` | `const glm::vec4&` | `glm::vec4(0.0f)` | Returned if the parameter does not exist. |

**Returns:** `glm::vec4` -- the parameter value, or `defaultVal`.

### hasMaterialParam

```cpp
bool hasMaterialParam(EntityHandle entity, const std::string& param) const;
```

Check if a material parameter exists on the entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `param` | `const std::string&` | Material parameter name. |

**Returns:** `bool` -- `true` if the parameter is present.

---

## Renderable Tag Access

### isVisible

```cpp
bool isVisible(EntityHandle entity) const;
```

Check if the entity is visible (will be rendered).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `bool` -- `true` if visible.

### setVisible

```cpp
void setVisible(EntityHandle entity, bool visible);
```

Show or hide the entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `visible` | `bool` | `true` to show, `false` to hide. |

### getCastShadows

```cpp
bool getCastShadows(EntityHandle entity) const;
```

Check if the entity casts shadows.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `bool` -- `true` if shadow casting is enabled.

### setCastShadows

```cpp
void setCastShadows(EntityHandle entity, bool castShadows);
```

Enable or disable shadow casting for the entity's renderable.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |
| `castShadows` | `bool` | `true` to cast shadows. |

---

## Hierarchy Access

### getParent

```cpp
EntityHandle getParent(EntityHandle entity) const;
```

Get the entity's parent in the transform hierarchy.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Target entity. |

**Returns:** `EntityHandle` -- parent entity, or `NullEntity` if no parent.

### setParent

```cpp
void setParent(EntityHandle child, EntityHandle parent);
```

Set the entity's parent. Pass `NullEntity` to detach from the hierarchy.

| Parameter | Type | Description |
|-----------|------|-------------|
| `child` | `EntityHandle` | The entity to reparent. |
| `parent` | `EntityHandle` | The new parent, or `NullEntity` to detach. |

### getChildren

```cpp
std::vector<EntityHandle> getChildren(EntityHandle entity) const;
```

Get all direct children of the entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Parent entity. |

**Returns:** `std::vector<EntityHandle>` -- child entity handles (may be empty).

---

## Animation Access

Methods for controlling skeletal animation playback. The entity must have animation data (typically loaded from a glTF file with `loadSkins` and `loadAnimations` enabled).

### getAnimationClipCount

```cpp
int getAnimationClipCount(EntityHandle entity) const;
```

Get the number of animation clips on an entity.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `int` -- clip count (0 if the entity has no animations).

### getAnimationClipName

```cpp
std::string getAnimationClipName(EntityHandle entity, int clipIndex) const;
```

Get the name of an animation clip by index.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `clipIndex` | `int` | Zero-based clip index. |

**Returns:** `std::string` -- clip name.

### getAnimationClipDuration

```cpp
float getAnimationClipDuration(EntityHandle entity, int clipIndex) const;
```

Get the duration of an animation clip in seconds.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `clipIndex` | `int` | Zero-based clip index. |

**Returns:** `float` -- duration in seconds.

### playAnimation

```cpp
void playAnimation(EntityHandle entity, int clipIndex);
```

Play an animation clip by index. Resets time to 0 and starts playback.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `clipIndex` | `int` | Zero-based clip index. |

### stopAnimation

```cpp
void stopAnimation(EntityHandle entity);
```

Stop animation playback. Pauses and resets time to 0.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

### isAnimationPlaying

```cpp
bool isAnimationPlaying(EntityHandle entity) const;
```

Check if an animation is currently playing.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `bool` -- `true` if playing.

### getAnimationSpeed

```cpp
float getAnimationSpeed(EntityHandle entity) const;
```

Get the animation playback speed multiplier.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `float` -- speed multiplier (default `1.0`).

### setAnimationSpeed

```cpp
void setAnimationSpeed(EntityHandle entity, float speed);
```

Set the animation playback speed multiplier.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `speed` | `float` | Speed multiplier. `1.0` = normal, `2.0` = double speed, `0.5` = half speed. |

### getAnimationTime

```cpp
float getAnimationTime(EntityHandle entity) const;
```

Get the current animation playback time.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `float` -- current time in seconds.

### setAnimationTime

```cpp
void setAnimationTime(EntityHandle entity, float time);
```

Set the current animation playback time (seek).

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `time` | `float` | Time in seconds. |

### isAnimationLooping

```cpp
bool isAnimationLooping(EntityHandle entity) const;
```

Check if animation is set to loop.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `bool` -- `true` if looping.

### setAnimationLooping

```cpp
void setAnimationLooping(EntityHandle entity, bool loop);
```

Enable or disable animation looping.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |
| `loop` | `bool` | `true` for looping playback. |

### getCurrentAnimationClip

```cpp
int getCurrentAnimationClip(EntityHandle entity) const;
```

Get the index of the currently active animation clip.

| Parameter | Type | Description |
|-----------|------|-------------|
| `entity` | `EntityHandle` | Animated entity. |

**Returns:** `int` -- zero-based clip index, or `-1` if no clip is active.

---

## Serialization

### saveToFile

```cpp
bool saveToFile(const std::string& path) const;
```

Save the scene to a JSON file.

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `const std::string&` | Output file path. |

**Returns:** `bool` -- `true` on success.

### loadFromFile

```cpp
bool loadFromFile(const std::string& path);
```

Load entities and components from a JSON file, replacing the current scene contents.

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `const std::string&` | Input file path. |

**Returns:** `bool` -- `true` on success.
