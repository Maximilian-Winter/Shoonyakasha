//
// IKTest.cpp - Tests for two-bone IK and IK goals on animators
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/AnimatorEvaluator.h"
#include "Animation/IK.h"
#include "ECS/Core.h"
#include "TestHelpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Shoonyakasha;

namespace {

Joint joint(const std::string& name, int parent, glm::vec3 t) {
    Joint j;
    j.name = name;
    j.parentIndex = parent;
    j.defaultTranslation = t;
    return j;
}

// Base -> Upper -> Middle -> End, hanging down one unit per bone. Base is
// turned and scaled when asked.
Skeleton makeLimb(glm::quat baseRotation = glm::quat(1, 0, 0, 0), float baseScale = 1.0f) {
    Skeleton s;
    s.joints.push_back(joint("Base", -1, glm::vec3(0.0f)));
    s.joints[0].defaultRotation = baseRotation;
    s.joints[0].defaultScale = glm::vec3(baseScale);
    s.joints.push_back(joint("Upper", 0, glm::vec3(0.0f)));
    s.joints.push_back(joint("Middle", 1, glm::vec3(0.0f, -1.0f, 0.0f)));
    s.joints.push_back(joint("End", 2, glm::vec3(0.0f, -1.0f, 0.0f)));
    s.buildNameLookup();
    s.buildEvaluationOrder();
    return s;
}

glm::vec3 at(const std::vector<glm::mat4>& g, int j) { return glm::vec3(g[j][3]); }

struct Solved {
    Pose pose;
    std::vector<glm::mat4> globals;
};

Solved solve(const Skeleton& s, TwoBoneIKGoal goal) {
    Solved r;
    r.pose.setRest(s);
    PoseOps::globalTransforms(s, r.pose, r.globals);
    goal.upper = 1;
    goal.middle = 2;
    goal.end = 3;
    solveTwoBoneIK(s, r.pose, r.globals, goal);
    return r;
}

// A small Mixamo-named humanoid facing +Z, left toward +X.
std::shared_ptr<Skeleton> makeHumanoid() {
    auto s = std::make_shared<Skeleton>();
    auto add = [&](const char* name, int parent, glm::vec3 t) {
        s->joints.push_back(joint(std::string("mixamorig:") + name, parent, t));
        return static_cast<int>(s->joints.size()) - 1;
    };
    const int hips = add("Hips", -1, {0.0f, 1.0f, 0.0f});
    const int spine = add("Spine", hips, {0.0f, 0.2f, 0.0f});
    const int head = add("Head", spine, {0.0f, 0.4f, 0.0f});
    (void)head;
    for (int side = 0; side < 2; ++side) {
        const float x = side == 0 ? 1.0f : -1.0f;
        const std::string s0 = side == 0 ? "Left" : "Right";
        const int arm = add((s0 + "Arm").c_str(), spine, {0.2f * x, 0.3f, 0.0f});
        const int fore = add((s0 + "ForeArm").c_str(), arm, {0.25f * x, 0.0f, 0.0f});
        add((s0 + "Hand").c_str(), fore, {0.25f * x, 0.0f, 0.0f});
        const int up = add((s0 + "UpLeg").c_str(), hips, {0.1f * x, -0.05f, 0.0f});
        // Knees bent slightly forward.
        const int leg = add((s0 + "Leg").c_str(), up, {0.0f, -0.45f, 0.02f});
        add((s0 + "Foot").c_str(), leg, {0.0f, -0.45f, -0.02f});
    }
    s->buildNameLookup();
    s->buildEvaluationOrder();
    return s;
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Solver
// ═══════════════════════════════════════════════════════════════

TEST(TwoBoneIK, ReachesTargetAndKeepsLengths) {
    const Skeleton s = makeLimb();
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(0.5f, -1.4f, 0.3f);
    const Solved r = solve(s, goal);
    TestHelpers::ExpectVec3Near(at(r.globals, 3), goal.target, 1e-4f);
    EXPECT_NEAR(glm::length(at(r.globals, 2) - at(r.globals, 1)), 1.0f, 1e-4f);
    EXPECT_NEAR(glm::length(at(r.globals, 3) - at(r.globals, 2)), 1.0f, 1e-4f);
}

TEST(TwoBoneIK, PoleChoosesTheBendSide) {
    const Skeleton s = makeLimb();
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(0.0f, -1.5f, 0.0f);
    goal.hasPole = true;
    goal.pole = glm::vec3(0.0f, 0.0f, 1.0f);
    EXPECT_GT(at(solve(s, goal).globals, 2).z, 0.5f);
    goal.pole = glm::vec3(0.0f, 0.0f, -1.0f);
    EXPECT_LT(at(solve(s, goal).globals, 2).z, -0.5f);
}

TEST(TwoBoneIK, OutOfReachStraightensTowardTarget) {
    const Skeleton s = makeLimb();
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(3.0f, 0.0f, 0.0f);
    const Solved r = solve(s, goal);
    TestHelpers::ExpectVec3Near(at(r.globals, 3), glm::vec3(2.0f, 0.0f, 0.0f), 1e-3f);
    TestHelpers::ExpectVec3Near(at(r.globals, 2), glm::vec3(1.0f, 0.0f, 0.0f), 1e-2f);
}

TEST(TwoBoneIK, WeightBlendsFromThePose) {
    const Skeleton s = makeLimb();
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(2.0f, 0.0f, 0.0f);
    goal.weight = 0.0f;
    TestHelpers::ExpectVec3Near(at(solve(s, goal).globals, 3), glm::vec3(0.0f, -2.0f, 0.0f), 1e-5f);
    goal.weight = 0.5f;
    const glm::vec3 halfway = at(solve(s, goal).globals, 3);
    EXPECT_GT(halfway.x, 0.5f);
    EXPECT_LT(halfway.x, 1.9f);
}

TEST(TwoBoneIK, EndRotationIsApplied) {
    const Skeleton s = makeLimb();
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(0.3f, -1.5f, 0.2f);
    goal.hasEndRotation = true;
    goal.endRotation = glm::angleAxis(0.8f, glm::normalize(glm::vec3(1.0f, 0.5f, 0.0f)));
    const Solved r = solve(s, goal);
    glm::mat3 m(r.globals[3]);
    const glm::quat q = glm::quat_cast(m);
    EXPECT_NEAR(std::abs(glm::dot(q, goal.endRotation)), 1.0f, 1e-4f);
    TestHelpers::ExpectVec3Near(at(r.globals, 3), goal.target, 1e-4f);
}

TEST(TwoBoneIK, UnderTurnedScaledParent) {
    const Skeleton s = makeLimb(glm::angleAxis(0.7f, glm::vec3(0.0f, 0.0f, 1.0f)), 2.0f);
    TwoBoneIKGoal goal;
    goal.target = glm::vec3(1.0f, -2.5f, 1.0f);   // within the scaled reach of 4
    const Solved r = solve(s, goal);
    TestHelpers::ExpectVec3Near(at(r.globals, 3), goal.target, 1e-3f);
}

// ═══════════════════════════════════════════════════════════════
// Animator goals
// ═══════════════════════════════════════════════════════════════

TEST(AnimatorIK, WorldGoalThroughTheModelTransform) {
    entt::registry registry;
    entt::entity root = registry.create();
    auto& transform = registry.emplace<ECS::TransformComponent>(root);
    transform.worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f)) *
                            glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0, 1, 0)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    auto& animator = registry.emplace<AnimatorComponent>(root);
    animator.skeleton = std::make_shared<Skeleton>(makeLimb());
    ASSERT_TRUE(AnimatorEvaluator::setChain(animator, "arm", 1, 2, 3));
    EXPECT_FALSE(AnimatorEvaluator::setChain(animator, "bad", 3, 2, 1));

    const glm::vec3 goal(5.3f, -0.7f, 0.2f);
    IKGoalState state;
    state.chain = "arm";
    state.position = goal;
    animator.ikGoals.push_back(state);
    AnimatorEvaluator::update(registry, 0.0f);

    const auto& after = registry.get<AnimatorComponent>(root);
    TestHelpers::ExpectVec3Near(glm::vec3(transform.worldMatrix * after.globalTransforms[3][3]), goal, 1e-4f);
    // The animated pose before IK is kept apart.
    TestHelpers::ExpectVec3Near(glm::vec3(after.animatedGlobals[3][3]), glm::vec3(0.0f, -2.0f, 0.0f), 1e-5f);
    TestHelpers::ExpectVec3Near(glm::vec3(after.boneMatrices[3][3]), glm::vec3(after.globalTransforms[3][3]), 1e-5f);
}

