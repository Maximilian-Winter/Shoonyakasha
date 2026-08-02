//
// Created by maxim on 12.08.2025.
//

//
// ECS/Systems.h - The systems that breathe life into components
//

#pragma once

#include "Core.h"
#include <chrono>
#include <algorithm>
#include <functional>
#include <string>

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// System Base Class - Common interface for all systems
// ═══════════════════════════════════════════════════════════════

class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void initialize(entt::registry& /*registry*/) {}
    virtual void update(entt::registry& registry, float deltaTime) = 0;
    virtual void cleanup(entt::registry& /*registry*/) {}

    bool enabled = true;
    int priority = 0;      // Lower numbers run first
    std::string name;      // Optional - used for SystemManager::findSystem/removeSystem
};

// ═══════════════════════════════════════════════════════════════
// Transform System - Managing the spatial relationships
// ═══════════════════════════════════════════════════════════════

class TransformSystem : public ISystem {
public:
    void update(entt::registry& registry, float /*deltaTime*/) override {
        if (!enabled) return;

        // First pass: Update all local matrices for dirty transforms
        auto transformView = registry.view<TransformComponent>();
        for (auto entity : transformView) {
            auto& transform = transformView.get<TransformComponent>(entity);
            if (transform.isDirty) {
                transform.localMatrix = transform.getLocalMatrix();
                transform.isDirty = false;
            }
        }

        // Second pass: Update world matrices for root entities (no parent)
        auto rootView = registry.view<TransformComponent>(entt::exclude<HierarchyComponent>);
        for (auto entity : rootView) {
            auto& transform = rootView.get<TransformComponent>(entity);
            transform.worldMatrix = transform.localMatrix;
        }

        // Third pass: Update world matrices for child entities
        updateChildTransforms(registry, entt::null, 0);
    }

    /// Hard cap on hierarchy depth.
    ///
    /// SceneAPI::setParent rejects cycles, but a parent/child graph can also
    /// arrive from Scene::deserialize, which restores links straight from JSON.
    /// Without a bound, a cycle in that data causes a stack overflow instead of a
    /// diagnosable error.
    static constexpr uint32_t kMaxHierarchyDepth = 256;

private:
    /// Compose world matrices down the hierarchy.
    ///
    /// Roots are found with one scan; below that this descends through each node's
    /// own `children` list. The previous version re-scanned every hierarchy entity
    /// once per level — fine when a handful of entities were parented, quadratic
    /// now that the glTF loader builds a real scene graph.
    void updateChildTransforms(entt::registry& registry, entt::entity parent, uint32_t depth) {
        if (depth >= kMaxHierarchyDepth) return;

        if (parent == entt::null) {
            auto rootView = registry.view<HierarchyComponent, TransformComponent>();
            for (auto entity : rootView) {
                auto [hierarchy, transform] =
                    rootView.get<HierarchyComponent, TransformComponent>(entity);
                if (hierarchy.parent != entt::null) continue;

                transform.worldMatrix = transform.localMatrix;
                updateChildTransforms(registry, entity, depth + 1);
            }
            return;
        }

        auto* parentHierarchy = registry.try_get<HierarchyComponent>(parent);
        auto* parentTransform = registry.try_get<TransformComponent>(parent);
        if (!parentHierarchy || !parentTransform) return;

        // By value: the recursion below can reach code that touches the registry,
        // and a moved component would leave this dangling mid-loop.
        const glm::mat4 parentWorld = parentTransform->worldMatrix;
        const std::vector<entt::entity> children = parentHierarchy->children;

        for (auto child : children) {
            if (!registry.valid(child)) continue;
            auto* childTransform = registry.try_get<TransformComponent>(child);
            if (!childTransform) continue;

            childTransform->worldMatrix = parentWorld * childTransform->localMatrix;
            updateChildTransforms(registry, child, depth + 1);
        }
    }
};

// ═══════════════════════════════════════════════════════════════
// Camera System - Managing the viewpoint into the world
// ═══════════════════════════════════════════════════════════════

