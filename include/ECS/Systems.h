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
    int priority = 0; // Lower numbers run first
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