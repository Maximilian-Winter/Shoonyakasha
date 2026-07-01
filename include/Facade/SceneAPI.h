//
// Facade/SceneAPI.h - Python-friendly scene, entity, and component access
//
// No Vulkan, no EnTT, no templates in this header.
// All component access uses concrete typed methods.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Shoonyakasha {

// Forward declare — Scene.h is never included here
namespace ECS {
    class Scene;
    class ComponentRegistry;
}
class Sprite2DManager;

namespace Facade {

class SceneAPI {
public:
    /// Construct wrapping an ECS::Scene (used by EngineAPI internally)
    explicit SceneAPI(ECS::Scene& scene);

    // Test-only: wrap a raw registry without Scene (no GPU needed).
    // Include <entt/entt.hpp> and "ECS/Core.h" BEFORE this header.
#ifdef SHOONYAKASHA_TESTING
    SceneAPI(entt::registry& registry, ECS::ComponentRegistry& componentRegistry);
#endif

    ~SceneAPI();

    // Non-copyable
    SceneAPI(const SceneAPI&) = delete;
    SceneAPI& operator=(const SceneAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Entity Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Create an entity with optional name. Auto-adds Transform + Active.
    EntityHandle createEntity(const std::string& name = "");

    /// Destroy an entity and remove it from parent hierarchy.
    void destroyEntity(EntityHandle entity);

    /// Check if an entity handle is valid.
    bool isValid(EntityHandle entity) const;

    /// Get total entity count.
    size_t getEntityCount() const;

    // ═══════════════════════════════════════════════════════════
    // Entity Queries
    // ═══════════════════════════════════════════════════════════

    /// Find entity by name (returns NullEntity if not found).
    EntityHandle findEntityByName(const std::string& name) const;

    /// Find all entities with a given tag.
    std::vector<EntityHandle> findEntitiesWithTag(const std::string& tag) const;

    /// Get the main camera entity.
    EntityHandle getMainCamera();

    /// Get all entity handles (for iteration from Python).
    std::vector<EntityHandle> getAllEntities() const;

    // ═══════════════════════════════════════════════════════════
    // Component Management (string-based)
    // ═══════════════════════════════════════════════════════════

    /// Add a component by registered name (e.g. "Transform", "Light", "Camera").
    bool addComponent(EntityHandle entity, const std::string& componentName);

    /// Remove a component by name.
    bool removeComponent(EntityHandle entity, const std::string& componentName);

    /// Check if entity has a component by name.
    bool hasComponent(EntityHandle entity, const std::string& componentName) const;

    /// List all registered component type names.
    std::vector<std::string> getComponentNames() const;

    // ═══════════════════════════════════════════════════════════
    // Name / Tag / Active
    // ═══════════════════════════════════════════════════════════

    std::string getName(EntityHandle entity) const;
    void setName(EntityHandle entity, const std::string& name);

    std::string getTag(EntityHandle entity) const;
    void setTag(EntityHandle entity, const std::string& tag);

    bool isActive(EntityHandle entity) const;
    void setActive(EntityHandle entity, bool active);

    // ═══════════════════════════════════════════════════════════
    // Transform Access
    // ═══════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════
    // Camera Access
    // ═══════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════
    // Light Access
    // ═══════════════════════════════════════════════════════════

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

    // ═══════════════════════════════════════════════════════════
    // Material Access (explicit typed methods — no templates)
    // ═══════════════════════════════════════════════════════════

    void setMaterialFloat(EntityHandle entity, const std::string& param, float value);
    float getMaterialFloat(EntityHandle entity, const std::string& param,
                           float defaultVal = 0.0f) const;

    void setMaterialVec3(EntityHandle entity, const std::string& param,
                         const glm::vec3& value);
    glm::vec3 getMaterialVec3(EntityHandle entity, const std::string& param,
                              const glm::vec3& defaultVal = glm::vec3(0.0f)) const;

    void setMaterialVec4(EntityHandle entity, const std::string& param,
                         const glm::vec4& value);
    glm::vec4 getMaterialVec4(EntityHandle entity, const std::string& param,
                              const glm::vec4& defaultVal = glm::vec4(0.0f)) const;

    bool hasMaterialParam(EntityHandle entity, const std::string& param) const;

    /// Load an image file and assign it to a named texture slot
    /// (e.g. "albedoMap", "spriteTexture"). Requires the engine's
    /// Sprite2DManager to be wired (always true once the engine is running).
    bool setMaterialTexture(EntityHandle entity, const std::string& slotName,
                            const std::string& filePath);

    // ═══════════════════════════════════════════════════════════
    // Sprite / UI Access
    // ═══════════════════════════════════════════════════════════

    /// Load an image file as this entity's sprite texture ("spriteTexture" slot).
    bool setSpriteTexture(EntityHandle entity, const std::string& filePath);

    void setSpriteColor(EntityHandle entity, const glm::vec4& color);
    glm::vec4 getSpriteColor(EntityHandle entity) const;

    /// Sub-rectangle of the texture to display, in normalized UV space
    /// (u0, v0, u1, v1). Defaults to the full texture (0,0,1,1).
    void setSpriteUVRect(EntityHandle entity, const glm::vec4& uvRect);
    glm::vec4 getSpriteUVRect(EntityHandle entity) const;

    bool isScreenSpaceSprite(EntityHandle entity) const;

    /// Anchor a screen-space sprite/UI panel/text label to a viewport
    /// corner/edge/center. Only meaningful for entities created with
    /// createUIPanel/createText (screen-space sprites).
    void setUIAnchor(EntityHandle entity, UIAnchor anchor, const glm::vec2& offsetPixels);
    UIAnchor getUIAnchor(EntityHandle entity) const;
    glm::vec2 getUIAnchorOffset(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════════════
    // Text Access
    // ═══════════════════════════════════════════════════════════

    void setText(EntityHandle entity, const std::string& text);
    std::string getText(EntityHandle entity) const;

    void setTextColor(EntityHandle entity, const glm::vec4& color);
    void setTextFontSize(EntityHandle entity, float fontSize);
    void setTextAlign(EntityHandle entity, TextHAlign align);

    // ═══════════════════════════════════════════════════════════
    // Renderable Tag Access
    // ═══════════════════════════════════════════════════════════

    bool isVisible(EntityHandle entity) const;
    void setVisible(EntityHandle entity, bool visible);

    bool getCastShadows(EntityHandle entity) const;
    void setCastShadows(EntityHandle entity, bool castShadows);

    // ═══════════════════════════════════════════════════════════
    // Hierarchy Access
    // ═══════════════════════════════════════════════════════════

    EntityHandle getParent(EntityHandle entity) const;
    void setParent(EntityHandle child, EntityHandle parent);
    std::vector<EntityHandle> getChildren(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════════════
    // Animation Access
    // ═══════════════════════════════════════════════════════════

    /// Get number of animation clips on an entity (0 if not animated).
    int getAnimationClipCount(EntityHandle entity) const;

    /// Get animation clip name by index.
    std::string getAnimationClipName(EntityHandle entity, int clipIndex) const;

    /// Get animation clip duration (seconds) by index.
    float getAnimationClipDuration(EntityHandle entity, int clipIndex) const;

    /// Play an animation clip by index (resets time, sets playing).
    void playAnimation(EntityHandle entity, int clipIndex);

    /// Stop animation playback (pauses and resets time to 0).
    void stopAnimation(EntityHandle entity);

    /// Check if animation is currently playing.
    bool isAnimationPlaying(EntityHandle entity) const;

    /// Get animation playback speed (default 1.0).
    float getAnimationSpeed(EntityHandle entity) const;

    /// Set animation playback speed.
    void setAnimationSpeed(EntityHandle entity, float speed);

    /// Get current animation time (seconds).
    float getAnimationTime(EntityHandle entity) const;

    /// Set current animation time (seconds).
    void setAnimationTime(EntityHandle entity, float time);

    /// Check if animation is set to loop.
    bool isAnimationLooping(EntityHandle entity) const;

    /// Set animation looping.
    void setAnimationLooping(EntityHandle entity, bool loop);

    /// Get current animation clip index (-1 if none).
    int getCurrentAnimationClip(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════════════
    // Serialization
    // ═══════════════════════════════════════════════════════════

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    // ═══════════════════════════════════════════════════════════
    // Internal wiring — not part of the stable public API surface.
    // Called once by EngineAPI during onInit(), after Sprite2DManager
    // exists, so texture-loading facade calls have somewhere to go.
    // ═══════════════════════════════════════════════════════════

    void wireSprite2DManager(Sprite2DManager* manager);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
