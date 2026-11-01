#include "Planet/PlanetPhysics.h"

// Jolt.h comes first: it sets up the macros every other Jolt header uses.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Shoonyakasha::Planet {

namespace {

/// Object layers: static bodies, the fine and the coarse ground meshes,
/// and moving bodies near (on the fine ground) and far (on the coarse).
/// Two broad phase layers: what does not move, and what does.
namespace Layers {
constexpr JPH::ObjectLayer NON_MOVING = 0;
constexpr JPH::ObjectLayer MOVING = 1;
constexpr JPH::ObjectLayer GROUND = 2;
constexpr JPH::ObjectLayer COARSE_GROUND = 3;
constexpr JPH::ObjectLayer FAR = 4;
constexpr JPH::uint BROAD_PHASE_COUNT = 2;

bool moving(JPH::ObjectLayer layer) {
    return layer == MOVING || layer == FAR;
}
}  // namespace Layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return Layers::BROAD_PHASE_COUNT; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(Layers::moving(layer) ? 1 : 0);
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "NON_MOVING" : "MOVING";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override {
        return Layers::moving(layer) || broadPhase.GetValue() == 1;
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (!Layers::moving(a) && !Layers::moving(b)) {
            return false;
        }
        return !((a == Layers::MOVING && b == Layers::COARSE_GROUND) || (b == Layers::MOVING && a == Layers::COARSE_GROUND) ||
                 (a == Layers::FAR && b == Layers::GROUND) || (b == Layers::FAR && a == Layers::GROUND));
    }
};

void initialiseJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

JPH::Vec3 toVec3(const PhysicsVec3& v) {
    return JPH::Vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
}

JPH::RVec3 toRVec3(const PhysicsVec3& v) {
    return JPH::RVec3(static_cast<JPH::Real>(v[0]), static_cast<JPH::Real>(v[1]), static_cast<JPH::Real>(v[2]));
}

JPH::Quat toQuat(const PhysicsQuat& q) {
    return JPH::Quat(static_cast<float>(q[1]), static_cast<float>(q[2]), static_cast<float>(q[3]),
                     static_cast<float>(q[0])).Normalized();
}

template <typename V>
PhysicsVec3 fromVec3(const V& v) {
    return {static_cast<double>(v.GetX()), static_cast<double>(v.GetY()), static_cast<double>(v.GetZ())};
}

PhysicsQuat fromQuat(const JPH::Quat& q) {
    return {static_cast<double>(q.GetW()), static_cast<double>(q.GetX()), static_cast<double>(q.GetY()),
            static_cast<double>(q.GetZ())};
}

JPH::EMotionType toMotionType(PhysicsMotion motion) {
    switch (motion) {
        case PhysicsMotion::Static: return JPH::EMotionType::Static;
        case PhysicsMotion::Kinematic: return JPH::EMotionType::Kinematic;
        case PhysicsMotion::Dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

PhysicsMotion fromMotionType(JPH::EMotionType motion) {
    switch (motion) {
        case JPH::EMotionType::Static: return PhysicsMotion::Static;
        case JPH::EMotionType::Kinematic: return PhysicsMotion::Kinematic;
        case JPH::EMotionType::Dynamic: return PhysicsMotion::Dynamic;
    }
    return PhysicsMotion::Static;
}

constexpr JPH::uint MAX_BODY_PAIRS = 65536;
constexpr JPH::uint MAX_CONTACT_CONSTRAINTS = 16384;
constexpr size_t TEMP_ALLOCATOR_BYTES = 16 * 1024 * 1024;
constexpr float STEP_UP = 0.4f;            // m a character steps up onto
constexpr float STICK_DOWN = 0.5f;         // m a character on ground is kept on the floor below it

JPH::Quat upRotation(JPH::Vec3Arg up) {
    return JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), up.Normalized());
}

/// Bodies in the same collision group pass through each other: a
/// character's inner body is in the group of the character's id, and so is
/// every body set to pass through that character.
class CharacterGroups final : public JPH::GroupFilter {
public:
    bool CanCollide(const JPH::CollisionGroup& a, const JPH::CollisionGroup& b) const override {
        return a.GetGroupID() == JPH::CollisionGroup::cInvalidGroup || a.GetGroupID() != b.GetGroupID();
    }
};

