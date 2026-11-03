//
// AnimationControllerTest.cpp - Tests for controller parsing, the state
// machine, blend weights, layers, events and root motion
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/AnimationController.h"
#include "Animation/AnimatorEvaluator.h"
#include "Animation/ControllerEvaluator.h"
#include "ECS/Core.h"
#include "TestHelpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <string>

using namespace Shoonyakasha;

namespace {

// Hips -> Spine -> Arm, and Hips -> Leg. Hips 1 unit up.
std::shared_ptr<Skeleton> makeSkeleton() {
    auto skeleton = std::make_shared<Skeleton>();
    auto add = [&](const char* name, int parent, glm::vec3 t) {
        Joint joint;
        joint.name = name;
        joint.parentIndex = parent;
        joint.defaultTranslation = t;
        skeleton->joints.push_back(joint);
    };
    add("Hips", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    add("Spine", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    add("Arm", 1, glm::vec3(0.5f, 0.0f, 0.0f));
    add("Leg", 0, glm::vec3(0.0f, -0.5f, 0.0f));
    skeleton->buildNameLookup();
    skeleton->buildEvaluationOrder();
    return skeleton;
}

// A clip of `duration` seconds moving joint `joint` from `from` to `to`.
std::shared_ptr<AnimationClip> slide(const std::string& name, int joint, glm::vec3 from, glm::vec3 to,
                                     float duration = 1.0f) {
    auto clip = std::make_shared<AnimationClip>();
    clip->name = name;
    clip->duration = duration;
    AnimationChannel channel;
    channel.targetJointIndex = joint;
    channel.property = AnimationChannel::Property::Translation;
    channel.timestamps = {0.0f, duration};
    channel.values = {glm::vec4(from, 0.0f), glm::vec4(to, 0.0f)};
    clip->channels.push_back(channel);
    return clip;
}

// A clip holding joint `joint` at `at`.
std::shared_ptr<AnimationClip> hold(const std::string& name, int joint, glm::vec3 at, float duration = 1.0f) {
    return slide(name, joint, at, at, duration);
}

AnimatorComponent makeAnimator() {
    AnimatorComponent animator;
    animator.skeleton = makeSkeleton();
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    animator.clips = {
        hold("idle", 0, up + glm::vec3(0.0f, 0.0f, 0.0f)),
        hold("walk", 0, up + glm::vec3(1.0f, 0.0f, 0.0f), 2.0f),
        hold("run", 0, up + glm::vec3(3.0f, 0.0f, 0.0f), 0.5f),
        hold("jump", 0, up + glm::vec3(0.0f, 2.0f, 0.0f)),
        hold("wave", 2, glm::vec3(0.5f, 1.0f, 0.0f)),
    };
    return animator;
}

const char* kLocomotion = R"({
  "parameters": {
    "speed": {"type": "float"},
    "grounded": {"type": "bool", "default": true},
    "jump": {"type": "trigger"}
  },
  "layers": [{
    "name": "base",
    "states": {
      "locomotion": {"blend1d": {"parameter": "speed", "clips": [["idle", 0], ["walk", 1], ["run", 3]]}},
      "jump": {"clip": "jump", "loop": false, "events": [[0.0, "takeoff"], [0.5, "apex"]]},
      "fall": {"clip": "idle"}
    },
    "transitions": [
      {"from": "locomotion", "to": "jump", "duration": 0.2, "when": [["jump"]]},
      {"from": "jump", "to": "locomotion", "duration": 0.0, "exit_time": 0.9},
      {"from": "any", "to": "fall", "when": [["grounded", "==", false]]},
      {"from": "fall", "to": "locomotion", "when": [["grounded", "==", true]]}
    ]
  }]
})";

std::string parseError(const std::string& json) {
    try {
        parseAnimationController(json);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

void setParam(AnimatorComponent& animator, const char* name, float value) {
    auto& runtime = *animator.controller;
    runtime.parameters[runtime.definition->findParameter(name)] = value;
}

std::string currentState(const AnimatorComponent& animator, int layer = 0) {
    const auto& runtime = *animator.controller;
    const int state = runtime.layers[layer].current.state;
    return runtime.definition->layers[layer].states[state].name;
}

void tick(AnimatorComponent& animator, float dt) {
    AnimatorEvaluator::advance(animator, dt);
    AnimatorEvaluator::evaluate(animator);
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Parsing
// ═══════════════════════════════════════════════════════════════

TEST(AnimationControllerParse, ReadsLocomotion) {
    auto definition = parseAnimationController(kLocomotion);
    ASSERT_EQ(definition->parameters.size(), 3u);
    EXPECT_EQ(definition->parameters[1].type, ControllerParameterType::Bool);
    EXPECT_FLOAT_EQ(definition->parameters[1].defaultValue, 1.0f);
    ASSERT_EQ(definition->layers.size(), 1u);
    const auto& layer = definition->layers[0];
    ASSERT_EQ(layer.states.size(), 3u);
    EXPECT_EQ(layer.states[0].name, "locomotion");   // file order
    EXPECT_EQ(layer.states[0].motion.kind, ControllerMotion::Kind::Blend1D);
    EXPECT_EQ(layer.states[0].motion.children.size(), 3u);
    EXPECT_FALSE(layer.states[1].loop);
    ASSERT_EQ(layer.transitions.size(), 4u);
    EXPECT_EQ(layer.transitions[2].from, -1);
    EXPECT_FLOAT_EQ(layer.transitions[1].exitTime, 0.9f);
}

TEST(AnimationControllerParse, ErrorsNameThePath) {
    EXPECT_NE(parseError("{").find("invalid JSON"), std::string::npos);
    EXPECT_NE(parseError(R"({"layers": [{"name": "a", "states": {"s": {"clip": "c", "lop": true}}}]})")
                  .find("layers[0].states.s: unknown key 'lop'"), std::string::npos);
    EXPECT_NE(parseError(R"({"layers": [{"name": "a", "states": {"s": {"blend1d": {"parameter": "x", "clips": [["c", 0]]}}}}]})")
                  .find("layers[0].states.s.blend1d.parameter: no parameter 'x'"), std::string::npos);
    EXPECT_NE(parseError(R"({"parameters": {"x": {"type": "float"}},
        "layers": [{"name": "a", "states": {"s": {"blend1d": {"parameter": "x", "clips": [["c", 1], ["d", 0]]}}}}]})")
                  .find("thresholds must ascend"), std::string::npos);
    EXPECT_NE(parseError(R"({"layers": [{"name": "a", "states": {"s": {"clip": "c"}},
        "transitions": [{"from": "s", "to": "t", "exit_time": 0.5}]}]})")
                  .find("layers[0].transitions[0].to: no state 't'"), std::string::npos);
    EXPECT_NE(parseError(R"({"layers": [{"name": "a", "states": {"s": {"clip": "c"}},
        "transitions": [{"from": "s", "to": "s"}]}]})")
                  .find("needs conditions"), std::string::npos);
    EXPECT_NE(parseError(R"({"parameters": {"f": {"type": "float"}}, "layers": [{"name": "a", "states": {"s": {"clip": "c"}},
        "transitions": [{"from": "s", "to": "s", "when": [["f"]]}]}]})")
                  .find("needs a bool or trigger"), std::string::npos);
    EXPECT_NE(parseError(R"({"layers": [{"name": "a", "states": {"s": {"clip": "c", "blend1d": {}}}}]})")
                  .find("exactly one of"), std::string::npos);
}

