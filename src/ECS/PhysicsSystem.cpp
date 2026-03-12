//
// PhysicsSystem.cpp - Bullet3 Physics Integration
//
// 重力之道 — The Way of Gravity
//
// All Bullet types live here and nowhere else.
// The PIMPL pattern keeps Bullet headers from leaking into the engine's public API.
//

#include "ECS/PhysicsSystem.h"

// Bullet includes — ONLY here in the entire engine
#include "bullet/LinearMath/btVector3.h"
#include "bullet/LinearMath/btQuaternion.h"
#include "bullet/btBulletCollisionCommon.h"
#include "bullet/btBulletDynamicsCommon.h"

#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace Shoonyakasha {
namespace ECS {

// ═══════════════════════════════════════════════════════════════
// GLM ↔ Bullet Conversions
// ═══════════════════════════════════════════════════════════════

static btVector3 toBt(const glm::vec3& v) {
    return btVector3(v.x, v.y, v.z);
}

static glm::vec3 toGlm(const btVector3& v) {
    return glm::vec3(v.x(), v.y(), v.z());
}

// Convert TransformComponent euler angles (Y→X→Z order) to btQuaternion.
// Matches the rotation order in TransformComponent::getLocalMatrix():
//   R = Ry(yaw) * Rx(pitch) * Rz(roll)
//   where rotation.x = pitch, rotation.y = yaw, rotation.z = roll
static btQuaternion toBtQuat(const glm::vec3& eulerRadians) {
    glm::quat qY = glm::angleAxis(eulerRadians.y, glm::vec3(0, 1, 0));  // Yaw
    glm::quat qX = glm::angleAxis(eulerRadians.x, glm::vec3(1, 0, 0));  // Pitch
    glm::quat qZ = glm::angleAxis(eulerRadians.z, glm::vec3(0, 0, 1));  // Roll
    glm::quat q = qY * qX * qZ;
    return btQuaternion(q.x, q.y, q.z, q.w);
}

// Convert btQuaternion to euler angles matching TransformComponent's Y→X→Z convention.
static glm::vec3 toEuler(const btQuaternion& btq) {
    glm::quat q(btq.w(), btq.x(), btq.y(), btq.z());
    glm::mat3 rotMat = glm::mat3_cast(q);

    // Decompose Y→X→Z rotation matrix:
    //   R = Ry * Rx * Rz
    // pitch = asin(-R[2][1])
    // yaw   = atan2(R[2][0], R[2][2])
    // roll  = atan2(R[0][1], R[1][1])
    float pitch = std::asin(std::clamp(-rotMat[2][1], -1.0f, 1.0f));
    float yaw   = std::atan2(rotMat[2][0], rotMat[2][2]);
    float roll  = std::atan2(rotMat[0][1], rotMat[1][1]);

    return glm::vec3(pitch, yaw, roll);
}

// ═══════════════════════════════════════════════════════════════
// Per-Entity Physics Data — RAII tracking of all Bullet allocations
// ═══════════════════════════════════════════════════════════════

struct PhysicsBodyData {
    btRigidBody*          rigidBody    = nullptr;
    btCollisionShape*     shape        = nullptr;
    btDefaultMotionState* motionState  = nullptr;
    entt::entity          entity       = entt::null;
};

// ═══════════════════════════════════════════════════════════════
// PhysicsSystem::Impl — The hidden Bullet world
// ═══════════════════════════════════════════════════════════════

struct PhysicsSystem::Impl {
    // ─── Bullet World ─────────────────────────────────────────
    std::unique_ptr<btDefaultCollisionConfiguration>       collisionConfig;
    std::unique_ptr<btCollisionDispatcher>                  dispatcher;
    std::unique_ptr<btDbvtBroadphase>                       broadphase;
    std::unique_ptr<btSequentialImpulseConstraintSolver>    solver;
    std::unique_ptr<btDiscreteDynamicsWorld>                 dynamicsWorld;

