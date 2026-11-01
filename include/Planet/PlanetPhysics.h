#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Shoonyakasha::Planet {

using PhysicsVec3 = std::array<double, 3>;
using PhysicsQuat = std::array<double, 4>;   ///< (w, x, y, z), a unit quaternion

/// How a body moves: not at all, along a path it is given, or under forces
/// and contacts.
enum class PhysicsMotion { Static = 0, Kinematic = 1, Dynamic = 2 };

/// What a ray hit: the body, the point and the surface's unit normal there,
/// and how far along the ray (0 at its origin, 1 at its maximum distance).
struct PhysicsRayHit {
    uint32_t body = 0;
    PhysicsVec3 point{0.0, 0.0, 0.0};
    PhysicsVec3 normal{0.0, 0.0, 0.0};
    double fraction = 0.0;
};

/// A watched body struck since the last takeStrikes: the fastest speed and
/// the largest momentum along the contact's normal it was struck with.
struct PhysicsStrike {
    uint32_t body = 0;
    double speed = 0.0;        // m/s
    double momentum = 0.0;     // kg m/s
};

/// How a character stands: on ground it can walk on, on ground too steep to
/// climb, touching something that does not hold it up, or touching nothing.
enum class CharacterGroundState { OnGround = 0, OnSteepGround = 1, NotSupported = 2, InAir = 3 };

/// What a character stands on: its state, the ground's unit normal and
/// velocity there, and the body (when `hasBody`).
struct CharacterGround {
    CharacterGroundState state = CharacterGroundState::InAir;
    PhysicsVec3 normal{0.0, 0.0, 0.0};
    PhysicsVec3 velocity{0.0, 0.0, 0.0};
    uint32_t body = 0;
    bool hasBody = false;
};

/// Rigid bodies on Jolt Physics (single precision), with no global
/// gravity: applyCentralGravity pulls the awake dynamic bodies towards a
/// point each step. A moving body's velocity is damped by Jolt's default
/// (0.05 of it a second) until setLinearDamping changes it. Positions are in the caller's frame, which shiftOrigin
/// translates; rotations are unit quaternions, (w, x, y, z).
///
/// Static bodies (and the ground meshes) collide with moving ones only;
/// kinematic and dynamic bodies collide with each other and with static
/// bodies. Ground meshes are fine or coarse: a moving body is near or far
/// (setFar), and collides with the fine ground when near, with the coarse
/// ground when far. Every box and sphere can change its motion later
/// (setMotion). Bodies are named by a 32-bit id, which stays valid until
/// remove.
///
/// Characters are capsules moved by their velocity (Jolt's
/// CharacterVirtual), each with its own up: its position is at the
/// capsule's bottom, and it stands on what it touches below the middle of
/// its lower cap. Each carries a kinematic inner body that moving bodies
/// collide with. Characters are named by their own 32-bit ids.
class PlanetPhysics {
public:
    /// A world holding up to `maxBodies` bodies, stepped on `threads`
    /// worker threads (-1: up to four, one fewer than the hardware has).
    explicit PlanetPhysics(uint32_t maxBodies = 4096, int threads = -1);
    ~PlanetPhysics();

    PlanetPhysics(const PlanetPhysics&) = delete;
    PlanetPhysics& operator=(const PlanetPhysics&) = delete;

    /// A box of the given half extents (m). Its mass is `mass` (kg) when
    /// greater than 0, otherwise from `density` (kg/m^3).
    uint32_t createBox(const PhysicsVec3& halfExtents, const PhysicsVec3& position, const PhysicsQuat& rotation,
                       PhysicsMotion motion, double density, double mass, double friction, double restitution);

    /// A sphere; its mass as createBox's.
    uint32_t createSphere(double radius, const PhysicsVec3& position, PhysicsMotion motion, double density,
                          double mass, double friction, double restitution);

    /// A capsule along its Y axis: a cylinder `halfHeight` metres either
    /// side of its centre, capped by half spheres of `radius`, turned by
    /// `rotation`; its mass as createBox's.
    uint32_t createCapsule(double radius, double halfHeight, const PhysicsVec3& position,
                           const PhysicsQuat& rotation, PhysicsMotion motion, double density, double mass,
                           double friction, double restitution);

    /// A static triangle mesh from a grid of `columns` x `rows` points
    /// (x, y, z each, row by row), relative to `position`. Each cell is two
    /// triangles, wound counter-clockwise seen from the side that
    /// cross(next column - point, next row - point) points to. A ground
    /// mesh, or with `coarse` a coarse ground mesh (see the class).
    uint32_t createGridMesh(const float* points, uint32_t columns, uint32_t rows, const PhysicsVec3& position,
                            double friction, bool coarse = false);

    /// The convex hull of `count` points (x, y, z each) relative to
    /// `position`, turned by `rotation`; its mass as createBox's.
    uint32_t createConvexHull(const float* points, size_t count, const PhysicsVec3& position,
                              const PhysicsQuat& rotation, PhysicsMotion motion, double density, double mass,
                              double friction, double restitution);