constexpr JPH::uint64 WATCHED = 1;     // a body's user data: strikes on it are reported

/// Reports the watched bodies a dynamic body strikes with at least
/// `momentum` along the contact's normal, from the physics' job threads.
class Strikes final : public JPH::ContactListener {
public:
    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override {
        check(a, b, manifold);
        check(b, a, manifold);
    }

    /// The bodies struck since the last call, each once in order of their
    /// ids, with the fastest speed and largest momentum of their strikes.
    std::vector<PhysicsStrike> take() {
        std::vector<PhysicsStrike> struck;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            struck.swap(m_struck);
        }
        std::sort(struck.begin(), struck.end(),
                  [](const PhysicsStrike& x, const PhysicsStrike& y) { return x.body < y.body; });
        std::vector<PhysicsStrike> merged;
        for (const PhysicsStrike& s : struck) {
            if (!merged.empty() && merged.back().body == s.body) {
                merged.back().speed = std::max(merged.back().speed, s.speed);
                merged.back().momentum = std::max(merged.back().momentum, s.momentum);
            } else {
                merged.push_back(s);
            }
        }
        return merged;
    }

    float momentum = 300.0f;

private:
    void check(const JPH::Body& watched, const JPH::Body& other, const JPH::ContactManifold& manifold) {
        if (watched.GetUserData() != WATCHED || !other.IsDynamic() || manifold.mRelativeContactPointsOn1.empty()) {
            return;
        }
        JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
        JPH::Vec3 relative = other.GetPointVelocity(point);
        if (!watched.IsStatic()) {
            relative -= watched.GetPointVelocity(point);
        }
        float speed = std::abs(relative.Dot(manifold.mWorldSpaceNormal));
        float inverse = other.GetMotionProperties()->GetInverseMass();
        if (inverse <= 0.0f || speed / inverse < momentum) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_struck.push_back({watched.GetID().GetIndexAndSequenceNumber(), speed, speed / inverse});
    }

    std::mutex m_mutex;
    std::vector<PhysicsStrike> m_struck;
};

/// Every body but the character's inner body and the bodies in its group.
class SkipCharacter final : public JPH::BodyFilter {
public:
    SkipCharacter(JPH::BodyID inner, JPH::CollisionGroup::GroupID group) : m_inner(inner), m_group(group) {}

    bool ShouldCollide(const JPH::BodyID& body) const override { return body != m_inner; }

    bool ShouldCollideLocked(const JPH::Body& body) const override {
        return body.GetCollisionGroup().GetGroupID() != m_group;
    }

private:
    JPH::BodyID m_inner;
    JPH::CollisionGroup::GroupID m_group;
};

}  // namespace

struct PlanetPhysics::Impl {
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectPairs objectPairs;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem;
    JPH::PhysicsSystem system;
    JPH::BodyIDVector scratch;
    std::vector<JPH::Ref<JPH::CharacterVirtual>> characters;   // by id; null once removed
    JPH::Ref<CharacterGroups> characterGroups;
    Strikes strikes;

    void setGroup(JPH::BodyID body, JPH::CollisionGroup group) {
        JPH::BodyLockWrite lock(system.GetBodyLockInterface(), body);
        if (lock.Succeeded()) {
            lock.GetBody().SetCollisionGroup(group);
        }
    }