TEST(AnimationControllerBind, ReportsMissingClipAndJoint) {
    AnimatorComponent animator = makeAnimator();
    auto missingClip = parseAnimationController(R"({"layers": [{"name": "a", "states": {"s": {"clip": "dance"}}}]})");
    try {
        ControllerEvaluator::bind(missingClip, animator);
        FAIL() << "expected an error";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "layers[0].states.s: no clip 'dance'");
    }
    auto missingJoint = parseAnimationController(
        R"({"layers": [{"name": "a", "mask": ["Tail"], "states": {"s": {"clip": "idle"}}}]})");
    EXPECT_THROW(ControllerEvaluator::bind(missingJoint, animator), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Blend weights
// ═══════════════════════════════════════════════════════════════

TEST(ControllerBlendWeights, OneDimensional) {
    const std::vector<float> thresholds = {0.0f, 1.0f, 3.0f};
    auto w = ControllerEvaluator::blend1DWeights(thresholds, -1.0f);
    EXPECT_FLOAT_EQ(w[0], 1.0f);
    w = ControllerEvaluator::blend1DWeights(thresholds, 2.0f);
    EXPECT_FLOAT_EQ(w[0], 0.0f);
    EXPECT_FLOAT_EQ(w[1], 0.5f);
    EXPECT_FLOAT_EQ(w[2], 0.5f);
    w = ControllerEvaluator::blend1DWeights(thresholds, 10.0f);
    EXPECT_FLOAT_EQ(w[2], 1.0f);
}

TEST(ControllerBlendWeights, TwoDimensionalFreeform) {
    // Idle at the centre, walks in four directions, a run straight ahead.
    const std::vector<glm::vec2> points = {
        {0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0}, {0, 3}};
    auto w = ControllerEvaluator::blend2DWeights(points, glm::vec2(0.0f, 1.0f));
    EXPECT_NEAR(w[1], 1.0f, 1e-5f);   // exactly on a sample point
    w = ControllerEvaluator::blend2DWeights(points, glm::vec2(0.0f, 2.0f));
    EXPECT_NEAR(w[1], 0.5f, 1e-5f);   // halfway from walk to run
    EXPECT_NEAR(w[5], 0.5f, 1e-5f);
    w = ControllerEvaluator::blend2DWeights(points, glm::vec2(0.5f, 0.5f));
    float total = 0.0f;
    for (float v : w) {
        EXPECT_GE(v, 0.0f);
        total += v;
    }
    EXPECT_NEAR(total, 1.0f, 1e-5f);
    EXPECT_NEAR(w[1], w[4], 1e-5f);    // symmetric between forward and right
    EXPECT_NEAR(w[2], 0.0f, 1e-5f);    // nothing from backward
}

// ═══════════════════════════════════════════════════════════════
// State machine
// ═══════════════════════════════════════════════════════════════

TEST(AnimationController, BlendTreeFollowsParameter) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    EXPECT_EQ(currentState(animator), "locomotion");

    setParam(animator, "speed", 2.0f);   // halfway from walk (x = 1) to run (x = 3)
    tick(animator, 0.0f);
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(2.0f, 1.0f, 0.0f));
}