    void remove(uint32_t body);
    bool contains(uint32_t body) const;

    /// A kinematic body moved to `position` and `rotation` over the next step of `dt` seconds.
    void moveKinematic(uint32_t body, const PhysicsVec3& position, const PhysicsQuat& rotation, double dt);

    /// A character `height` metres tall of `radius`, its feet at
    /// `position`, standing along `up` (unit); it pushes bodies as a
    /// `mass` kg body and walks on slopes up to `maxSlopeDegrees`.
    uint32_t createCharacter(const PhysicsVec3& position, const PhysicsVec3& up, double radius, double height,
                             double mass, double maxSlopeDegrees);
    void removeCharacter(uint32_t character);
    /// The character's up (unit), and its capsule turned along it.
    void setCharacterUp(uint32_t character, const PhysicsVec3& up);
    PhysicsVec3 characterPosition(uint32_t character) const;
    void setCharacterPosition(uint32_t character, const PhysicsVec3& position);
    PhysicsVec3 characterVelocity(uint32_t character) const;
    void setCharacterVelocity(uint32_t character, const PhysicsVec3& velocity);
    /// The character moved by its velocity over `dt` seconds, sliding along
    /// what it touches, stepping up onto steps up to 0.4 m and kept on the
    /// floor down to 0.5 m below it while it was on ground (both along its
    /// up); `gravity` (m/s^2) is how it pushes what it stands on.
    void updateCharacter(uint32_t character, double dt, const PhysicsVec3& gravity);
    CharacterGround characterGround(uint32_t character) const;

    void getTransform(uint32_t body, PhysicsVec3& position, PhysicsQuat& rotation) const;
    void setTransform(uint32_t body, const PhysicsVec3& position, const PhysicsQuat& rotation, bool activate);
    void getVelocity(uint32_t body, PhysicsVec3& linear, PhysicsVec3& angular) const;
    void setVelocity(uint32_t body, const PhysicsVec3& linear, const PhysicsVec3& angular);

    /// A force (N) through the centre of mass for the next step; wakes the body.
    void addForce(uint32_t body, const PhysicsVec3& force);
    /// An impulse (N s) through the centre of mass; wakes the body.
    void addImpulse(uint32_t body, const PhysicsVec3& impulse);

    /// The body's motion, and its collisions those of the new motion; a
    /// body turned kinematic or dynamic is woken, and near.
    void setMotion(uint32_t body, PhysicsMotion motion);
    /// Whether a moving body is far (on the coarse ground) or near (on the
    /// fine ground); static bodies stay as they are.
    void setFar(uint32_t body, bool far);
    bool isFar(uint32_t body) const;
    /// The share of a moving body's velocity it loses a second (Jolt's
    /// linear damping); static bodies stay as they are.
    void setLinearDamping(uint32_t body, double damping);
    PhysicsMotion motion(uint32_t body) const;
    /// The body passes through the character's capsule and its inner body,
    /// and they through it; `character` -1: through no character. A body
    /// passes through one character at a time.
    void setPassThrough(uint32_t body, int character);
    /// kg; 0 for static bodies.
    double mass(uint32_t body) const;
    /// Whether the body is awake.
    bool isActive(uint32_t body) const;
    void activate(uint32_t body);
    /// The body put to sleep where it is, until something wakes it.
    void deactivate(uint32_t body);

    /// Position (3) and rotation (4) of each of `count` bodies into `out`,
    /// 7 doubles per body.
    void getTransforms(const uint32_t* bodies, size_t count, double* out) const;

    /// The nearest surface along `direction` (unit) from `origin` within
    /// `maxDistance` metres that a near moving body would hit (not the
    /// coarse ground); false when there is none. With `skipCharacter`
    /// (a character's id, or -1 for none), the ray passes through that
    /// character's inner body and the bodies passing through it.
    bool castRay(const PhysicsVec3& origin, const PhysicsVec3& direction, double maxDistance,
                 PhysicsRayHit& hit, int skipCharacter = -1) const;

    /// Whether a dynamic body striking `body` is reported (takeStruck, takeStrikes).
    void watchStrikes(uint32_t body, bool watched);
    /// The momentum (kg m/s) a dynamic body must strike a watched body
    /// with, along the contact's normal, for the strike to be reported.
    void setStrikeMomentum(double momentum);
    /// The watched bodies struck since the last call (of either), each once, in order.
    std::vector<uint32_t> takeStruck();
    /// The same, each with its fastest speed and largest momentum.
    std::vector<PhysicsStrike> takeStrikes();

    /// For the next step, each awake dynamic body pulled towards `centre`
    /// with acceleration gm / r^2 (gm in m^3/s^2).
    void applyCentralGravity(const PhysicsVec3& centre, double gm);

    /// One step of `dt` seconds.
    void step(double dt);

    /// Every body and character moved by -offset, without waking any body.
    void shiftOrigin(const PhysicsVec3& offset);

    uint32_t bodyCount() const;
    uint32_t activeCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Shoonyakasha::Planet