    JPH::CharacterVirtual& character(uint32_t id) const {
        if (id >= characters.size() || characters[id] == nullptr) {
            throw std::invalid_argument("no character " + std::to_string(id));
        }
        return *characters[id];
    }

    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }

    JPH::BodyID added(uint32_t id) const {
        JPH::BodyID body(id);
        if (!system.GetBodyInterface().IsAdded(body)) {
            throw std::invalid_argument("no body " + std::to_string(id));
        }
        return body;
    }

    uint32_t add(JPH::BodyCreationSettings& settings) {
        JPH::Body* body = bodies().CreateBody(settings);
        if (body == nullptr) {
            throw std::runtime_error("PlanetPhysics: the world holds no more bodies");
        }
        bool moving = settings.mMotionType != JPH::EMotionType::Static;
        bodies().AddBody(body->GetID(), moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        return body->GetID().GetIndexAndSequenceNumber();
    }

    uint32_t addSolid(const JPH::ShapeSettings& shape, const PhysicsVec3& position, const PhysicsQuat& rotation,
                      PhysicsMotion motion, double mass, double friction, double restitution) {
        JPH::ShapeSettings::ShapeResult result = shape.Create();
        if (result.HasError()) {
            throw std::invalid_argument(std::string("PlanetPhysics: ") + result.GetError().c_str());
        }
        JPH::BodyCreationSettings settings(result.Get(), toRVec3(position), toQuat(rotation), toMotionType(motion),
                                           motion == PhysicsMotion::Static ? Layers::NON_MOVING : Layers::MOVING);
        settings.mAllowDynamicOrKinematic = true;
        settings.mFriction = static_cast<float>(friction);
        settings.mRestitution = static_cast<float>(restitution);
        if (mass > 0.0) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = static_cast<float>(mass);
        }
        return add(settings);
    }
};

PlanetPhysics::PlanetPhysics(uint32_t maxBodies, int threads) : m_impl(std::make_unique<Impl>()) {
    initialiseJolt();
    m_impl->characterGroups = new CharacterGroups();
    if (threads < 0) {
        int hardware = static_cast<int>(std::thread::hardware_concurrency());
        threads = std::clamp(hardware - 1, 1, 4);
    }
    m_impl->tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(TEMP_ALLOCATOR_BYTES);
    m_impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                                                   threads);
    m_impl->system.Init(maxBodies, 0, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS, m_impl->broadPhaseLayers,
                        m_impl->objectVsBroadPhase, m_impl->objectPairs);
    m_impl->system.SetGravity(JPH::Vec3::sZero());
    m_impl->system.SetContactListener(&m_impl->strikes);
}

PlanetPhysics::~PlanetPhysics() {
    m_impl->characters.clear();
    JPH::BodyIDVector all;
    m_impl->system.GetBodies(all);
    if (!all.empty()) {
        JPH::BodyInterface& bodies = m_impl->bodies();
        bodies.RemoveBodies(all.data(), static_cast<int>(all.size()));
        bodies.DestroyBodies(all.data(), static_cast<int>(all.size()));
    }
}

uint32_t PlanetPhysics::createBox(const PhysicsVec3& halfExtents, const PhysicsVec3& position,
                                  const PhysicsQuat& rotation, PhysicsMotion motion, double density, double mass,
                                  double friction, double restitution) {
    JPH::Vec3 half = toVec3(halfExtents);
    float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin());
    JPH::BoxShapeSettings shape(half, convex);
    shape.SetDensity(static_cast<float>(density));
    return m_impl->addSolid(shape, position, rotation, motion, mass, friction, restitution);
}

uint32_t PlanetPhysics::createSphere(double radius, const PhysicsVec3& position, PhysicsMotion motion,
                                     double density, double mass, double friction, double restitution) {
    JPH::SphereShapeSettings shape(static_cast<float>(radius));
    shape.SetDensity(static_cast<float>(density));
    return m_impl->addSolid(shape, position, {1.0, 0.0, 0.0, 0.0}, motion, mass, friction, restitution);
}

uint32_t PlanetPhysics::createCapsule(double radius, double halfHeight, const PhysicsVec3& position,
                                      const PhysicsQuat& rotation, PhysicsMotion motion, double density,
                                      double mass, double friction, double restitution) {
    JPH::CapsuleShapeSettings shape(static_cast<float>(halfHeight), static_cast<float>(radius));
    shape.SetDensity(static_cast<float>(density));
    return m_impl->addSolid(shape, position, rotation, motion, mass, friction, restitution);
}

