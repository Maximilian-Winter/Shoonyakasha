//
// PhysicsSystem.cpp - Jolt Physics Integration
//
// 重力之道 — The Way of Gravity
//
// All Jolt types for the ECS live here; the header hides them behind PIMPL.
//
// Bodies: a RigidBodyComponent's type is Jolt's motion type (a dynamic body
// of mass 0 or less is static); its mass, drag and angular drag are the
// body's mass and damping, useGravity its gravity factor (1 or 0) and
// freezeRotation leaves it only its translations. A ColliderComponent's
// shape is a box (size: full extents), a sphere (size.x: radius), a capsule
// along Y (size.x: radius, size.y: its cylinder's length) or an infinite
// plane facing +Y through the origin; a mesh collider is a unit box. Without
// a collider the body is a unit box with friction 0.5 and restitution 0.
// A trigger is a sensor. Two bodies' friction and restitution combine by
// multiplying.
//
// Stepping: the frame's time accrues, and whole fixed steps are taken, at
// most maxSubSteps a frame; time beyond that is dropped and less than a
// step carries over (maxSubSteps 0: one step of the frame's time). Before
// the first step a kinematic body is moved to its transform over that step;
// each step applies the forces given since the frame began. After the steps
// a dynamic body's transform is its position and rotation carried on by its
// velocities over the time carried over, and its component's velocities are
// copied from the body.
//

#include "ECS/PhysicsSystem.h"

#include "../Physics/JoltRuntime.h"

// Jolt.h comes first: it sets up the macros every other Jolt header uses.
#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Shoonyakasha {
namespace ECS {

namespace {

constexpr JPH::uint MAX_BODIES = 65536;
constexpr JPH::uint MAX_BODY_PAIRS = 65536;
constexpr JPH::uint MAX_CONTACT_CONSTRAINTS = 16384;
constexpr size_t TEMP_ALLOCATOR_BYTES = 16 * 1024 * 1024;
constexpr float PLANE_HALF_EXTENT = 10000.0f;   // m: the plane's bounds for the broad phase

// Object layers: what does not move, and what does; one broad phase layer each.
namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
} // namespace Layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(layer == Layers::MOVING ? 1 : 0);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == JPH::BroadPhaseLayer(1) ? "MOVING" : "NON_MOVING";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        return layer == Layers::MOVING || broadPhase == JPH::BroadPhaseLayer(1);
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::MOVING || b == Layers::MOVING;
    }
};

float multiplyFriction(const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
    return a.GetFriction() * b.GetFriction();
}

float multiplyRestitution(const JPH::Body& a, const JPH::SubShapeID&, const JPH::Body& b, const JPH::SubShapeID&) {
    return a.GetRestitution() * b.GetRestitution();
}

JPH::Vec3 toJolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }

glm::vec3 toGlm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

// TransformComponent's Euler angles (Y→X→Z order) as a quaternion, matching
// TransformComponent::getLocalMatrix(): R = Ry(yaw) * Rx(pitch) * Rz(roll),
// where rotation.x = pitch, rotation.y = yaw, rotation.z = roll.
JPH::Quat toJoltQuat(const glm::vec3& eulerRadians) {
    glm::quat qY = glm::angleAxis(eulerRadians.y, glm::vec3(0, 1, 0));  // Yaw
    glm::quat qX = glm::angleAxis(eulerRadians.x, glm::vec3(1, 0, 0));  // Pitch
    glm::quat qZ = glm::angleAxis(eulerRadians.z, glm::vec3(0, 0, 1));  // Roll
    glm::quat q = qY * qX * qZ;
    return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
}

// A quaternion as Euler angles in TransformComponent's Y→X→Z convention.
glm::vec3 toEuler(JPH::QuatArg jq) {
    glm::quat q(jq.GetW(), jq.GetX(), jq.GetY(), jq.GetZ());
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

} // namespace

// ═══════════════════════════════════════════════════════════════
// Per-Entity Physics Data
// ═══════════════════════════════════════════════════════════════

struct PhysicsBodyData {
    JPH::BodyID body;
    RigidBodyComponent::Type type = RigidBodyComponent::Dynamic;
    glm::vec3 force{0.0f};                       // the frame's central forces, N
    std::vector<std::pair<glm::vec3, glm::vec3>> pointForces;   // (force, world point), this frame
};

// ═══════════════════════════════════════════════════════════════
// PhysicsSystem::Impl — The hidden Jolt world
// ═══════════════════════════════════════════════════════════════

struct PhysicsSystem::Impl {
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    JPH::PhysicsSystem system;