    // ─── Per-Entity Tracking ──────────────────────────────────
    std::unordered_map<entt::entity, PhysicsBodyData> bodies;

    // ─── Signal Connections ───────────────────────────────────
    entt::connection onRigidBodyConstruct;
    entt::connection onRigidBodyDestroy;

    // ─── Simulation Parameters ────────────────────────────────
    float fixedTimeStep = 1.0f / 60.0f;
    int   maxSubSteps   = 10;

    // ─── Registry Pointer (set during initialize) ─────────────
    entt::registry* registry = nullptr;

    // ─── Internal Methods ─────────────────────────────────────

    void createBodyInternal(entt::registry& reg, entt::entity entity);
    void removeBodyInternal(entt::entity entity);
    btCollisionShape* createCollisionShape(const ColliderComponent& collider);
    void cleanupAllBodies();

    // ─── Signal Callbacks ─────────────────────────────────────

    void onRigidBodyAdded(entt::registry& reg, entt::entity entity) {
        if (reg.all_of<TransformComponent>(entity)) {
            createBodyInternal(reg, entity);
        }
    }

    void onRigidBodyRemoved(entt::registry& reg, entt::entity entity) {
        removeBodyInternal(entity);
    }
};

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {
    priority = 5;  // After TransformSystem(0), before CameraSystem(10)

    // Create the Bullet dynamics world
    m_impl->collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();
    m_impl->dispatcher = std::make_unique<btCollisionDispatcher>(
        m_impl->collisionConfig.get());
    m_impl->broadphase = std::make_unique<btDbvtBroadphase>();
    m_impl->solver = std::make_unique<btSequentialImpulseConstraintSolver>();

    m_impl->dynamicsWorld = std::make_unique<btDiscreteDynamicsWorld>(
        m_impl->dispatcher.get(),
        m_impl->broadphase.get(),
        m_impl->solver.get(),
        m_impl->collisionConfig.get());

    m_impl->dynamicsWorld->setGravity(btVector3(0, -9.81f, 0));

    printf("[PhysicsSystem] Bullet dynamics world created (gravity: 0, -9.81, 0)\n");
}

PhysicsSystem::~PhysicsSystem() {
    // Safety: if cleanup() wasn't called, tear down bodies now
    if (m_impl && m_impl->dynamicsWorld && !m_impl->bodies.empty()) {
        m_impl->cleanupAllBodies();
    }
}

// ═══════════════════════════════════════════════════════════════
// ISystem Interface
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::initialize(entt::registry& registry) {
    m_impl->registry = &registry;

    // Connect EnTT signals for automatic body lifecycle
    m_impl->onRigidBodyConstruct = registry.on_construct<RigidBodyComponent>()
        .connect<&Impl::onRigidBodyAdded>(*m_impl);

    m_impl->onRigidBodyDestroy = registry.on_destroy<RigidBodyComponent>()
        .connect<&Impl::onRigidBodyRemoved>(*m_impl);

    // Create bodies for entities that already exist before initialization
    auto view = registry.view<RigidBodyComponent, TransformComponent>();
    uint32_t preExisting = 0;
    for (auto entity : view) {
        m_impl->createBodyInternal(registry, entity);
        preExisting++;
    }

    if (preExisting > 0) {
        printf("[PhysicsSystem] Created %u bodies for pre-existing entities\n", preExisting);
    }

    printf("[PhysicsSystem] Initialized with EnTT signals connected\n");
}

void PhysicsSystem::update(entt::registry& registry, float deltaTime) {
    if (!enabled) return;

    // ─── ECS → Bullet: Sync kinematic bodies ─────────────────
    for (auto& [entity, bodyData] : m_impl->bodies) {
        if (!registry.valid(entity) || !bodyData.rigidBody) continue;

        auto* rbComp = registry.try_get<RigidBodyComponent>(entity);
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (!rbComp || !transform) continue;

        if (rbComp->type == RigidBodyComponent::Kinematic) {
            btTransform btTrans;
            btTrans.setOrigin(toBt(transform->position));
            btTrans.setRotation(toBtQuat(transform->rotation));
            bodyData.motionState->setWorldTransform(btTrans);
        }
    }

    // ─── Step Simulation ──────────────────────────────────────
    m_impl->dynamicsWorld->stepSimulation(
        deltaTime,
        m_impl->maxSubSteps,
        m_impl->fixedTimeStep);

    // ─── Bullet → ECS: Sync dynamic bodies ───────────────────
    for (auto& [entity, bodyData] : m_impl->bodies) {
        if (!registry.valid(entity) || !bodyData.rigidBody) continue;

        auto* rbComp = registry.try_get<RigidBodyComponent>(entity);
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (!rbComp || !transform) continue;

        if (rbComp->type == RigidBodyComponent::Dynamic) {
            btTransform bulletTransform;
            bodyData.motionState->getWorldTransform(bulletTransform);

            // Position sync
            transform->position = toGlm(bulletTransform.getOrigin());

            // Rotation sync (quaternion → euler, matching Y→X→Z order)
            transform->rotation = toEuler(bulletTransform.getRotation());

            // Velocity sync back to component (for inspection/serialization)
            rbComp->velocity = toGlm(bodyData.rigidBody->getLinearVelocity());
            rbComp->angularVelocity = toGlm(bodyData.rigidBody->getAngularVelocity());

            transform->isDirty = true;
        }
    }
}

void PhysicsSystem::cleanup(entt::registry& registry) {
    // Disconnect signals
    m_impl->onRigidBodyConstruct.release();
    m_impl->onRigidBodyDestroy.release();

    // Remove all tracked bodies
    m_impl->cleanupAllBodies();
    m_impl->registry = nullptr;

    printf("[PhysicsSystem] Cleaned up\n");
}

// ═══════════════════════════════════════════════════════════════
// Force / Impulse API
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::addForce(entt::registry& registry, entt::entity entity,
                             const glm::vec3& force) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->applyCentralForce(toBt(force));
}