uint32_t PlanetPhysics::createGridMesh(const float* points, uint32_t columns, uint32_t rows,
                                       const PhysicsVec3& position, double friction, bool coarse) {
    if (columns < 2 || rows < 2) {
        throw std::invalid_argument("PlanetPhysics: a grid mesh needs at least 2 x 2 points");
    }
    JPH::VertexList vertices;
    vertices.reserve(static_cast<size_t>(columns) * rows);
    for (size_t k = 0; k < static_cast<size_t>(columns) * rows; ++k) {
        vertices.push_back(JPH::Float3(points[3 * k], points[3 * k + 1], points[3 * k + 2]));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(static_cast<size_t>(columns - 1) * (rows - 1) * 2);
    for (uint32_t j = 0; j + 1 < rows; ++j) {
        for (uint32_t i = 0; i + 1 < columns; ++i) {
            uint32_t k = j * columns + i;
            triangles.push_back(JPH::IndexedTriangle(k, k + 1, k + columns));
            triangles.push_back(JPH::IndexedTriangle(k + 1, k + columns + 1, k + columns));
        }
    }
    JPH::MeshShapeSettings shape(std::move(vertices), std::move(triangles));
    JPH::ShapeSettings::ShapeResult result = shape.Create();
    if (result.HasError()) {
        throw std::invalid_argument(std::string("PlanetPhysics: ") + result.GetError().c_str());
    }
    JPH::BodyCreationSettings settings(result.Get(), toRVec3(position), JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, coarse ? Layers::COARSE_GROUND : Layers::GROUND);
    settings.mFriction = static_cast<float>(friction);
    return m_impl->add(settings);
}

uint32_t PlanetPhysics::createConvexHull(const float* points, size_t count, const PhysicsVec3& position,
                                         const PhysicsQuat& rotation, PhysicsMotion motion, double density,
                                         double mass, double friction, double restitution) {
    JPH::Array<JPH::Vec3> hull;
    hull.reserve(count);
    for (size_t k = 0; k < count; ++k) {
        hull.push_back(JPH::Vec3(points[3 * k], points[3 * k + 1], points[3 * k + 2]));
    }
    JPH::ConvexHullShapeSettings shape(hull);
    shape.SetDensity(static_cast<float>(density));
    return m_impl->addSolid(shape, position, rotation, motion, mass, friction, restitution);
}

void PlanetPhysics::remove(uint32_t body) {
    JPH::BodyID id = m_impl->added(body);
    m_impl->bodies().RemoveBody(id);
    m_impl->bodies().DestroyBody(id);
}

bool PlanetPhysics::contains(uint32_t body) const {
    return m_impl->bodies().IsAdded(JPH::BodyID(body));
}

void PlanetPhysics::moveKinematic(uint32_t body, const PhysicsVec3& position, const PhysicsQuat& rotation,
                                  double dt) {
    m_impl->bodies().MoveKinematic(m_impl->added(body), toRVec3(position), toQuat(rotation),
                                   static_cast<float>(dt));
}

uint32_t PlanetPhysics::createCharacter(const PhysicsVec3& position, const PhysicsVec3& up, double radius,
                                        double height, double mass, double maxSlopeDegrees) {
    float r = static_cast<float>(radius);
    float halfCylinder = std::max(0.5f * static_cast<float>(height) - r, 0.01f);
    JPH::RefConst<JPH::Shape> capsule =
        JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, halfCylinder + r, 0.0f), JPH::Quat::sIdentity(),
                                            new JPH::CapsuleShape(halfCylinder, r))
            .Create()
            .Get();
    JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
    settings->mShape = capsule;
    settings->mInnerBodyShape = capsule;
    settings->mInnerBodyLayer = Layers::MOVING;
    settings->mMass = static_cast<float>(mass);
    settings->mMaxSlopeAngle = JPH::DegreesToRadians(static_cast<float>(maxSlopeDegrees));
    settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -r);
    settings->mUp = toVec3(up).Normalized();
    JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
        settings, toRVec3(position), upRotation(toVec3(up)), 0, &m_impl->system);
    m_impl->characters.push_back(character);
    uint32_t id = static_cast<uint32_t>(m_impl->characters.size() - 1);
    m_impl->setGroup(character->GetInnerBodyID(), JPH::CollisionGroup(m_impl->characterGroups, id, 0));
    return id;
}