    // ─── Per-Entity Tracking ──────────────────────────────────
    std::unordered_map<entt::entity, PhysicsBodyData> bodies;

    // ─── Signal Connections ───────────────────────────────────
    entt::connection onRigidBodyConstruct;
    entt::connection onRigidBodyDestroy;

    // ─── Simulation Parameters ────────────────────────────────
    float fixedTimeStep = 1.0f / 60.0f;
    int   maxSubSteps   = 10;
    float carried       = 0.0f;   // s of the frames' time not yet stepped

    entt::registry* registry = nullptr;

    JPH::BodyInterface& bodyInterface() { return system.GetBodyInterface(); }

    void createBodyInternal(entt::registry& reg, entt::entity entity);
    void removeBodyInternal(entt::entity entity);
    void cleanupAllBodies();
    void step(float dt, bool first, entt::registry& reg);

    PhysicsBodyData* find(entt::entity entity) {
        auto it = bodies.find(entity);
        return it == bodies.end() ? nullptr : &it->second;
    }

    void onRigidBodyAdded(entt::registry& reg, entt::entity entity) {
        if (reg.all_of<TransformComponent>(entity)) {
            createBodyInternal(reg, entity);
        }
    }

    void onRigidBodyRemoved(entt::registry& /*reg*/, entt::entity entity) {
        removeBodyInternal(entity);
    }
};

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {
    priority = 5;  // After TransformSystem(0), before CameraSystem(10)

    Physics::initialiseJolt();
    const int hardware = static_cast<int>(std::thread::hardware_concurrency());
    const int threads = std::clamp(hardware - 1, 1, 4);
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(TEMP_ALLOCATOR_BYTES);
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                                   threads);
    m_impl->system.Init(MAX_BODIES, 0, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, m_impl->broadPhaseLayers,
                        m_impl->objectVsBroadPhase, m_impl->objectPairs);
    m_impl->system.SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
    m_impl->system.SetCombineFriction(multiplyFriction);
    m_impl->system.SetCombineRestitution(multiplyRestitution);

    printf("[PhysicsSystem] Jolt physics world created (gravity: 0, -9.81, 0)\n");
}

PhysicsSystem::~PhysicsSystem() {
    if (m_impl && !m_impl->bodies.empty()) {
        m_impl->cleanupAllBodies();
    }
}

// ═══════════════════════════════════════════════════════════════
// ISystem Interface
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::initialize(entt::registry& registry) {
    m_impl->registry = &registry;

    m_impl->onRigidBodyConstruct = registry.on_construct<RigidBodyComponent>()
        .connect<&Impl::onRigidBodyAdded>(*m_impl);

    m_impl->onRigidBodyDestroy = registry.on_destroy<RigidBodyComponent>()
        .connect<&Impl::onRigidBodyRemoved>(*m_impl);

    // Bodies for entities that already exist before initialization
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

void PhysicsSystem::Impl::step(float dt, bool first, entt::registry& reg) {
    JPH::BodyInterface& bi = bodyInterface();
    for (auto& [entity, data] : bodies) {
        if (data.type == RigidBodyComponent::Kinematic && first) {
            if (auto* transform = reg.try_get<TransformComponent>(entity)) {
                bi.MoveKinematic(data.body, JPH::RVec3(toJolt(transform->position)), toJoltQuat(transform->rotation),
                                 dt);
            }
        }
        if (data.type != RigidBodyComponent::Dynamic) continue;
        if (data.force != glm::vec3(0.0f)) bi.AddForce(data.body, toJolt(data.force));
        for (const auto& [force, point] : data.pointForces) {
            bi.AddForce(data.body, toJolt(force), JPH::RVec3(toJolt(point)));
        }
    }
    system.Update(dt, 1, tempAllocator.get(), jobSystem.get());
}

void PhysicsSystem::update(entt::registry& registry, float deltaTime) {
    if (!enabled) return;
    Impl& impl = *m_impl;

    // ─── Fixed steps ──────────────────────────────────────────
    float extra = 0.0f;   // s the dynamic bodies' transforms are carried on past the last step
    if (impl.maxSubSteps <= 0) {
        if (deltaTime > 0.0f) impl.step(deltaTime, true, registry);
    } else {
        impl.carried += deltaTime;
        int steps = static_cast<int>(impl.carried / impl.fixedTimeStep);
        impl.carried -= static_cast<float>(steps) * impl.fixedTimeStep;
        steps = std::min(steps, impl.maxSubSteps);
        for (int k = 0; k < steps; ++k) impl.step(impl.fixedTimeStep, k == 0, registry);
        extra = impl.carried;
    }
    for (auto& [entity, data] : impl.bodies) {
        data.force = glm::vec3(0.0f);
        data.pointForces.clear();
    }

    // ─── Jolt → ECS: dynamic bodies ───────────────────────────
    JPH::BodyInterface& bi = impl.bodyInterface();
    for (auto& [entity, data] : impl.bodies) {
        if (data.type != RigidBodyComponent::Dynamic || !registry.valid(entity)) continue;
        auto* rbComp = registry.try_get<RigidBodyComponent>(entity);
        auto* transform = registry.try_get<TransformComponent>(entity);
        if (!rbComp || !transform) continue;

        JPH::RVec3 position;
        JPH::Quat rotation;
        bi.GetPositionAndRotation(data.body, position, rotation);
        const JPH::Vec3 v = bi.GetLinearVelocity(data.body);
        const JPH::Vec3 w = bi.GetAngularVelocity(data.body);
        if (extra > 0.0f && bi.IsActive(data.body)) {
            position += v * extra;
            const float angle = w.Length() * extra;
            if (angle > 1e-9f) rotation = (JPH::Quat::sRotation(w.Normalized(), angle) * rotation).Normalized();
        }
        transform->position = toGlm(JPH::Vec3(position));
        transform->rotation = toEuler(rotation);
        rbComp->velocity = toGlm(v);
        rbComp->angularVelocity = toGlm(w);
        transform->isDirty = true;
    }
}

void PhysicsSystem::cleanup(entt::registry& /*registry*/) {
    m_impl->onRigidBodyConstruct.release();
    m_impl->onRigidBodyDestroy.release();
    m_impl->cleanupAllBodies();
    m_impl->registry = nullptr;

    printf("[PhysicsSystem] Cleaned up\n");
}

// ═══════════════════════════════════════════════════════════════
// Force / Impulse API (dynamic bodies only; others are left alone)
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::addForce(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& force) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type != RigidBodyComponent::Dynamic) return;
    data->force += force;
    m_impl->bodyInterface().ActivateBody(data->body);
}