void PhysicsSystem::addForceAtPoint(entt::registry& registry, entt::entity entity,
                                     const glm::vec3& force, const glm::vec3& point) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->applyForce(toBt(force), toBt(point));
}

void PhysicsSystem::addImpulse(entt::registry& registry, entt::entity entity,
                               const glm::vec3& impulse) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->applyCentralImpulse(toBt(impulse));
}

void PhysicsSystem::addTorqueImpulse(entt::registry& registry, entt::entity entity,
                                      const glm::vec3& torque) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->applyTorqueImpulse(toBt(torque));
}

void PhysicsSystem::setLinearVelocity(entt::registry& registry, entt::entity entity,
                                       const glm::vec3& velocity) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->setLinearVelocity(toBt(velocity));
}

void PhysicsSystem::setAngularVelocity(entt::registry& registry, entt::entity entity,
                                        const glm::vec3& velocity) {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return;
    it->second.rigidBody->activate(true);
    it->second.rigidBody->setAngularVelocity(toBt(velocity));
}

// ═══════════════════════════════════════════════════════════════
// World Configuration
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::setGravity(const glm::vec3& gravity) {
    m_impl->dynamicsWorld->setGravity(toBt(gravity));
}

glm::vec3 PhysicsSystem::getGravity() const {
    return toGlm(m_impl->dynamicsWorld->getGravity());
}

void PhysicsSystem::setFixedTimeStep(float timeStep) {
    m_impl->fixedTimeStep = timeStep;
}

float PhysicsSystem::getFixedTimeStep() const {
    return m_impl->fixedTimeStep;
}

void PhysicsSystem::setMaxSubSteps(int maxSubSteps) {
    m_impl->maxSubSteps = maxSubSteps;
}

int PhysicsSystem::getMaxSubSteps() const {
    return m_impl->maxSubSteps;
}

// ═══════════════════════════════════════════════════════════════
// Query API
// ═══════════════════════════════════════════════════════════════

