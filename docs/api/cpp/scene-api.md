# SceneAPI

Access: `engine.getScene()`. [Other language reference](../python/scene.md).

Include `Facade/SceneAPI.h`; namespace `Shoonyakasha::Facade`. [Source declaration](../../../include/Facade/SceneAPI.h).

Provides entity lifecycle and typed access to built-in components. Obtain it from a running engine in init or later. Entity handles are unsigned 32-bit values, represented as Python integers; `NullEntity` / `NULL_ENTITY` is `4294967295`. Check validity after destruction or scene replacement; handles are not persistent IDs.

## Entity and component contract

Creating an entity adds Transform and Active. Name/tag lookup, parent/child access, local transforms, world transforms, camera/light properties, material parameters, visibility, sorting, and animation are exposed below. Python vector getters return tuples; world matrices are column-major tuples of four tuples. C++ uses GLM values.

Positions and scales are local to the parent; transform rotation setters use Euler radians. Camera FOV setters use degrees. World transforms are refreshed by the transform system: a local setter does not imply all cached world values have updated immediately.

Name-based component access supports only the registered native names returned by `getComponentNames()` / `get_component_names()`. It does not expose arbitrary component fields. User-defined Python payloads belong to [Ecs](../../guides/script-ecs.md).

Missing components are handled with method-specific defaults/no-ops in the facade; do not use those fallback values to infer component presence. Check entity validity and component presence first. Material getters accept explicit defaults. Texture setters return false when loading or wiring fails.

## Rendering and animation

Entity layer masks are eight bits (0–255), default 255. A pass renders an entity if its mask intersects the pass mask. Sort keys order lower values first for `sortMode: sort_key`.

Text labels generate separate glyph entities. Use text-specific setters for layer, sort order, and visibility. Hide a label before destroying it: destroying the label alone can leave glyphs visible. See [sprites/UI/text](../../guides/sprites-ui-text.md).

Clip indices are zero-based; duration/time are seconds. Play resets time; stop resets to zero. Clip queries return zero/empty values on nonanimated entities and current clip is -1 when none is selected. Use a skinned pipeline to render animation.

## Serialization

Save/load delegate to the partial ECS snapshot format. Load clears existing entities and remaps saved IDs; it does not merge with loaded glTF entities. Save does not check stream failure, so true is not proof of a successful disk write. See [serialization limitations](../../guides/scene-serialization.md) before using this for persistence.

<!-- BEGIN SOURCE API -->

## Members

Declarations below are extracted from the public facade header; test constructors and internal wiring are omitted.