void PhysicsSystem::addForceAtPoint(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& force,
                                    const glm::vec3& point) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type != RigidBodyComponent::Dynamic) return;
    data->pointForces.emplace_back(force, point);
    m_impl->bodyInterface().ActivateBody(data->body);
}

void PhysicsSystem::addImpulse(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& impulse) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type != RigidBodyComponent::Dynamic) return;
    m_impl->bodyInterface().AddImpulse(data->body, toJolt(impulse));
}

void PhysicsSystem::addTorqueImpulse(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& torque) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type != RigidBodyComponent::Dynamic) return;
    m_impl->bodyInterface().AddAngularImpulse(data->body, toJolt(torque));
}

void PhysicsSystem::setLinearVelocity(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& velocity) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type == RigidBodyComponent::Static) return;
    m_impl->bodyInterface().SetLinearVelocity(data->body, toJolt(velocity));
}

void PhysicsSystem::setAngularVelocity(entt::registry& /*registry*/, entt::entity entity, const glm::vec3& velocity) {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type == RigidBodyComponent::Static) return;
    m_impl->bodyInterface().SetAngularVelocity(data->body, toJolt(velocity));
}

// ═══════════════════════════════════════════════════════════════
// World Configuration
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::setGravity(const glm::vec3& gravity) {
    m_impl->system.SetGravity(toJolt(gravity));
}

glm::vec3 PhysicsSystem::getGravity() const {
    return toGlm(m_impl->system.GetGravity());
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

glm::vec3 PhysicsSystem::getLinearVelocity(entt::registry& /*registry*/, entt::entity entity) const {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type == RigidBodyComponent::Static) return glm::vec3(0.0f);
    return toGlm(m_impl->bodyInterface().GetLinearVelocity(data->body));
}