void PlanetPhysics::removeCharacter(uint32_t character) {
    m_impl->character(character);
    m_impl->characters[character] = nullptr;
}

void PlanetPhysics::setCharacterUp(uint32_t character, const PhysicsVec3& up) {
    JPH::CharacterVirtual& c = m_impl->character(character);
    JPH::Vec3 u = toVec3(up).Normalized();
    c.SetUp(u);
    c.SetRotation(upRotation(u));
}

PhysicsVec3 PlanetPhysics::characterPosition(uint32_t character) const {
    return fromVec3(m_impl->character(character).GetPosition());
}

void PlanetPhysics::setCharacterPosition(uint32_t character, const PhysicsVec3& position) {
    m_impl->character(character).SetPosition(toRVec3(position));
}

PhysicsVec3 PlanetPhysics::characterVelocity(uint32_t character) const {
    return fromVec3(m_impl->character(character).GetLinearVelocity());
}

void PlanetPhysics::setCharacterVelocity(uint32_t character, const PhysicsVec3& velocity) {
    m_impl->character(character).SetLinearVelocity(toVec3(velocity));
}

void PlanetPhysics::updateCharacter(uint32_t character, double dt, const PhysicsVec3& gravity) {
    JPH::CharacterVirtual& c = m_impl->character(character);
    JPH::Vec3 up = c.GetUp();
    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = -STICK_DOWN * up;
    settings.mWalkStairsStepUp = STEP_UP * up;
    SkipCharacter self(c.GetInnerBodyID(), character);
    c.ExtendedUpdate(static_cast<float>(dt), toVec3(gravity), settings,
                     m_impl->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                     m_impl->system.GetDefaultLayerFilter(Layers::MOVING), self, {}, *m_impl->tempAllocator);
}

CharacterGround PlanetPhysics::characterGround(uint32_t character) const {
    const JPH::CharacterVirtual& c = m_impl->character(character);
    CharacterGround ground;
    ground.state = static_cast<CharacterGroundState>(static_cast<int>(c.GetGroundState()));
    ground.normal = fromVec3(c.GetGroundNormal());
    ground.velocity = fromVec3(c.GetGroundVelocity());
    ground.hasBody = !c.GetGroundBodyID().IsInvalid();
    ground.body = ground.hasBody ? c.GetGroundBodyID().GetIndexAndSequenceNumber() : 0;
    return ground;
}

void PlanetPhysics::getTransform(uint32_t body, PhysicsVec3& position, PhysicsQuat& rotation) const {
    JPH::RVec3 p;
    JPH::Quat q;
    m_impl->bodies().GetPositionAndRotation(m_impl->added(body), p, q);
    position = fromVec3(p);
    rotation = fromQuat(q);
}

void PlanetPhysics::setTransform(uint32_t body, const PhysicsVec3& position, const PhysicsQuat& rotation,
                                 bool activate) {
    m_impl->bodies().SetPositionAndRotation(m_impl->added(body), toRVec3(position), toQuat(rotation),
                                            activate ? JPH::EActivation::Activate
                                                     : JPH::EActivation::DontActivate);
}

void PlanetPhysics::getVelocity(uint32_t body, PhysicsVec3& linear, PhysicsVec3& angular) const {
    JPH::Vec3 v;
    JPH::Vec3 w;
    m_impl->bodies().GetLinearAndAngularVelocity(m_impl->added(body), v, w);
    linear = fromVec3(v);
    angular = fromVec3(w);
}

void PlanetPhysics::setVelocity(uint32_t body, const PhysicsVec3& linear, const PhysicsVec3& angular) {
    m_impl->bodies().SetLinearAndAngularVelocity(m_impl->added(body), toVec3(linear), toVec3(angular));
}

