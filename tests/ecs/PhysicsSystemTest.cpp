//
// PhysicsSystemTest.cpp - The ECS physics system's behaviour
//
// Tier 2: ECS integration — uses EnTT registry and the physics engine, no GPU.
//
// Falling under gravity, resting on a plane (boxes, spheres, capsules),
// impulses and torque impulses, kinematic bodies moving with their
// transforms and carrying what rests on them, gravity off per body, frozen
// rotation, triggers, restitution combined by multiplying, the substep cap,
// the transform between fixed steps, rotations kept through the Euler
// conversion, and bodies made and removed with their components.
//

#include <gtest/gtest.h>
#include "ECS/Core.h"
#include "ECS/PhysicsSystem.h"

#include <glm/glm.hpp>

using namespace Shoonyakasha::ECS;

namespace {

constexpr float STEP = 1.0f / 60.0f;

struct World {
    entt::registry registry;
    PhysicsSystem physics;

    World() { physics.initialize(registry); }
    ~World() { physics.cleanup(registry); }

    entt::entity body(RigidBodyComponent::Type type, const glm::vec3& position, float mass = 1.0f,
                      ColliderComponent::Shape shape = ColliderComponent::Box, const glm::vec3& size = glm::vec3(1.0f),
                      float drag = 0.0f) {
        const entt::entity e = registry.create();
        auto& t = registry.emplace<TransformComponent>(e);
        t.position = position;
        auto& collider = registry.emplace<ColliderComponent>(e);
        collider.shape = shape;
        collider.size = size;
        RigidBodyComponent rb;
        rb.type = type;
        rb.mass = mass;
        rb.drag = drag;
        rb.angularDrag = drag;
        registry.emplace<RigidBodyComponent>(e, rb);      // the body is made here, from the above
        return e;
    }

    entt::entity ground() {
        return body(RigidBodyComponent::Static, glm::vec3(0.0f), 0.0f, ColliderComponent::Plane);
    }

    void run(float seconds, float dt = STEP) {
        const int frames = static_cast<int>(seconds / dt + 0.5f);
        for (int k = 0; k < frames; ++k) physics.update(registry, dt);
    }

    glm::vec3 position(entt::entity e) { return registry.get<TransformComponent>(e).position; }
};

} // namespace

TEST(PhysicsSystem, FallsUnderGravity) {
    World w;
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 10.0f, 0.0f));
    w.run(1.0f);
    EXPECT_NEAR(w.position(box).y, 10.0f - 0.5f * 9.81f, 0.15f);
    EXPECT_NEAR(w.physics.getLinearVelocity(w.registry, box).y, -9.81f, 0.15f);
    EXPECT_NEAR(w.position(box).x, 0.0f, 1e-4f);
}

TEST(PhysicsSystem, ShapesRestOnAPlane) {
    World w;
    w.ground();
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f));
    const auto ball = w.body(RigidBodyComponent::Dynamic, glm::vec3(5.0f, 3.0f, 0.0f), 1.0f,
                             ColliderComponent::Sphere, glm::vec3(0.5f));
    // A capsule's size: radius 0.4, its cylinder 1.2 long: 2.0 tall in all. Kept upright.
    const auto capsule = w.body(RigidBodyComponent::Dynamic, glm::vec3(-5.0f, 3.0f, 0.0f), 1.0f,
                                ColliderComponent::Capsule, glm::vec3(0.4f, 1.2f, 0.4f));
    w.registry.get<RigidBodyComponent>(capsule).freezeRotation = true;
    w.physics.rebuildBody(w.registry, capsule);
    w.run(4.0f);
    EXPECT_NEAR(w.position(box).y, 0.5f, 0.05f);
    EXPECT_NEAR(w.position(ball).y, 0.5f, 0.05f);
    EXPECT_NEAR(w.position(capsule).y, 1.0f, 0.05f);
    EXPECT_LT(glm::length(w.physics.getLinearVelocity(w.registry, box)), 0.05f);
}