```cpp
explicit SceneAPI(ECS::Scene& scene);
~SceneAPI();
EntityHandle createEntity(const std::string& name = "");
void destroyEntity(EntityHandle entity);
bool isValid(EntityHandle entity) const;
size_t getEntityCount() const;
EntityHandle findEntityByName(const std::string& name) const;
std::vector<EntityHandle> findEntitiesWithTag(const std::string& tag) const;
EntityHandle getMainCamera();
std::vector<EntityHandle> getAllEntities() const;
bool addComponent(EntityHandle entity, const std::string& componentName);
bool removeComponent(EntityHandle entity, const std::string& componentName);
bool hasComponent(EntityHandle entity, const std::string& componentName) const;
std::vector<std::string> getComponentNames() const;
std::string getName(EntityHandle entity) const;
void setName(EntityHandle entity, const std::string& name);
std::string getTag(EntityHandle entity) const;
void setTag(EntityHandle entity, const std::string& tag);
bool isActive(EntityHandle entity) const;
void setActive(EntityHandle entity, bool active);
glm::vec3 getPosition(EntityHandle entity) const;
void setPosition(EntityHandle entity, const glm::vec3& pos);
glm::vec3 getRotation(EntityHandle entity) const;
void setRotation(EntityHandle entity, const glm::vec3& eulerRadians);
glm::vec3 getScale(EntityHandle entity) const;
void setScale(EntityHandle entity, const glm::vec3& scale);
glm::vec3 getWorldPosition(EntityHandle entity) const;
glm::mat4 getWorldMatrix(EntityHandle entity) const;
glm::vec3 getForward(EntityHandle entity) const;
glm::vec3 getRight(EntityHandle entity) const;
glm::vec3 getUp(EntityHandle entity) const;
CameraType getCameraType(EntityHandle entity) const;
void setCameraType(EntityHandle entity, CameraType type);
float getCameraFov(EntityHandle entity) const;
void setCameraFov(EntityHandle entity, float fov);
float getCameraNear(EntityHandle entity) const;
void setCameraNear(EntityHandle entity, float nearPlane);
float getCameraFar(EntityHandle entity) const;
void setCameraFar(EntityHandle entity, float farPlane);
float getCameraOrthoSize(EntityHandle entity) const;
void setCameraOrthoSize(EntityHandle entity, float size);
bool isCameraMain(EntityHandle entity) const;
void setCameraMain(EntityHandle entity, bool isMain);
LightType getLightType(EntityHandle entity) const;
void setLightType(EntityHandle entity, LightType type);
glm::vec3 getLightColor(EntityHandle entity) const;
void setLightColor(EntityHandle entity, const glm::vec3& color);
float getLightIntensity(EntityHandle entity) const;
void setLightIntensity(EntityHandle entity, float intensity);
float getLightRange(EntityHandle entity) const;
void setLightRange(EntityHandle entity, float range);
bool getLightCastShadows(EntityHandle entity) const;
void setLightCastShadows(EntityHandle entity, bool castShadows);
void setMaterialFloat(EntityHandle entity, const std::string& param, float value);
float getMaterialFloat(EntityHandle entity, const std::string& param, float defaultVal = 0.0f) const;
void setMaterialVec3(EntityHandle entity, const std::string& param, const glm::vec3& value);
glm::vec3 getMaterialVec3(EntityHandle entity, const std::string& param, const glm::vec3& defaultVal = glm::vec3(0.0f)) const;
void setMaterialVec4(EntityHandle entity, const std::string& param, const glm::vec4& value);
glm::vec4 getMaterialVec4(EntityHandle entity, const std::string& param, const glm::vec4& defaultVal = glm::vec4(0.0f)) const;
bool hasMaterialParam(EntityHandle entity, const std::string& param) const;
bool setMaterialTexture(EntityHandle entity, const std::string& slotName, const std::string& filePath);
bool setSpriteTexture(EntityHandle entity, const std::string& filePath);
void setSpriteColor(EntityHandle entity, const glm::vec4& color);
glm::vec4 getSpriteColor(EntityHandle entity) const;
void setSpriteUVRect(EntityHandle entity, const glm::vec4& uvRect);
glm::vec4 getSpriteUVRect(EntityHandle entity) const;
bool isScreenSpaceSprite(EntityHandle entity) const;
void setUIAnchor(EntityHandle entity, UIAnchor anchor, const glm::vec2& offsetPixels);
UIAnchor getUIAnchor(EntityHandle entity) const;
glm::vec2 getUIAnchorOffset(EntityHandle entity) const;
void setText(EntityHandle entity, const std::string& text);
std::string getText(EntityHandle entity) const;
void setTextColor(EntityHandle entity, const glm::vec4& color);
void setTextFontSize(EntityHandle entity, float fontSize);
void setTextAlign(EntityHandle entity, TextHAlign align);
void setTextLayerMask(EntityHandle entity, uint8_t mask);
void setTextSortKey(EntityHandle entity, uint32_t sortKey);
void setTextVisible(EntityHandle entity, bool visible);
bool isTextVisible(EntityHandle entity) const;
bool isVisible(EntityHandle entity) const;
void setVisible(EntityHandle entity, bool visible);
bool getCastShadows(EntityHandle entity) const;
void setCastShadows(EntityHandle entity, bool castShadows);
uint8_t getRenderLayerMask(EntityHandle entity) const;
void setRenderLayerMask(EntityHandle entity, uint8_t mask);
uint32_t getSortKey(EntityHandle entity) const;
void setSortKey(EntityHandle entity, uint32_t sortKey);
EntityHandle getParent(EntityHandle entity) const;
void setParent(EntityHandle child, EntityHandle parent);
std::vector<EntityHandle> getChildren(EntityHandle entity) const;
int getAnimationClipCount(EntityHandle entity) const;
std::string getAnimationClipName(EntityHandle entity, int clipIndex) const;
float getAnimationClipDuration(EntityHandle entity, int clipIndex) const;
void playAnimation(EntityHandle entity, int clipIndex);
void stopAnimation(EntityHandle entity);
bool isAnimationPlaying(EntityHandle entity) const;
float getAnimationSpeed(EntityHandle entity) const;
void setAnimationSpeed(EntityHandle entity, float speed);
float getAnimationTime(EntityHandle entity) const;
void setAnimationTime(EntityHandle entity, float time);
bool isAnimationLooping(EntityHandle entity) const;
void setAnimationLooping(EntityHandle entity, bool loop);
int getCurrentAnimationClip(EntityHandle entity) const;
bool saveToFile(const std::string& path) const;
bool loadFromFile(const std::string& path);
```

<!-- END SOURCE API -->