void PlanetPhysics::addForce(uint32_t body, const PhysicsVec3& force) {
    m_impl->bodies().AddForce(m_impl->added(body), toVec3(force), JPH::EActivation::Activate);
}

void PlanetPhysics::addImpulse(uint32_t body, const PhysicsVec3& impulse) {
    m_impl->bodies().AddImpulse(m_impl->added(body), toVec3(impulse));
}

void PlanetPhysics::setMotion(uint32_t body, PhysicsMotion motion) {
    JPH::BodyID id = m_impl->added(body);
    bool moving = motion != PhysicsMotion::Static;
    JPH::BodyInterface& bodies = m_impl->bodies();
    bodies.SetObjectLayer(id, moving ? Layers::MOVING : Layers::NON_MOVING);
    bodies.SetMotionType(id, toMotionType(motion),
                         moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
}

void PlanetPhysics::setPassThrough(uint32_t body, int character) {
    JPH::BodyID id = m_impl->added(body);
    if (character < 0) {
        m_impl->setGroup(id, JPH::CollisionGroup());
        return;
    }
    m_impl->character(static_cast<uint32_t>(character));
    m_impl->setGroup(id, JPH::CollisionGroup(m_impl->characterGroups, static_cast<uint32_t>(character), 0));
}

void PlanetPhysics::setFar(uint32_t body, bool far) {
    JPH::BodyID id = m_impl->added(body);
    JPH::BodyInterface& bodies = m_impl->bodies();
    if (!Layers::moving(bodies.GetObjectLayer(id))) {
        return;
    }
    bodies.SetObjectLayer(id, far ? Layers::FAR : Layers::MOVING);
}

void PlanetPhysics::setLinearDamping(uint32_t body, double damping) {
    JPH::BodyLockWrite lock(m_impl->system.GetBodyLockInterface(), m_impl->added(body));
    JPH::MotionProperties* motion = lock.GetBody().GetMotionPropertiesUnchecked();
    if (motion != nullptr) {
        motion->SetLinearDamping(static_cast<float>(damping));
    }
}

bool PlanetPhysics::isFar(uint32_t body) const {
    return m_impl->bodies().GetObjectLayer(m_impl->added(body)) == Layers::FAR;
}

PhysicsMotion PlanetPhysics::motion(uint32_t body) const {
    return fromMotionType(m_impl->bodies().GetMotionType(m_impl->added(body)));
}

double PlanetPhysics::mass(uint32_t body) const {
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), m_impl->added(body));
    const JPH::Body& b = lock.GetBody();
    if (b.IsStatic()) {
        return 0.0;
    }
    float inverse = b.GetMotionProperties()->GetInverseMassUnchecked();
    return inverse > 0.0f ? 1.0 / static_cast<double>(inverse) : 0.0;
}

bool PlanetPhysics::isActive(uint32_t body) const {
    return m_impl->bodies().IsActive(m_impl->added(body));
}

void PlanetPhysics::activate(uint32_t body) {
    m_impl->bodies().ActivateBody(m_impl->added(body));
}

void PlanetPhysics::deactivate(uint32_t body) {
    m_impl->bodies().DeactivateBody(m_impl->added(body));
}

void PlanetPhysics::getTransforms(const uint32_t* bodies, size_t count, double* out) const {
    for (size_t k = 0; k < count; ++k) {
        PhysicsVec3 p;
        PhysicsQuat q;
        getTransform(bodies[k], p, q);
        std::copy(p.begin(), p.end(), out + 7 * k);
        std::copy(q.begin(), q.end(), out + 7 * k + 3);
    }
}