glm::vec3 PhysicsSystem::getLinearVelocity(entt::registry& registry,
                                            entt::entity entity) const {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return glm::vec3(0.0f);
    return toGlm(it->second.rigidBody->getLinearVelocity());
}

glm::vec3 PhysicsSystem::getAngularVelocity(entt::registry& registry,
                                              entt::entity entity) const {
    auto it = m_impl->bodies.find(entity);
    if (it == m_impl->bodies.end() || !it->second.rigidBody) return glm::vec3(0.0f);
    return toGlm(it->second.rigidBody->getAngularVelocity());
}

uint32_t PhysicsSystem::getBodyCount() const {
    return static_cast<uint32_t>(m_impl->bodies.size());
}

// ═══════════════════════════════════════════════════════════════
// Manual Body Management
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::createBody(entt::registry& registry, entt::entity entity) {
    m_impl->createBodyInternal(registry, entity);
}

void PhysicsSystem::removeBody(entt::registry& registry, entt::entity entity) {
    m_impl->removeBodyInternal(entity);
}

void PhysicsSystem::rebuildBody(entt::registry& registry, entt::entity entity) {
    m_impl->removeBodyInternal(entity);
    m_impl->createBodyInternal(registry, entity);
}

// ═══════════════════════════════════════════════════════════════
// Impl: createBodyInternal — The heart of body creation
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::Impl::createBodyInternal(entt::registry& reg, entt::entity entity) {
    // Skip if body already exists
    if (bodies.find(entity) != bodies.end()) return;

    auto* rbComp = reg.try_get<RigidBodyComponent>(entity);
    auto* transform = reg.try_get<TransformComponent>(entity);
    if (!rbComp || !transform) return;

    PhysicsBodyData bodyData;
    bodyData.entity = entity;

    // ─── Create Collision Shape ───────────────────────────────
    auto* collider = reg.try_get<ColliderComponent>(entity);
    if (collider) {
        bodyData.shape = createCollisionShape(*collider);
        collider->bulletShape = bodyData.shape;
    } else {
        // Default: unit box (half-extents 0.5)
        bodyData.shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
    }

    // ─── Create Motion State (with full position + rotation) ──
    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(toBt(transform->position));
    startTransform.setRotation(toBtQuat(transform->rotation));

    bodyData.motionState = new btDefaultMotionState(startTransform);

    // ─── Calculate Mass and Inertia ───────────────────────────
    btScalar mass = 0.0f;
    btVector3 localInertia(0, 0, 0);

    if (rbComp->type == RigidBodyComponent::Dynamic) {
        mass = rbComp->mass;
        if (mass > 0.0f) {
            bodyData.shape->calculateLocalInertia(mass, localInertia);
        }
    }
    // Static and Kinematic bodies have mass = 0

    // ─── Create Rigid Body ────────────────────────────────────
    btRigidBody::btRigidBodyConstructionInfo rbInfo(
        mass, bodyData.motionState, bodyData.shape, localInertia);

    // Physics material properties
    rbInfo.m_friction = collider ? collider->friction : 0.5f;
    rbInfo.m_restitution = collider ? collider->restitution : 0.0f;
    rbInfo.m_linearDamping = rbComp->drag;
    rbInfo.m_angularDamping = rbComp->angularDrag;

    bodyData.rigidBody = new btRigidBody(rbInfo);

    // ─── Configure Body Flags ─────────────────────────────────
    if (rbComp->type == RigidBodyComponent::Static) {
        bodyData.rigidBody->setCollisionFlags(
            bodyData.rigidBody->getCollisionFlags() |
            btCollisionObject::CF_STATIC_OBJECT);
    }

    if (rbComp->type == RigidBodyComponent::Kinematic) {
        bodyData.rigidBody->setCollisionFlags(
            bodyData.rigidBody->getCollisionFlags() |
            btCollisionObject::CF_KINEMATIC_OBJECT);
        bodyData.rigidBody->setActivationState(DISABLE_DEACTIVATION);
    }

    if (rbComp->freezeRotation) {
        bodyData.rigidBody->setAngularFactor(btVector3(0, 0, 0));
    }

    if (!rbComp->useGravity) {
        bodyData.rigidBody->setGravity(btVector3(0, 0, 0));
    }

    // Set initial velocities
    if (rbComp->type == RigidBodyComponent::Dynamic) {
        bodyData.rigidBody->setLinearVelocity(toBt(rbComp->velocity));
        bodyData.rigidBody->setAngularVelocity(toBt(rbComp->angularVelocity));
    }

    // Handle triggers
    if (collider && collider->isTrigger) {
        bodyData.rigidBody->setCollisionFlags(
            bodyData.rigidBody->getCollisionFlags() |
            btCollisionObject::CF_NO_CONTACT_RESPONSE);
    }

    // Store entity in user pointer for future collision callback use
    bodyData.rigidBody->setUserIndex(static_cast<int>(
        static_cast<uint32_t>(entity)));

    // ─── Add to World ─────────────────────────────────────────
    dynamicsWorld->addRigidBody(bodyData.rigidBody);
    rbComp->bulletRigidBody = bodyData.rigidBody;

    bodies[entity] = bodyData;
}

