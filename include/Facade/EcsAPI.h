//
// Facade/EcsAPI.h - Low-level ECS access for scripting bindings
//
// No Vulkan, no EnTT, no templates in this header.
//
// Complements SceneAPI (which owns entity lifecycle and typed accessors
// for built-in components) with two things no scripting language can get
// from the fixed, compile-time-typed ComponentRegistry:
//
//   1. Generic opaque component storage - attach/read/remove arbitrary
//      script-defined data per entity, keyed by name. The engine core
//      never inspects the stored value (see ECS/ScriptComponentBag.h).
//   2. Generic per-frame systems - register a callback that runs every
//      frame through the same SystemManager/priority ordering as the
//      built-in systems (TransformSystem, CameraSystem, ...), without
//      writing a new C++ ISystem subclass per script.
//
// Both are intentionally scripting-language-agnostic at this layer (a
// std::shared_ptr<void> payload, a std::function<bool(float)> callback) -
// the Python-specific PyObject* marshalling lives entirely in
// python/shoonyakasha/_ecs_bridge.h, same separation SceneAPI/EngineAPI
// already use for Python callbacks (_callback_bridge.h).
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

namespace ECS {
    class Scene;
    class SystemManager;
}

namespace Facade {

class EcsAPI {
public:
    /// Construct wrapping an ECS::Scene (used by EngineAPI internally)
    explicit EcsAPI(ECS::Scene& scene);

    // Test-only: wrap a raw registry + system manager without a Scene.
    // Include <entt/entt.hpp>, "ECS/Core.h" and "ECS/Systems.h" BEFORE
    // this header.
#ifdef SHOONYAKASHA_TESTING
    EcsAPI(entt::registry& registry, ECS::SystemManager& systemManager);
#endif

    ~EcsAPI();

    EcsAPI(const EcsAPI&) = delete;
    EcsAPI& operator=(const EcsAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Script Component Access (generic, opaque payload)
    // ═══════════════════════════════════════════════════════════

    /// Attach (or replace) a script-defined component under `name`.
    /// The engine takes shared ownership of `data` - it is released when
    /// removed, overwritten, or the entity is destroyed.
    void setComponent(EntityHandle entity, const std::string& name, std::shared_ptr<void> data);

    /// Get a previously-attached component's opaque payload (nullptr if absent).
    std::shared_ptr<void> getComponent(EntityHandle entity, const std::string& name) const;

    bool hasComponent(EntityHandle entity, const std::string& name) const;

    /// Detach a script-defined component. Returns true if it was present.
    bool removeComponent(EntityHandle entity, const std::string& name);

    /// Names of all script-defined components attached to an entity.
    std::vector<std::string> getComponentNames(EntityHandle entity) const;

    /// All entities carrying a script-defined component named `name`.
    /// O(entities with *any* script component) - see ScriptComponentBag.h.
    std::vector<EntityHandle> findEntitiesWithComponent(const std::string& name) const;

    // ═══════════════════════════════════════════════════════════
    // System Management (generic per-frame callback)
    // ═══════════════════════════════════════════════════════════

    // Returns false to report a failure for this frame (e.g. the script
    // raised an exception). After maxConsecutiveFailures such reports in
    // a row the system auto-disables (see setSystemEnabled/isSystemEnabled).
    using SystemUpdateFn = std::function<bool(float)>;

    /// Register a new per-frame system. Fails (returns false, does not
    /// replace) if a system named `name` is already registered.
    /// maxConsecutiveFailures <= 0 disables auto-disable entirely.
    bool addSystem(const std::string& name, SystemUpdateFn fn,
                   int priority = 0, int maxConsecutiveFailures = 5);

    bool removeSystem(const std::string& name);
    bool hasSystem(const std::string& name) const;

    bool setSystemEnabled(const std::string& name, bool enabled);
    bool isSystemEnabled(const std::string& name) const;

    /// Number of consecutive failures reported so far (0 if healthy or not found).
    int getSystemFailureCount(const std::string& name) const;
    int getSystemMaxFailures(const std::string& name) const;
    void setSystemMaxFailures(const std::string& name, int max);
    void resetSystemFailureCount(const std::string& name);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