bool PlanetPhysics::castRay(const PhysicsVec3& origin, const PhysicsVec3& direction, double maxDistance,
                            PhysicsRayHit& hit, int skipCharacter) const {
    PhysicsVec3 reach{direction[0] * maxDistance, direction[1] * maxDistance, direction[2] * maxDistance};
    JPH::RRayCast ray(toRVec3(origin), toVec3(reach));
    JPH::RayCastResult result;
    bool found = false;
    const JPH::BroadPhaseLayerFilter& broadPhase = m_impl->system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING);
    const JPH::ObjectLayerFilter& layers = m_impl->system.GetDefaultLayerFilter(Layers::MOVING);
    if (skipCharacter >= 0) {
        uint32_t id = static_cast<uint32_t>(skipCharacter);
        SkipCharacter filter(m_impl->character(id).GetInnerBodyID(), id);
        found = m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, broadPhase, layers, filter);
    } else {
        found = m_impl->system.GetNarrowPhaseQuery().CastRay(ray, result, broadPhase, layers);
    }
    if (!found) {
        return false;
    }
    JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded()) {
        return false;
    }
    hit.body = result.mBodyID.GetIndexAndSequenceNumber();
    hit.point = fromVec3(point);
    hit.normal = fromVec3(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    hit.fraction = static_cast<double>(result.mFraction);
    return true;
}

void PlanetPhysics::watchStrikes(uint32_t body, bool watched) {
    m_impl->bodies().SetUserData(m_impl->added(body), watched ? WATCHED : 0);
}

void PlanetPhysics::setStrikeMomentum(double momentum) {
    m_impl->strikes.momentum = static_cast<float>(momentum);
}

std::vector<uint32_t> PlanetPhysics::takeStruck() {
    std::vector<uint32_t> bodies;
    for (const PhysicsStrike& strike : m_impl->strikes.take()) {
        bodies.push_back(strike.body);
    }
    return bodies;
}

std::vector<PhysicsStrike> PlanetPhysics::takeStrikes() {
    return m_impl->strikes.take();
}

void PlanetPhysics::applyCentralGravity(const PhysicsVec3& centre, double gm) {
    JPH::BodyIDVector& active = m_impl->scratch;
    m_impl->system.GetActiveBodies(JPH::EBodyType::RigidBody, active);
    const JPH::BodyLockInterfaceNoLock& locks = m_impl->system.GetBodyLockInterfaceNoLock();
    for (const JPH::BodyID& id : active) {
        JPH::BodyLockWrite lock(locks, id);
        if (!lock.Succeeded()) {
            continue;
        }
        JPH::Body& body = lock.GetBody();
        if (!body.IsDynamic()) {
            continue;
        }
        JPH::RVec3 p = body.GetCenterOfMassPosition();
        double dx = centre[0] - static_cast<double>(p.GetX());
        double dy = centre[1] - static_cast<double>(p.GetY());
        double dz = centre[2] - static_cast<double>(p.GetZ());
        double r2 = dx * dx + dy * dy + dz * dz;
        if (r2 <= 0.0) {
            continue;
        }
        float inverse = body.GetMotionProperties()->GetInverseMass();
        if (inverse <= 0.0f) {
            continue;
        }
        double scale = gm / (r2 * std::sqrt(r2)) / static_cast<double>(inverse);
        body.AddForce(JPH::Vec3(static_cast<float>(dx * scale), static_cast<float>(dy * scale),
                                static_cast<float>(dz * scale)));
    }
}

void PlanetPhysics::step(double dt) {
    m_impl->system.Update(static_cast<float>(dt), 1, m_impl->tempAllocator.get(), m_impl->jobSystem.get());
}

void PlanetPhysics::shiftOrigin(const PhysicsVec3& offset) {
    JPH::BodyIDVector all;
    m_impl->system.GetBodies(all);
    JPH::BodyInterface& bodies = m_impl->bodies();
    JPH::RVec3 shift = toRVec3(offset);
    for (const JPH::BodyID& id : all) {
        bodies.SetPosition(id, bodies.GetPosition(id) - shift, JPH::EActivation::DontActivate);
    }
    for (const JPH::Ref<JPH::CharacterVirtual>& c : m_impl->characters) {
        if (c != nullptr) {
            c->SetPosition(c->GetPosition() - shift);
        }
    }
}

uint32_t PlanetPhysics::bodyCount() const {
    return m_impl->system.GetNumBodies();
}

uint32_t PlanetPhysics::activeCount() const {
    return m_impl->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

}  // namespace Shoonyakasha::Planet
