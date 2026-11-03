//
// PoseTest.cpp - Tests for Pose, PoseOps, evaluation order and clip binding
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/Pose.h"
#include "Animation/AnimationEvaluator.h"
#include "Animation/ClipBinding.h"
#include "TestHelpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using namespace Shoonyakasha;

namespace {

Joint makeJoint(const std::string& name, int parent, glm::vec3 translation) {
    Joint joint;
    joint.name = name;
    joint.parentIndex = parent;
    joint.defaultTranslation = translation;
    return joint;
}

// Root -> Mid -> Tip, each one unit above its parent.
Skeleton makeChain() {
    Skeleton skeleton;
    skeleton.joints.push_back(makeJoint("Root", -1, glm::vec3(0.0f)));
    skeleton.joints.push_back(makeJoint("Mid", 0, glm::vec3(0.0f, 1.0f, 0.0f)));
    skeleton.joints.push_back(makeJoint("Tip", 1, glm::vec3(0.0f, 1.0f, 0.0f)));
    skeleton.buildNameLookup();
    return skeleton;
}

AnimationChannel rotationChannel(int joint, const std::string& name, glm::quat a, glm::quat b) {
    AnimationChannel channel;
    channel.targetJointIndex = joint;
    channel.targetJointName = name;
    channel.property = AnimationChannel::Property::Rotation;
    channel.timestamps = {0.0f, 1.0f};
    channel.values = {glm::vec4(a.x, a.y, a.z, a.w), glm::vec4(b.x, b.y, b.z, b.w)};
    return channel;
}

AnimationChannel translationChannel(int joint, const std::string& name, glm::vec3 a, glm::vec3 b,
                                    float end = 1.0f) {
    AnimationChannel channel;
    channel.targetJointIndex = joint;
    channel.targetJointName = name;
    channel.property = AnimationChannel::Property::Translation;
    channel.timestamps = {0.0f, end};
    channel.values = {glm::vec4(a, 0.0f), glm::vec4(b, 0.0f)};
    return channel;
}

void expectQuatNear(const glm::quat& a, const glm::quat& b, float epsilon = 1e-5f) {
    // q and -q are the same rotation.
    EXPECT_NEAR(std::abs(glm::dot(a, b)), 1.0f, epsilon);
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Evaluation order
// ═══════════════════════════════════════════════════════════════

TEST(SkeletonEvaluationOrder, ParentsComeBeforeChildren) {
    Skeleton skeleton;
    // Children listed before their parents: Tip(0) <- Mid(2) <- Root(1)
    skeleton.joints.push_back(makeJoint("Tip", 2, glm::vec3(0.0f, 1.0f, 0.0f)));
    skeleton.joints.push_back(makeJoint("Root", -1, glm::vec3(0.0f)));
    skeleton.joints.push_back(makeJoint("Mid", 1, glm::vec3(0.0f, 1.0f, 0.0f)));
    skeleton.buildEvaluationOrder();

    ASSERT_EQ(skeleton.evaluationOrder.size(), 3u);
    EXPECT_EQ(skeleton.evaluationOrder[0], 1);
    EXPECT_EQ(skeleton.evaluationOrder[1], 2);
    EXPECT_EQ(skeleton.evaluationOrder[2], 0);
}

TEST(SkeletonEvaluationOrder, CycleDoesNotHang) {
    Skeleton skeleton;
    skeleton.joints.push_back(makeJoint("A", 1, glm::vec3(0.0f)));
    skeleton.joints.push_back(makeJoint("B", 0, glm::vec3(0.0f)));
    skeleton.buildEvaluationOrder();
    EXPECT_EQ(skeleton.evaluationOrder.size(), 2u);
}

TEST(SkeletonEvaluationOrder, BoneMatricesWithChildBeforeParent) {
    Skeleton skeleton;
    skeleton.joints.push_back(makeJoint("Tip", 2, glm::vec3(0.0f, 1.0f, 0.0f)));
    skeleton.joints.push_back(makeJoint("Root", -1, glm::vec3(0.0f, 0.0f, 5.0f)));
    skeleton.joints.push_back(makeJoint("Mid", 1, glm::vec3(0.0f, 1.0f, 0.0f)));

    std::vector<glm::vec3> t, s;
    std::vector<glm::quat> r;
    AnimationEvaluator::setDefaultPose(skeleton, t, r, s);
    std::vector<glm::mat4> bones;
    AnimationEvaluator::computeBoneMatrices(skeleton, t, r, s, bones);

    // Tip sits at Root + Mid + Tip translations.
    TestHelpers::ExpectVec3Near(glm::vec3(bones[0][3]), glm::vec3(0.0f, 2.0f, 5.0f));
}

// ═══════════════════════════════════════════════════════════════
// Sampling and global transforms
// ═══════════════════════════════════════════════════════════════

TEST(PoseOps, SampleStartsFromRestPose) {
    Skeleton skeleton = makeChain();
    AnimationClip clip;
    clip.duration = 1.0f;
    clip.channels.push_back(translationChannel(0, "Root", glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f)));

    Pose pose;
    PoseOps::sample(clip, 0.5f, skeleton, pose);

    ASSERT_EQ(pose.jointCount(), 3u);
    TestHelpers::ExpectVec3Near(pose.translations[0], glm::vec3(1.0f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(pose.translations[1], glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST(PoseOps, GlobalTransformsComposeDownTheChain) {
    Skeleton skeleton = makeChain();
    Pose pose;
    pose.setRest(skeleton);
    // Mid turns 90 degrees about Z: Tip moves to -X of Mid.
    pose.rotations[1] = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));

    std::vector<glm::mat4> globals;
    PoseOps::globalTransforms(skeleton, pose, globals);

    TestHelpers::ExpectVec3Near(glm::vec3(globals[1][3]), glm::vec3(0.0f, 1.0f, 0.0f));
    TestHelpers::ExpectVec3Near(glm::vec3(globals[2][3]), glm::vec3(-1.0f, 1.0f, 0.0f));
}

TEST(PoseOps, BoneMatricesMultiplyInverseBind) {
    Skeleton skeleton = makeChain();
    skeleton.joints[2].inverseBindMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.0f, 0.0f));
    Pose pose;
    pose.setRest(skeleton);

    std::vector<glm::mat4> globals, bones;
    PoseOps::globalTransforms(skeleton, pose, globals);
    PoseOps::boneMatrices(skeleton, globals, bones);

    // At rest, a bone whose inverse bind matches its rest transform is identity.
    TestHelpers::ExpectMat4Near(bones[2], glm::mat4(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Blending
// ═══════════════════════════════════════════════════════════════

TEST(PoseOps, BlendEndpointsAndMidpoint) {
    Pose a, b, out;
    a.setIdentity(1);
    b.setIdentity(1);
    b.translations[0] = glm::vec3(4.0f, 0.0f, 0.0f);
    b.rotations[0] = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    b.scales[0] = glm::vec3(3.0f);

    PoseOps::blend(a, b, 0.0f, out);
    TestHelpers::ExpectVec3Near(out.translations[0], glm::vec3(0.0f));
    PoseOps::blend(a, b, 1.0f, out);
    expectQuatNear(out.rotations[0], b.rotations[0]);

    PoseOps::blend(a, b, 0.5f, out);
    TestHelpers::ExpectVec3Near(out.translations[0], glm::vec3(2.0f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(out.scales[0], glm::vec3(2.0f));
    expectQuatNear(out.rotations[0], glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST(PoseOps, BlendTakesShorterArc) {
    Pose a, b, out;
    a.setIdentity(1);
    b.setIdentity(1);
    // The same rotation as a small turn, stored with negative w.
    const glm::quat small = glm::angleAxis(0.2f, glm::vec3(0.0f, 1.0f, 0.0f));
    b.rotations[0] = -small;

    PoseOps::blend(a, b, 0.5f, out);
    expectQuatNear(out.rotations[0], glm::angleAxis(0.1f, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST(PoseOps, BlendOutputMayAliasInput) {
    Pose a, b;
    a.setIdentity(1);
    b.setIdentity(1);
    b.translations[0] = glm::vec3(2.0f, 0.0f, 0.0f);
    PoseOps::blend(a, b, 0.5f, a);
    TestHelpers::ExpectVec3Near(a.translations[0], glm::vec3(1.0f, 0.0f, 0.0f));
}

TEST(PoseOps, BlendPerJointUsesEachWeight) {
    Pose a, b, out;
    a.setIdentity(3);
    b.setIdentity(3);
    for (auto& t : b.translations) t = glm::vec3(1.0f, 0.0f, 0.0f);

    PoseOps::blendPerJoint(a, b, {1.0f, 0.25f}, out);
    TestHelpers::ExpectVec3Near(out.translations[0], glm::vec3(1.0f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(out.translations[1], glm::vec3(0.25f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(out.translations[2], glm::vec3(0.0f));  // past the weights: 0
}

TEST(PoseOps, AdditiveRoundTrip) {
    Pose reference, pose, delta, base, out;
    reference.setIdentity(1);
    reference.translations[0] = glm::vec3(0.0f, 1.0f, 0.0f);
    reference.rotations[0] = glm::angleAxis(0.3f, glm::vec3(1.0f, 0.0f, 0.0f));
    reference.scales[0] = glm::vec3(2.0f);
    pose = reference;
    pose.translations[0] += glm::vec3(0.5f, 0.0f, 0.0f);
    pose.rotations[0] = reference.rotations[0] * glm::angleAxis(0.4f, glm::vec3(0.0f, 0.0f, 1.0f));
    pose.scales[0] = glm::vec3(4.0f);

    PoseOps::makeAdditive(pose, reference, delta);
    TestHelpers::ExpectVec3Near(delta.translations[0], glm::vec3(0.5f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(delta.scales[0], glm::vec3(2.0f));

    // Applying the full delta to the reference gives the pose back.
    PoseOps::applyAdditive(reference, delta, 1.0f, out);
    TestHelpers::ExpectVec3Near(out.translations[0], pose.translations[0]);
    expectQuatNear(out.rotations[0], pose.rotations[0]);
    TestHelpers::ExpectVec3Near(out.scales[0], pose.scales[0]);

    // Weight 0 leaves the base as it is.
    base.setIdentity(1);
    PoseOps::applyAdditive(base, delta, 0.0f, out);
    TestHelpers::ExpectVec3Near(out.translations[0], glm::vec3(0.0f));
    expectQuatNear(out.rotations[0], glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    TestHelpers::ExpectVec3Near(out.scales[0], glm::vec3(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// Clip binding
// ═══════════════════════════════════════════════════════════════

TEST(ClipBinding, MapsChannelsByJointName) {
    Skeleton skeleton = makeChain();
    AnimationClip clip;
    clip.name = "wave";
    clip.channels.push_back(translationChannel(-1, "Tip", glm::vec3(0.0f), glm::vec3(1.0f), 2.0f));
    clip.channels.push_back(rotationChannel(-1, "Mid", glm::quat(1, 0, 0, 0), glm::quat(1, 0, 0, 0)));

    auto bound = bindClip(clip, skeleton);
    ASSERT_EQ(bound->channels.size(), 2u);
    EXPECT_EQ(bound->channels[0].targetJointIndex, 2);
    EXPECT_EQ(bound->channels[1].targetJointIndex, 1);
    EXPECT_EQ(bound->name, "wave");
    EXPECT_FLOAT_EQ(bound->duration, 2.0f);
}

TEST(ClipBinding, DropsUnknownJointsAndTheirDuration) {
    Skeleton skeleton = makeChain();
    AnimationClip clip;
    clip.channels.push_back(translationChannel(-1, "Root", glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
    clip.channels.push_back(translationChannel(-1, "Tail", glm::vec3(0.0f), glm::vec3(1.0f), 5.0f));
    clip.weightChannels.push_back(MorphWeightChannel{});

    auto bound = bindClip(clip, skeleton);
    ASSERT_EQ(bound->channels.size(), 1u);
    EXPECT_EQ(bound->channels[0].targetJointName, "Root");
    EXPECT_FLOAT_EQ(bound->duration, 1.0f);
    EXPECT_TRUE(bound->weightChannels.empty());
}