TEST(PhysicsSystem, ImpulsesChangeVelocity) {
    World w;
    w.physics.setGravity(glm::vec3(0.0f));
    EXPECT_EQ(w.physics.getGravity(), glm::vec3(0.0f));
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f), 2.0f);
    w.physics.addImpulse(w.registry, box, glm::vec3(4.0f, 0.0f, 0.0f));
    w.run(1.0f);
    const glm::vec3 v = w.physics.getLinearVelocity(w.registry, box);
    EXPECT_NEAR(v.x, 2.0f, 1e-3f);
    EXPECT_NEAR(w.position(box).x, 2.0f, 0.05f);
    w.physics.addTorqueImpulse(w.registry, box, glm::vec3(0.0f, 1.0f, 0.0f));
    w.run(STEP);
    EXPECT_GT(w.physics.getAngularVelocity(w.registry, box).y, 0.5f);
    w.physics.setLinearVelocity(w.registry, box, glm::vec3(0.0f, 0.0f, -3.0f));
    w.physics.setAngularVelocity(w.registry, box, glm::vec3(0.0f));
    w.run(STEP);
    EXPECT_NEAR(w.physics.getLinearVelocity(w.registry, box).z, -3.0f, 1e-3f);
    EXPECT_NEAR(glm::length(w.physics.getAngularVelocity(w.registry, box)), 0.0f, 1e-3f);
}

TEST(PhysicsSystem, ForcesAccumulateOverAStep) {
    World w;
    w.physics.setGravity(glm::vec3(0.0f));
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f), 2.0f);
    w.physics.addForce(w.registry, box, glm::vec3(0.0f, 0.0f, 120.0f));   // 60 m/s^2 for one step
    w.run(STEP);
    EXPECT_NEAR(w.physics.getLinearVelocity(w.registry, box).z, 1.0f, 0.02f);
}

TEST(PhysicsSystem, KinematicBodiesFollowTheirTransformAndCarryWhatRestsOnThem) {
    World w;
    const auto lift = w.body(RigidBodyComponent::Kinematic, glm::vec3(0.0f), 0.0f, ColliderComponent::Box,
                             glm::vec3(4.0f, 1.0f, 4.0f));
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 1.01f, 0.0f));
    w.run(1.0f);
    EXPECT_NEAR(w.position(box).y, 1.0f, 0.05f);
    for (int k = 0; k < 60; ++k) {                       // up at 1 m/s for a second
        w.registry.get<TransformComponent>(lift).position.y += STEP;
        w.physics.update(w.registry, STEP);
    }
    w.run(0.5f);
    EXPECT_NEAR(w.position(box).y, 2.0f, 0.1f);
    EXPECT_NEAR(w.position(lift).y, 1.0f, 1e-5f);        // the transform stays the caller's
}

TEST(PhysicsSystem, GravityOffForOneBody) {
    World w;
    const auto floating = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 10.0f, 0.0f));
    w.registry.get<RigidBodyComponent>(floating).useGravity = false;
    w.physics.rebuildBody(w.registry, floating);
    const auto falling = w.body(RigidBodyComponent::Dynamic, glm::vec3(5.0f, 10.0f, 0.0f));
    w.run(1.0f);
    EXPECT_NEAR(w.position(floating).y, 10.0f, 1e-4f);
    EXPECT_LT(w.position(falling).y, 6.0f);
}

TEST(PhysicsSystem, FrozenRotationIgnoresTorque) {
    World w;
    w.physics.setGravity(glm::vec3(0.0f));
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f));
    w.registry.get<RigidBodyComponent>(box).freezeRotation = true;
    w.physics.rebuildBody(w.registry, box);
    w.physics.addTorqueImpulse(w.registry, box, glm::vec3(1.0f, 2.0f, 3.0f));
    w.run(1.0f);
    EXPECT_NEAR(glm::length(w.physics.getAngularVelocity(w.registry, box)), 0.0f, 1e-4f);
    EXPECT_NEAR(glm::length(w.registry.get<TransformComponent>(box).rotation), 0.0f, 1e-4f);
}

TEST(PhysicsSystem, TriggersLetBodiesThrough) {
    World w;
    const auto trigger = w.body(RigidBodyComponent::Static, glm::vec3(0.0f), 0.0f, ColliderComponent::Box,
                                glm::vec3(4.0f, 1.0f, 4.0f));
    w.registry.get<ColliderComponent>(trigger).isTrigger = true;
    w.physics.rebuildBody(w.registry, trigger);
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 3.0f, 0.0f));
    w.run(2.0f);
    EXPECT_LT(w.position(box).y, -5.0f);
}

