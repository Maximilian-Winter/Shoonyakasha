//
// Facade/PhysicsAPI.h - Python-friendly physics control
//
// No Bullet, no EnTT in this header.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <memory>

namespace Shoonyakasha {
namespace Facade {

class PhysicsAPI {
public:
    PhysicsAPI();
    ~PhysicsAPI();

    // Non-copyable
    PhysicsAPI(const PhysicsAPI&) = delete;
    PhysicsAPI& operator=(const PhysicsAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Enable / Disable
    // ═══════════════════════════════════════════════════════════

    bool isEnabled() const;
    void setEnabled(bool enabled);

    // ═══════════════════════════════════════════════════════════
    // World Configuration
    // ═══════════════════════════════════════════════════════════

    void setGravity(const glm::vec3& gravity);
    glm::vec3 getGravity() const;

    void setFixedTimeStep(float timeStep);
    float getFixedTimeStep() const;

    void setMaxSubSteps(int maxSubSteps);
    int getMaxSubSteps() const;

    // ═══════════════════════════════════════════════════════════
    // Forces / Impulses
    // ═══════════════════════════════════════════════════════════

    /// Apply a continuous force (accumulated over substeps).
    void addForce(EntityHandle entity, const glm::vec3& force);

    /// Apply an instantaneous impulse (velocity change).
    void addImpulse(EntityHandle entity, const glm::vec3& impulse);

    /// Apply an instantaneous torque impulse.
    void addTorqueImpulse(EntityHandle entity, const glm::vec3& torque);

    // ═══════════════════════════════════════════════════════════
    // Velocity
    // ═══════════════════════════════════════════════════════════

    void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity);
    glm::vec3 getLinearVelocity(EntityHandle entity) const;

    void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity);
    glm::vec3 getAngularVelocity(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════════════
    // Body Management
    // ═══════════════════════════════════════════════════════════

    /// Rebuild physics body (after changing collider shape at runtime).
    void rebuildBody(EntityHandle entity);

    /// Get total tracked physics body count.
    uint32_t getBodyCount() const;

    // Impl is forward-declared (opaque) — public so EngineAPI can wire internals
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