class CameraSystem : public ISystem {
public:
    void update(entt::registry& registry, float /*deltaTime*/) override {
        if (!enabled) return;

        auto cameraView = registry.view<CameraComponent, TransformComponent>();

        for (auto entity : cameraView) {
            auto [camera, transform] = cameraView.get<CameraComponent, TransformComponent>(entity);

            // Update projection matrix
            camera.projectionMatrix = camera.getProjectionMatrix();

            // Update view matrix (inverse of camera's world transform)
            camera.viewMatrix = glm::inverse(transform.worldMatrix);
        }
    }

    entt::entity getMainCamera(const entt::registry& registry) const {
        auto cameraView = registry.view<CameraComponent>();
        for (auto entity : cameraView) {
            const auto& camera = cameraView.get<CameraComponent>(entity);
            if (camera.isMainCamera) {
                return entity;
            }
        }

        // If no main camera, return the first camera found
        if (!cameraView.empty()) {
            return *cameraView.begin();
        }

        return entt::null;
    }
};

// PhysicsSystem is now in its own file: ECS/PhysicsSystem.h
// (Bullet headers are isolated in PhysicsSystem.cpp via PIMPL)

// SkeletalAnimationSystem is in ECS/SkeletalAnimationSystem.h
// (Replaces the legacy placeholder AnimationSystem)

// ═══════════════════════════════════════════════════════════════
// Lifetime System - Managing entity lifecycles
// ═══════════════════════════════════════════════════════════════

class LifetimeSystem : public ISystem {
public:
    void update(entt::registry& registry, float deltaTime) override {
        if (!enabled) return;

        std::vector<entt::entity> toDestroy;

        auto lifetimeView = registry.view<LifetimeComponent>();
        for (auto entity : lifetimeView) {
            auto& lifetime = lifetimeView.get<LifetimeComponent>(entity);
            lifetime.timeToLive -= deltaTime;

            if (lifetime.timeToLive <= 0.0f && lifetime.destroyOnExpire) {
                toDestroy.push_back(entity);
            }
        }

        // A parent and its child can both expire on the same frame. Destroying
        // the parent cascades onto the child, so the second call here sees an
        // already-dead handle — destroyEntity guards validity for exactly this.
        for (auto entity : toDestroy) {
            EntityHelper::destroyEntity(registry, entity);
        }
    }
};

// ═══════════════════════════════════════════════════════════════
// Callback System - A system driven by an opaque per-frame callback
// ═══════════════════════════════════════════════════════════════
//
// Generic bridge for any scripting layer (Python today, potentially
// others later) to register per-frame logic without a new C++ type per
// script. The callback returns false to report a failure (e.g. it caught
// a scripting-language exception) - after maxConsecutiveFailures such
// reports in a row, the system disables itself (enabled = false) so a
// broken script doesn't spend time running every frame forever. A
// success resets the counter. maxConsecutiveFailures <= 0 disables
// auto-disable entirely (the system just keeps reporting failures).
//

class CallbackSystem : public ISystem {
public:
    using UpdateFn = std::function<bool(float)>;  // returns false on failure

    // priority is taken as a constructor argument (rather than set via the
    // ->priority member after addSystem<T>() returns) because
    // SystemManager::addSystem sorts by priority immediately after
    // construction, using whatever priority the object already has.
    CallbackSystem(std::string systemName, UpdateFn fn, int priorityValue = 0,
                   int maxConsecutiveFailures = 0)
        : m_fn(std::move(fn))
        , m_maxConsecutiveFailures(maxConsecutiveFailures)
    {
        name = std::move(systemName);
        priority = priorityValue;
    }

    void update(entt::registry& /*registry*/, float deltaTime) override {
        if (!m_fn) return;

        bool ok = m_fn(deltaTime);
        if (ok) {
            m_consecutiveFailures = 0;
            return;
        }

        ++m_consecutiveFailures;
        if (m_maxConsecutiveFailures > 0 && m_consecutiveFailures >= m_maxConsecutiveFailures) {
            enabled = false;
        }
    }

    int getConsecutiveFailures() const { return m_consecutiveFailures; }
    int getMaxConsecutiveFailures() const { return m_maxConsecutiveFailures; }
    void setMaxConsecutiveFailures(int max) { m_maxConsecutiveFailures = max; }
    void resetFailureCount() { m_consecutiveFailures = 0; }

private:
    UpdateFn m_fn;
    int m_maxConsecutiveFailures;
    int m_consecutiveFailures = 0;
};

