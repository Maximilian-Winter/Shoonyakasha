//
// ECS/PhysicsSystem.h - Bullet3 Physics Integration
//
// 重力之道 — The Way of Gravity
//
// Integrates Bullet3 rigid body dynamics with the ECS.
// All Bullet types are isolated in the .cpp via PIMPL — this header is Bullet-free.
//
// Priority: 5 (after TransformSystem=0, before CameraSystem=10)
//

#pragma once

#include "Systems.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// PhysicsSystem - Bullet3 Dynamics Integrated with ECS
// ═══════════════════════════════════════════════════════════════
//
// Uses EnTT on_construct/on_destroy signals to automatically
// create and remove physics bodies when RigidBodyComponent
// is added to or removed from entities.
//
// Sync model:
//   ECS → Bullet: Kinematic bodies (before simulation step)
//   Bullet → ECS: Dynamic bodies (after simulation step)
//

class PhysicsSystem : public ISystem {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    // Non-copyable, non-movable (owns Bullet world)
    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    // ─── ISystem Interface ────────────────────────────────────

    void initialize(entt::registry& registry) override;
    void update(entt::registry& registry, float deltaTime) override;
    void cleanup(entt::registry& registry) override;

    // ─── Force / Impulse API ──────────────────────────────────
    // All methods take entity + registry so they can be called from anywhere.

    /// Apply a continuous force (accumulated over substeps).
    void addForce(entt::registry& registry, entt::entity entity,
                  const glm::vec3& force);

    /// Apply a force at a world-space point (creates torque).
    void addForceAtPoint(entt::registry& registry, entt::entity entity,
                         const glm::vec3& force, const glm::vec3& point);

    /// Apply an instantaneous impulse (velocity change).
    void addImpulse(entt::registry& registry, entt::entity entity,
                    const glm::vec3& impulse);

    /// Apply an instantaneous torque impulse.
    void addTorqueImpulse(entt::registry& registry, entt::entity entity,
                          const glm::vec3& torque);

    /// Set linear velocity directly.
    void setLinearVelocity(entt::registry& registry, entt::entity entity,
                           const glm::vec3& velocity);

    /// Set angular velocity directly.
    void setAngularVelocity(entt::registry& registry, entt::entity entity,
                            const glm::vec3& velocity);

    // ─── World Configuration ──────────────────────────────────

    /// Set global gravity (default: 0, -9.81, 0).
    void setGravity(const glm::vec3& gravity);
    glm::vec3 getGravity() const;

    /// Set fixed timestep for physics simulation (default: 1/60).
    void setFixedTimeStep(float timeStep);
    float getFixedTimeStep() const;

    /// Set maximum substeps per frame (default: 10).
    void setMaxSubSteps(int maxSubSteps);
    int getMaxSubSteps() const;

    // ─── Query API ────────────────────────────────────────────

    /// Get the linear velocity of an entity's rigid body.
    glm::vec3 getLinearVelocity(entt::registry& registry,
                                entt::entity entity) const;

    /// Get the angular velocity of an entity's rigid body.
    glm::vec3 getAngularVelocity(entt::registry& registry,
                                 entt::entity entity) const;

    /// Get number of tracked physics bodies.
    uint32_t getBodyCount() const;

    // ─── Manual Body Management ───────────────────────────────
    // Normally handled automatically via EnTT signals.
    // These are exposed for edge cases.

    /// Force-create a physics body for an entity (if not already created).
    void createBody(entt::registry& registry, entt::entity entity);

    /// Force-remove the physics body for an entity.
    void removeBody(entt::registry& registry, entt::entity entity);

    /// Rebuild an entity's body (e.g., after changing collider shape at runtime).
    void rebuildBody(entt::registry& registry, entt::entity entity);

private:
    // ─── PIMPL — All Bullet types hidden ──────────────────────
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ECS
} // namespace Shoonyakasha

// Backward compatibility alias
namespace ECS = Shoonyakasha::ECS;