// ═══════════════════════════════════════════════════════════════
// Impl: removeBodyInternal — Clean RAII teardown
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::Impl::removeBodyInternal(entt::entity entity) {
    auto it = bodies.find(entity);
    if (it == bodies.end()) return;

    auto& bodyData = it->second;

    // Remove from dynamics world first
    if (bodyData.rigidBody && dynamicsWorld) {
        dynamicsWorld->removeRigidBody(bodyData.rigidBody);
    }

    // Delete in reverse creation order
    delete bodyData.rigidBody;
    delete bodyData.motionState;
    delete bodyData.shape;

    // Clear component pointers if entity is still valid
    if (registry && registry->valid(entity)) {
        if (auto* rb = registry->try_get<RigidBodyComponent>(entity)) {
            rb->bulletRigidBody = nullptr;
        }
        if (auto* col = registry->try_get<ColliderComponent>(entity)) {
            col->bulletShape = nullptr;
        }
    }

    bodies.erase(it);
}

// ═══════════════════════════════════════════════════════════════
// Impl: createCollisionShape
// ═══════════════════════════════════════════════════════════════

btCollisionShape* PhysicsSystem::Impl::createCollisionShape(
    const ColliderComponent& collider)
{
    switch (collider.shape) {
        case ColliderComponent::Box:
            // ColliderComponent::size = full extents, Bullet wants half-extents
            return new btBoxShape(btVector3(
                collider.size.x * 0.5f,
                collider.size.y * 0.5f,
                collider.size.z * 0.5f));

        case ColliderComponent::Sphere:
            // size.x = radius
            return new btSphereShape(collider.size.x);

        case ColliderComponent::Capsule:
            // size.x = radius, size.y = height
            return new btCapsuleShape(collider.size.x, collider.size.y);

        case ColliderComponent::Plane:
            // Infinite static plane (normal = up, offset = 0)
            return new btStaticPlaneShape(btVector3(0, 1, 0), 0);

        case ColliderComponent::Mesh:
            // TODO: Triangle mesh collider from MeshComponent
            printf("[PhysicsSystem] Warning: Mesh collider not yet implemented, using box fallback\n");
            return new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));

        default:
            return new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
    }
}

// ═══════════════════════════════════════════════════════════════
// Impl: cleanupAllBodies
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::Impl::cleanupAllBodies() {
    // Collect keys first (removeBodyInternal modifies the map)
    std::vector<entt::entity> toRemove;
    toRemove.reserve(bodies.size());
    for (auto& [entity, _] : bodies) {
        toRemove.push_back(entity);
    }
    for (auto entity : toRemove) {
        removeBodyInternal(entity);
    }
}

} // namespace ECS
} // namespace Shoonyakasha
