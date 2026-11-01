#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

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

/// Rigid bodies on Jolt Physics (single precision), with no global
/// gravity: applyCentralGravity pulls the awake dynamic bodies towards a
/// point each step. Positions are in the caller's frame, which shiftOrigin
/// translates; rotations are unit quaternions, (w, x, y, z).
///
/// Static bodies collide with moving ones only; kinematic and dynamic
/// bodies collide with everything. Every box and sphere can change its
/// motion later (setMotion). Bodies are named by a 32-bit id, which stays
/// valid until remove.
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

    /// A static triangle mesh from a grid of `columns` x `rows` points
    /// (x, y, z each, row by row), relative to `position`. Each cell is two
    /// triangles, wound counter-clockwise seen from the side that
    /// cross(next column - point, next row - point) points to.
    uint32_t createGridMesh(const float* points, uint32_t columns, uint32_t rows, const PhysicsVec3& position,
                            double friction);

    void remove(uint32_t body);
    bool contains(uint32_t body) const;

    void getTransform(uint32_t body, PhysicsVec3& position, PhysicsQuat& rotation) const;
    void setTransform(uint32_t body, const PhysicsVec3& position, const PhysicsQuat& rotation, bool activate);
    void getVelocity(uint32_t body, PhysicsVec3& linear, PhysicsVec3& angular) const;
    void setVelocity(uint32_t body, const PhysicsVec3& linear, const PhysicsVec3& angular);

    /// A force (N) through the centre of mass for the next step; wakes the body.
    void addForce(uint32_t body, const PhysicsVec3& force);
    /// An impulse (N s) through the centre of mass; wakes the body.
    void addImpulse(uint32_t body, const PhysicsVec3& impulse);

    /// The body's motion, and its collisions those of the new motion; a
    /// body turned kinematic or dynamic is woken.
    void setMotion(uint32_t body, PhysicsMotion motion);
    PhysicsMotion motion(uint32_t body) const;
    /// kg; 0 for static bodies.
    double mass(uint32_t body) const;
    /// Whether the body is awake.
    bool isActive(uint32_t body) const;
    void activate(uint32_t body);

    /// Position (3) and rotation (4) of each of `count` bodies into `out`,
    /// 7 doubles per body.
    void getTransforms(const uint32_t* bodies, size_t count, double* out) const;

    /// The nearest surface along `direction` (unit) from `origin` within
    /// `maxDistance` metres; false when there is none.
    bool castRay(const PhysicsVec3& origin, const PhysicsVec3& direction, double maxDistance,
                 PhysicsRayHit& hit) const;

    /// For the next step, each awake dynamic body pulled towards `centre`
    /// with acceleration gm / r^2 (gm in m^3/s^2).
    void applyCentralGravity(const PhysicsVec3& centre, double gm);

    /// One step of `dt` seconds.
    void step(double dt);

    /// Every body moved by -offset, without waking any.
    void shiftOrigin(const PhysicsVec3& offset);

    uint32_t bodyCount() const;
    uint32_t activeCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Shoonyakasha::Planet