glm::vec3 PhysicsSystem::getAngularVelocity(entt::registry& /*registry*/, entt::entity entity) const {
    PhysicsBodyData* data = m_impl->find(entity);
    if (!data || data->type == RigidBodyComponent::Static) return glm::vec3(0.0f);
    return toGlm(m_impl->bodyInterface().GetAngularVelocity(data->body));
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

void PhysicsSystem::removeBody(entt::registry& /*registry*/, entt::entity entity) {
    m_impl->removeBodyInternal(entity);
}

void PhysicsSystem::rebuildBody(entt::registry& registry, entt::entity entity) {
    m_impl->removeBodyInternal(entity);
    m_impl->createBodyInternal(registry, entity);
}

// ═══════════════════════════════════════════════════════════════
// Impl: createBodyInternal
// ═══════════════════════════════════════════════════════════════

namespace {

JPH::ShapeRefC unitBox() {
    return new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
}

JPH::ShapeRefC createShape(const ColliderComponent& collider) {
    switch (collider.shape) {
        case ColliderComponent::Box: {
            // ColliderComponent::size = full extents, Jolt wants half-extents
            const JPH::Vec3 half = toJolt(collider.size * 0.5f);
            const float radius = std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin());
            return new JPH::BoxShape(half, radius);
        }
        case ColliderComponent::Sphere:
            return new JPH::SphereShape(collider.size.x);
        case ColliderComponent::Capsule:
            // size.x = radius, size.y = the cylinder's length
            return new JPH::CapsuleShape(0.5f * collider.size.y, collider.size.x);
        case ColliderComponent::Plane:
            return new JPH::PlaneShape(JPH::Plane(JPH::Vec3(0.0f, 1.0f, 0.0f), 0.0f), nullptr, PLANE_HALF_EXTENT);
        case ColliderComponent::Mesh:
            printf("[PhysicsSystem] Warning: Mesh collider not yet implemented, using box fallback\n");
            return unitBox();
        default:
            return unitBox();
    }
}

} // namespace

void PhysicsSystem::Impl::createBodyInternal(entt::registry& reg, entt::entity entity) {
    if (bodies.find(entity) != bodies.end()) return;

    auto* rbComp = reg.try_get<RigidBodyComponent>(entity);
    auto* transform = reg.try_get<TransformComponent>(entity);
    if (!rbComp || !transform) return;

    auto* collider = reg.try_get<ColliderComponent>(entity);
    JPH::ShapeRefC shape = collider ? createShape(*collider) : unitBox();

    RigidBodyComponent::Type type = rbComp->type;
    if (type == RigidBodyComponent::Dynamic && rbComp->mass <= 0.0f) type = RigidBodyComponent::Static;
    if (collider && collider->shape == ColliderComponent::Plane) type = RigidBodyComponent::Static;
    const JPH::EMotionType motion = type == RigidBodyComponent::Static ? JPH::EMotionType::Static
        : type == RigidBodyComponent::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    const JPH::ObjectLayer layer = type == RigidBodyComponent::Static ? Layers::NON_MOVING : Layers::MOVING;

    JPH::BodyCreationSettings settings(shape, JPH::RVec3(toJolt(transform->position)),
                                       toJoltQuat(transform->rotation), motion, layer);
    settings.mFriction = collider ? collider->friction : 0.5f;
    settings.mRestitution = collider ? collider->restitution : 0.0f;
    settings.mLinearDamping = rbComp->drag;
    settings.mAngularDamping = rbComp->angularDrag;
    settings.mGravityFactor = rbComp->useGravity ? 1.0f : 0.0f;
    settings.mIsSensor = collider && collider->isTrigger;
    if (type == RigidBodyComponent::Dynamic) {
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        settings.mMassPropertiesOverride.mMass = rbComp->mass;
        if (rbComp->freezeRotation) settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
            JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ;
        settings.mLinearVelocity = toJolt(rbComp->velocity);
        settings.mAngularVelocity = rbComp->freezeRotation ? JPH::Vec3::sZero() : toJolt(rbComp->angularVelocity);
    }
    settings.mUserData = static_cast<JPH::uint64>(static_cast<uint32_t>(entity));

    JPH::BodyInterface& bi = bodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, type == RigidBodyComponent::Static
                                                             ? JPH::EActivation::DontActivate
                                                             : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
        printf("[PhysicsSystem] Warning: no room for another body (%u at most)\n", MAX_BODIES);
        return;
    }
    PhysicsBodyData data;
    data.body = id;
    data.type = type;
    bodies[entity] = std::move(data);
    rbComp->physicsBody = id.GetIndexAndSequenceNumber();
}

// ═══════════════════════════════════════════════════════════════
// Impl: removeBodyInternal
// ═══════════════════════════════════════════════════════════════

void PhysicsSystem::Impl::removeBodyInternal(entt::entity entity) {
    auto it = bodies.find(entity);
    if (it == bodies.end()) return;

    JPH::BodyInterface& bi = bodyInterface();
    bi.RemoveBody(it->second.body);
    bi.DestroyBody(it->second.body);

    if (registry && registry->valid(entity)) {
        if (auto* rb = registry->try_get<RigidBodyComponent>(entity)) {
            rb->physicsBody = RigidBodyComponent::NO_BODY;
        }
    }

    bodies.erase(it);
}

void PhysicsSystem::Impl::cleanupAllBodies() {
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