TEST(PhysicsSystem, RestitutionIsMultiplied) {
    World w;
    const auto ground = w.ground();
    const auto ball = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 2.0f, 0.0f), 1.0f,
                             ColliderComponent::Sphere, glm::vec3(0.5f));
    w.registry.get<ColliderComponent>(ball).restitution = 0.8f;
    w.physics.rebuildBody(w.registry, ball);
    float highest = 0.0f;
    bool landed = false;
    for (int k = 0; k < 120; ++k) {                      // the ground's restitution is 0: no bounce
        w.physics.update(w.registry, STEP);
        landed = landed || w.position(ball).y < 0.6f;
        if (landed) highest = std::max(highest, w.position(ball).y);
    }
    EXPECT_LT(highest, 0.6f);
    w.registry.get<ColliderComponent>(ground).restitution = 1.0f;
    w.physics.rebuildBody(w.registry, ground);
    w.registry.get<TransformComponent>(ball).position = glm::vec3(0.0f, 2.0f, 0.0f);
    w.physics.rebuildBody(w.registry, ball);
    highest = 0.0f;
    landed = false;
    for (int k = 0; k < 120; ++k) {                      // 1 x 0.8: it bounces
        w.physics.update(w.registry, STEP);
        landed = landed || w.position(ball).y < 0.6f;
        if (landed) highest = std::max(highest, w.position(ball).y);
    }
    EXPECT_GT(highest, 1.2f);
}

TEST(PhysicsSystem, SubstepsAreCapped) {
    World w;
    w.physics.setMaxSubSteps(2);
    EXPECT_EQ(w.physics.getMaxSubSteps(), 2);
    EXPECT_FLOAT_EQ(w.physics.getFixedTimeStep(), STEP);
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 10.0f, 0.0f));
    w.physics.update(w.registry, 1.0f);                  // a long frame: two steps at most
    EXPECT_NEAR(w.physics.getLinearVelocity(w.registry, box).y, -9.81f * 2.0f * STEP, 0.02f);
}

TEST(PhysicsSystem, TransformMovesBetweenFixedSteps) {
    World w;
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f, 10.0f, 0.0f));
    w.physics.setLinearVelocity(w.registry, box, glm::vec3(0.0f, -6.0f, 0.0f));
    w.run(STEP);                                         // one whole step first
    float last = w.position(box).y;
    for (int k = 0; k < 8; ++k) {                        // half a fixed step a frame
        w.physics.update(w.registry, 0.5f * STEP);
        const float y = w.position(box).y;
        EXPECT_LT(y, last) << "frame " << k;
        last = y;
    }
}

TEST(PhysicsSystem, RotationsSurviveTheEulerConversion) {
    World w;
    w.physics.setGravity(glm::vec3(0.0f));
    const auto box = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f));
    w.registry.get<TransformComponent>(box).rotation = glm::vec3(0.3f, 0.5f, 0.1f);
    w.physics.rebuildBody(w.registry, box);
    w.run(0.5f);
    const glm::vec3 r = w.registry.get<TransformComponent>(box).rotation;
    EXPECT_NEAR(r.x, 0.3f, 1e-4f);
    EXPECT_NEAR(r.y, 0.5f, 1e-4f);
    EXPECT_NEAR(r.z, 0.1f, 1e-4f);
}

TEST(PhysicsSystem, BodiesComeAndGoWithTheirComponents) {
    World w;
    EXPECT_EQ(w.physics.getBodyCount(), 0u);
    const auto a = w.body(RigidBodyComponent::Dynamic, glm::vec3(0.0f));
    const auto b = w.body(RigidBodyComponent::Static, glm::vec3(3.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(w.physics.getBodyCount(), 2u);
    w.registry.remove<RigidBodyComponent>(a);
    EXPECT_EQ(w.physics.getBodyCount(), 1u);
    w.registry.destroy(b);
    EXPECT_EQ(w.physics.getBodyCount(), 0u);
    w.physics.addImpulse(w.registry, a, glm::vec3(1.0f));  // no body: nothing happens
    EXPECT_EQ(w.physics.getLinearVelocity(w.registry, a), glm::vec3(0.0f));
}
