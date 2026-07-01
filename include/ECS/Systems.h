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
    virtual void initialize(entt::registry& registry) {}
    virtual void update(entt::registry& registry, float deltaTime) = 0;
    virtual void cleanup(entt::registry& registry) {}

    bool enabled = true;
    int priority = 0;      // Lower numbers run first
    std::string name;      // Optional - used for SystemManager::findSystem/removeSystem
};

// ═══════════════════════════════════════════════════════════════
// Transform System - Managing the spatial relationships
// ═══════════════════════════════════════════════════════════════

class TransformSystem : public ISystem {
public:
    void update(entt::registry& registry, float deltaTime) override {
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
        updateChildTransforms(registry, entt::null);
    }

private:
    void updateChildTransforms(entt::registry& registry, entt::entity parent) {
        auto hierarchyView = registry.view<HierarchyComponent, TransformComponent>();

        for (auto entity : hierarchyView) {
            auto [hierarchy, transform] = hierarchyView.get<HierarchyComponent, TransformComponent>(entity);

            if (hierarchy.parent == parent) {
                if (parent == entt::null) {
                    // Root entity
                    transform.worldMatrix = transform.localMatrix;
                } else {
                    // Child entity
                    if (auto* parentTransform = registry.try_get<TransformComponent>(parent)) {
                        transform.worldMatrix = parentTransform->worldMatrix * transform.localMatrix;
                    }
                }

                // Recursively update children
                updateChildTransforms(registry, entity);
            }
        }
    }
};

// ═══════════════════════════════════════════════════════════════
// Camera System - Managing the viewpoint into the world
// ═══════════════════════════════════════════════════════════════

class CameraSystem : public ISystem {
public:
    void update(entt::registry& registry, float deltaTime) override {
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

    void update(entt::registry& registry, float deltaTime) override {
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

class SystemManager {
public:
    template<typename T, typename... Args>
    T* addSystem(Args&&... args) {
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = system.get();
        m_systems.emplace_back(std::move(system));

        // Sort systems by priority
        std::sort(m_systems.begin(), m_systems.end(),
                 [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b) {
                     return a->priority < b->priority;
                 });

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

    // Remove a system by name. Returns true if a system was found and removed.
    bool removeSystem(const std::string& systemName) {
        auto it = std::find_if(m_systems.begin(), m_systems.end(),
            [&](const std::unique_ptr<ISystem>& s) { return s->name == systemName; });
        if (it == m_systems.end()) return false;
        m_systems.erase(it);
        return true;
    }

    void initialize(entt::registry& registry) {
        for (auto& system : m_systems) {
            system->initialize(registry);
        }
    }

    void update(entt::registry& registry, float deltaTime) {
        for (auto& system : m_systems) {
            if (system->enabled) {
                system->update(registry, deltaTime);
            }
        }
    }

    void cleanup(entt::registry& registry) {
        for (auto& system : m_systems) {
            system->cleanup(registry);
        }
    }

private:
    std::vector<std::unique_ptr<ISystem>> m_systems;
};

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;