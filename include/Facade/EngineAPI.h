//
// Facade/EngineAPI.h - Python-friendly engine lifecycle wrapper
//
// No Vulkan, no EnTT, no ApplicationBase in this header.
// All internals hidden behind PIMPL.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Shoonyakasha {
namespace Facade {

// Forward declare sub-APIs
class SceneAPI;
class InputAPI;
class PhysicsAPI;

class EngineAPI {
public:
    explicit EngineAPI(const EngineConfig& config);
    ~EngineAPI();

    // Non-copyable, non-movable
    EngineAPI(const EngineAPI&) = delete;
    EngineAPI& operator=(const EngineAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Lifecycle
    // ═══════════════════════════════════════════════════════════

    /// Run the engine. Blocks until the window is closed.
    void run();

    // ═══════════════════════════════════════════════════════════
    // Callback Registration (set before or during run)
    // ═══════════════════════════════════════════════════════════

    void setOnInit(VoidCallback cb);
    void setOnPostInit(VoidCallback cb);
    void setOnUpdate(UpdateCallback cb);
    void setOnPreRender(UpdateCallback cb);
    void setOnPostRender(VoidCallback cb);
    void setOnKeyPressed(KeyCallback cb);
    void setOnResize(ResizeCallback cb);
    void setOnCleanup(VoidCallback cb);

    // ═══════════════════════════════════════════════════════════
    // Sub-API Access (valid after construction; populated after onInit)
    // ═══════════════════════════════════════════════════════════

    SceneAPI&   getScene();
    InputAPI&   getInput();
    PhysicsAPI& getPhysics();

    // ═══════════════════════════════════════════════════════════
    // Convenience Helpers
    // ═══════════════════════════════════════════════════════════

    /// Create a camera entity with controller.
    EntityHandle createCamera(const glm::vec3& pos,
                              float fov = 60.f,
                              float speed = 8.f,
                              float nearPlane = 0.1f,
                              float farPlane = 1000.f);

    /// Load a glTF scene into the active ECS scene.
    GltfResult loadGltfScene(const std::string& path,
                             const GltfOptions& opts = {});

    /// Create a directional light entity.
    EntityHandle createDirectionalLight(const glm::vec3& direction,
                                        const glm::vec3& color = glm::vec3(1.f),
                                        float intensity = 2.f);

    /// Create a point light entity.
    EntityHandle createPointLight(const glm::vec3& position,
                                  const glm::vec3& color = glm::vec3(1.f),
                                  float intensity = 5.f,
                                  float range = 15.f);

    /// Get the camera entity handle.
    EntityHandle getCameraEntity() const;

    /// Get frame delta time (seconds).
    float getDeltaTime() const;

    // ═══════════════════════════════════════════════════════════
    // Scene Context Custom Values (for shader uniforms via dot-paths)
    // ═══════════════════════════════════════════════════════════

    void setCustomFloat(const std::string& key, float value);
    void setCustomVec2(const std::string& key, const glm::vec2& value);
    void setCustomVec3(const std::string& key, const glm::vec3& value);
    void setCustomVec4(const std::string& key, const glm::vec4& value);
    void setCustomUint(const std::string& key, uint32_t value);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