TEST(AnimatorIK, HumanoidFeetBendKneesForwardAndHipsOffset) {
    entt::registry registry;
    entt::entity root = registry.create();
    registry.emplace<ECS::TransformComponent>(root);
    auto& animator = registry.emplace<AnimatorComponent>(root);
    animator.skeleton = makeHumanoid();
    animator.humanoid = makeHumanoidAvatar(animator.skeleton);
    const int hips = animator.humanoid->joint(HumanoidBone::Hips);
    const int knee = animator.humanoid->joint(HumanoidBone::LeftLowerLeg);
    const int foot = animator.humanoid->joint(HumanoidBone::LeftFoot);

    // Lower the hips 0.2 and keep the left foot where it stands.
    AnimatorEvaluator::evaluate(animator);
    const glm::vec3 footAtRest(animator.globalTransforms[foot][3]);
    animator.hipsOffset = glm::vec3(0.0f, -0.2f, 0.0f);
    IKGoalState state;
    state.chain = "LeftFoot";
    state.position = footAtRest;
    animator.ikGoals.push_back(state);
    AnimatorEvaluator::update(registry, 0.0f);

    ASSERT_NE(animator.findChain("LeftFoot"), nullptr);
    EXPECT_NEAR(animator.globalTransforms[hips][3].y, 0.8f, 1e-5f);
    TestHelpers::ExpectVec3Near(glm::vec3(animator.globalTransforms[foot][3]), footAtRest, 1e-4f);
    EXPECT_GT(animator.globalTransforms[knee][3].z, 0.1f);   // the knee bends forward
    EXPECT_EQ(AnimatorEvaluator::chainFor(animator, "Tail"), nullptr);
}

TEST(AnimatorIK, NormalTiltsTheAnimatedEnd) {
    entt::registry registry;
    entt::entity root = registry.create();
    registry.emplace<ECS::TransformComponent>(root);
    auto& animator = registry.emplace<AnimatorComponent>(root);
    animator.skeleton = makeHumanoid();
    animator.humanoid = makeHumanoidAvatar(animator.skeleton);
    const int foot = animator.humanoid->joint(HumanoidBone::LeftFoot);
    AnimatorEvaluator::evaluate(animator);

    IKGoalState state;
    state.chain = "LeftFoot";
    state.position = glm::vec3(animator.globalTransforms[foot][3]);
    state.rotation = IKGoalState::Rotation::Normal;
    state.normal = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));   // a slope rising toward -Z
    animator.ikGoals.push_back(state);
    AnimatorEvaluator::update(registry, 0.0f);

    // The foot's up axis (its rest rotation is identity) now follows the normal.
    const glm::vec3 up = glm::normalize(glm::mat3(animator.globalTransforms[foot]) * glm::vec3(0.0f, 1.0f, 0.0f));
    TestHelpers::ExpectVec3Near(up, state.normal, 1e-4f);
}
