//
// Facade/PhysicsAPI.cpp - Delegates to PhysicsSystem (already PIMPL'd)
//

#include "PhysicsAPIImpl.h"  // Impl definition shared with EngineAPI.cpp
#include "FacadeInternal.h"

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

PhysicsAPI::PhysicsAPI()
    : m_impl(std::make_unique<Impl>())
{}

PhysicsAPI::~PhysicsAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Enable / Disable
// ═══════════════════════════════════════════════════════════════

bool PhysicsAPI::isEnabled() const {
    if (!m_impl->physicsSystem) return false;
    return m_impl->physicsSystem->enabled;
}

void PhysicsAPI::setEnabled(bool enabled) {
    if (m_impl->physicsSystem) m_impl->physicsSystem->enabled = enabled;
}

// ═══════════════════════════════════════════════════════════════
// World Configuration
// ═══════════════════════════════════════════════════════════════

void PhysicsAPI::setGravity(const glm::vec3& gravity) {
    if (m_impl->physicsSystem) m_impl->physicsSystem->setGravity(gravity);
}

glm::vec3 PhysicsAPI::getGravity() const {
    if (!m_impl->physicsSystem) return glm::vec3(0.f, -9.81f, 0.f);
    return m_impl->physicsSystem->getGravity();
}

void PhysicsAPI::setFixedTimeStep(float timeStep) {
    if (m_impl->physicsSystem) m_impl->physicsSystem->setFixedTimeStep(timeStep);
}

float PhysicsAPI::getFixedTimeStep() const {
    if (!m_impl->physicsSystem) return 1.f / 60.f;
    return m_impl->physicsSystem->getFixedTimeStep();
}

void PhysicsAPI::setMaxSubSteps(int maxSubSteps) {
    if (m_impl->physicsSystem) m_impl->physicsSystem->setMaxSubSteps(maxSubSteps);
}

int PhysicsAPI::getMaxSubSteps() const {
    if (!m_impl->physicsSystem) return 10;
    return m_impl->physicsSystem->getMaxSubSteps();
}

// ═══════════════════════════════════════════════════════════════
// Forces / Impulses
// ═══════════════════════════════════════════════════════════════

void PhysicsAPI::addForce(EntityHandle entity, const glm::vec3& force) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->addForce(*m_impl->registry, toEntt(entity), force);
}

void PhysicsAPI::addImpulse(EntityHandle entity, const glm::vec3& impulse) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->addImpulse(*m_impl->registry, toEntt(entity), impulse);
}

void PhysicsAPI::addTorqueImpulse(EntityHandle entity, const glm::vec3& torque) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->addTorqueImpulse(*m_impl->registry, toEntt(entity), torque);
}

// ═══════════════════════════════════════════════════════════════
// Velocity
// ═══════════════════════════════════════════════════════════════

void PhysicsAPI::setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->setLinearVelocity(*m_impl->registry, toEntt(entity), velocity);
}

glm::vec3 PhysicsAPI::getLinearVelocity(EntityHandle entity) const {
    if (!m_impl->physicsSystem || !m_impl->registry) return glm::vec3(0.f);
    return m_impl->physicsSystem->getLinearVelocity(*m_impl->registry, toEntt(entity));
}

void PhysicsAPI::setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->setAngularVelocity(*m_impl->registry, toEntt(entity), velocity);
}

glm::vec3 PhysicsAPI::getAngularVelocity(EntityHandle entity) const {
    if (!m_impl->physicsSystem || !m_impl->registry) return glm::vec3(0.f);
    return m_impl->physicsSystem->getAngularVelocity(*m_impl->registry, toEntt(entity));
}

// ═══════════════════════════════════════════════════════════════
// Body Management
// ═══════════════════════════════════════════════════════════════

void PhysicsAPI::rebuildBody(EntityHandle entity) {
    if (!m_impl->physicsSystem || !m_impl->registry) return;
    m_impl->physicsSystem->rebuildBody(*m_impl->registry, toEntt(entity));
}

uint32_t PhysicsAPI::getBodyCount() const {
    if (!m_impl->physicsSystem) return 0;
    return m_impl->physicsSystem->getBodyCount();
}

} // namespace Facade
} // namespace Shoonyakasha