TEST(AnimationController, BlendTreePhaseIsShared) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    setParam(animator, "speed", 2.0f);   // walk (2 s) and run (0.5 s) at weight 0.5: 1.25 s
    tick(animator, 0.625f);
    EXPECT_NEAR(animator.controller->layers[0].current.phase, 0.5f, 1e-5f);
}

TEST(AnimationController, TriggerTransitionFadesAndIsConsumed) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    tick(animator, 0.1f);
    setParam(animator, "jump", 1.0f);
    tick(animator, 0.0f);
    EXPECT_EQ(currentState(animator), "jump");
    EXPECT_TRUE(animator.controller->layers[0].isFading());
    EXPECT_FLOAT_EQ(animator.controller->parameters[2], 0.0f);   // trigger cleared

    tick(animator, 0.1f);   // halfway through the 0.2 s fade from idle (y = 1) to jump (y = 3)
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(0.0f, 2.0f, 0.0f), 1e-4f);
    tick(animator, 0.15f);
    EXPECT_FALSE(animator.controller->layers[0].isFading());
}

TEST(AnimationController, ExitTimeReturnsFromOneShot) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    setParam(animator, "jump", 1.0f);
    tick(animator, 0.0f);
    tick(animator, 0.5f);
    EXPECT_EQ(currentState(animator), "jump");
    tick(animator, 0.45f);   // phase 0.95 passes exit time 0.9
    EXPECT_EQ(currentState(animator), "locomotion");
}

TEST(AnimationController, AnyStateTransitionAndBack) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    setParam(animator, "grounded", 0.0f);
    tick(animator, 0.1f);
    EXPECT_EQ(currentState(animator), "fall");
    tick(animator, 0.1f);   // "any" does not re-enter the state it targets
    EXPECT_EQ(currentState(animator), "fall");
    EXPECT_EQ(animator.controller->layers[0].current.phase, 0.1f);
    setParam(animator, "grounded", 1.0f);
    tick(animator, 0.1f);
    EXPECT_EQ(currentState(animator), "locomotion");
}

TEST(AnimationController, EventsFireOncePerPass) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(kLocomotion), animator);
    setParam(animator, "jump", 1.0f);
    tick(animator, 0.0f);       // enters jump
    tick(animator, 0.25f);      // takeoff at 0 on entry
    ASSERT_EQ(animator.controller->events.size(), 1u);
    EXPECT_EQ(animator.controller->events[0], "takeoff");
    tick(animator, 0.25f);      // apex at 0.5
    ASSERT_EQ(animator.controller->events.size(), 2u);
    EXPECT_EQ(animator.controller->events[1], "apex");
    tick(animator, 0.1f);
    EXPECT_EQ(animator.controller->events.size(), 2u);
}

TEST(AnimationController, MaskedOverrideLayer) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(R"({
      "layers": [
        {"name": "base", "states": {"run": {"clip": "run"}}},
        {"name": "upper", "mask": ["Spine"], "weight": 0.5, "states": {"wave": {"clip": "wave"}}}
      ]})"), animator);
    tick(animator, 0.0f);
    // Hips come from the base layer only; Arm (under Spine) halfway to the wave.
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(3.0f, 1.0f, 0.0f));
    TestHelpers::ExpectVec3Near(animator.pose.translations[2], glm::vec3(0.5f, 0.5f, 0.0f));
    TestHelpers::ExpectVec3Near(animator.pose.translations[3], glm::vec3(0.0f, -0.5f, 0.0f));
}