// ═══════════════════════════════════════════════════════════════
// System Manager - Orchestrating all systems in harmony
// ═══════════════════════════════════════════════════════════════

/// Owns the systems and drives them in priority order.
///
/// Adding or removing a system from inside a system's own update() used to be
/// undefined behaviour: update() range-fors over the vector while addSystem
/// appends and sorts it and removeSystem erases from it. Removing the running
/// system additionally destroyed the std::function that was executing.
/// EcsAPI exposes both to scripting languages, where "unregister this system
/// when it is done" is the obvious idiom, so the mutations are deferred to the
/// end of the update instead.
class SystemManager {
public:
    template<typename T, typename... Args>
    T* addSystem(Args&&... args) {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = system.get();

        if (m_iterating) {
            m_pendingAdds.emplace_back(std::move(system));
        } else {
            m_systems.emplace_back(std::move(system));
            sortByPriority();
        }

        return ptr;
    }

    template<typename T>
    T* getSystem() {
        for (auto& system : m_systems) {
            if (auto* casted = dynamic_cast<T*>(system.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    // Find a system by name (set via ISystem::name). Returns nullptr if not found
    // or if multiple systems share a name, the first match in current order.
    ISystem* findSystem(const std::string& systemName) {
        for (auto& system : m_systems) {
            if (system->name == systemName) return system.get();
        }
        return nullptr;
    }

    /// Remove a system by name. Returns true if a matching system was found.
    ///
    /// Called from within an update, the removal is deferred to the end of that
    /// update — so a system may remove itself. The system's cleanup() runs
    /// before it is destroyed, which matters for anything holding EnTT signal
    /// connections: PhysicsSystem's on_construct/on_destroy sinks otherwise
    /// survive into freed memory.
    bool removeSystem(const std::string& systemName) {
        if (m_iterating) {
            if (!findSystem(systemName)) return false;
            m_pendingRemovals.push_back(systemName);
            return true;
        }

        auto it = std::find_if(m_systems.begin(), m_systems.end(),
            [&](const std::unique_ptr<ISystem>& s) { return s->name == systemName; });
        if (it == m_systems.end()) return false;

        if (m_registry) (*it)->cleanup(*m_registry);
        m_systems.erase(it);
        return true;
    }

    void initialize(entt::registry& registry) {
        m_registry = &registry;   // so removeSystem can run cleanup()
        for (auto& system : m_systems) {
            system->initialize(registry);
        }
    }

    void update(entt::registry& registry, float deltaTime) {
        m_registry = &registry;

        m_iterating = true;
        for (auto& system : m_systems) {
            if (system->enabled) {
                system->update(registry, deltaTime);
            }
        }
        m_iterating = false;

        applyPendingChanges(registry);
    }

    void cleanup(entt::registry& registry) {
        for (auto& system : m_systems) {
            system->cleanup(registry);
        }
    }

private:
    void sortByPriority() {
        std::sort(m_systems.begin(), m_systems.end(),
                 [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b) {
                     return a->priority < b->priority;
                 });
    }

    void applyPendingChanges(entt::registry& registry) {
        if (m_pendingRemovals.empty() && m_pendingAdds.empty()) return;

        for (const auto& name : m_pendingRemovals) {
            auto it = std::find_if(m_systems.begin(), m_systems.end(),
                [&](const std::unique_ptr<ISystem>& s) { return s->name == name; });
            if (it != m_systems.end()) {
                (*it)->cleanup(registry);
                m_systems.erase(it);
            }
        }
        m_pendingRemovals.clear();

        for (auto& system : m_pendingAdds) {
            system->initialize(registry);   // it missed this cycle's initialize()
            m_systems.emplace_back(std::move(system));
        }
        m_pendingAdds.clear();

        sortByPriority();
    }

    std::vector<std::unique_ptr<ISystem>> m_systems;

    // Deferred mutation while update() is walking m_systems
    std::vector<std::unique_ptr<ISystem>> m_pendingAdds;
    std::vector<std::string> m_pendingRemovals;
    bool m_iterating = false;

    entt::registry* m_registry = nullptr;   // last registry seen; for cleanup on removal
};

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;