TEST(AnimationController, AdditiveLayer) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(R"({
      "layers": [
        {"name": "base", "states": {"walk": {"clip": "walk"}}},
        {"name": "lean", "mode": "additive", "states": {"jump": {"clip": "jump"}}}
      ]})"), animator);
    tick(animator, 0.0f);
    // Walk puts Hips at (1, 1, 0); jump differs from rest by (0, 2, 0).
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(1.0f, 3.0f, 0.0f));
}

TEST(AnimationController, SpeedParameterScalesAdvance) {
    AnimatorComponent animator = makeAnimator();
    animator.controller = ControllerEvaluator::bind(parseAnimationController(R"({
      "parameters": {"rate": {"type": "float", "default": 0.5}},
      "layers": [{"name": "base", "states": {"walk": {"clip": "walk", "speed": 2.0, "speed_parameter": "rate"}}}]})"),
        animator);
    tick(animator, 1.0f);   // 2 s clip at speed 2 * 0.5
    EXPECT_NEAR(animator.controller->layers[0].current.phase, 0.5f, 1e-5f);
}

// ═══════════════════════════════════════════════════════════════
// Root motion
// ═══════════════════════════════════════════════════════════════

TEST(RootMotion, PlayHoldsJointInPlaceAndCollectsMovement) {
    AnimatorComponent animator = makeAnimator();
    animator.clips.push_back(slide("stride", 0, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.2f, 2.0f), 1.0f));
    AnimatorEvaluator::setRootMotion(animator, true);
    EXPECT_EQ(animator.rootMotionJoint, 0);
    animator.play(animator.findClip("stride"));

    tick(animator, 0.5f);
    TestHelpers::ExpectVec3Near(animator.pose.translations[0], glm::vec3(0.0f, 1.1f, 0.0f));   // Y kept
    TestHelpers::ExpectVec3Near(animator.rootMotionDelta, glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(RootMotion, LoopWrapCountsWholeCycles) {
    AnimatorComponent animator = makeAnimator();
    animator.clips.push_back(slide("stride", 0, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 2.0f), 1.0f));
    AnimatorEvaluator::setRootMotion(animator, true);
    animator.play(animator.findClip("stride"));
    tick(animator, 0.75f);
    tick(animator, 1.5f);   // 0.75 -> 2.25: rest of a cycle, one full cycle, a quarter
    TestHelpers::ExpectVec3Near(animator.rootMotionDelta, glm::vec3(0.0f, 0.0f, 3.0f), 1e-4f);
}

TEST(RootMotion, ControllerBlendWeightsTheMovement) {
    AnimatorComponent animator = makeAnimator();
    animator.clips = {
        slide("walk", 0, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 1.0f), 1.0f),
        slide("run", 0, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 3.0f), 1.0f),
    };
    AnimatorEvaluator::setRootMotion(animator, true);
    animator.controller = ControllerEvaluator::bind(parseAnimationController(R"({
      "parameters": {"speed": {"type": "float", "default": 0.5}},
      "layers": [{"name": "base", "states": {"move": {"blend1d": {"parameter": "speed",
                  "clips": [["walk", 0], ["run", 1]]}}}}]})"), animator);
    tick(animator, 0.5f);
    // Half a cycle of each, weighted 0.5: (0.5 + 1.5) / 2
    TestHelpers::ExpectVec3Near(animator.rootMotionDelta, glm::vec3(0.0f, 0.0f, 1.0f), 1e-4f);
    EXPECT_NEAR(animator.pose.translations[0].z, 0.0f, 1e-5f);
}

TEST(RootMotion, UpdateCollectsWorldMovement) {
    entt::registry registry;
    entt::entity root = registry.create();
    auto& transform = registry.emplace<ECS::TransformComponent>(root);
    transform.worldMatrix = glm::rotate(glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)),
                                        glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    auto& animator = registry.emplace<AnimatorComponent>(root, makeAnimator());
    animator.clips.push_back(slide("stride", 0, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 2.0f), 1.0f));
    AnimatorEvaluator::setRootMotion(animator, true);
    animator.play(animator.findClip("stride"));

    AnimatorEvaluator::update(registry, 0.5f);
    // (0, 0, 1) turned 90 degrees about Y and halved: (0.5, 0, 0)
    TestHelpers::ExpectVec3Near(registry.get<AnimatorComponent>(root).rootMotionWorld,
                                glm::vec3(0.5f, 0.0f, 0.0f), 1e-5f);
}
