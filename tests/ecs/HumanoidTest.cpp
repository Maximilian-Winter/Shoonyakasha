//
// HumanoidTest.cpp - Tests for humanoid bone mapping and retargeting
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/Humanoid.h"
#include "Animation/Retarget.h"
#include "Animation/Pose.h"
#include "Resources/AnimationClipLoader.h"
#include "TestHelpers.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <map>
#include <stdexcept>

using namespace Shoonyakasha;
using B = HumanoidBone;

namespace {

struct Spec {
    B bone;
    B parent;   // Count: the skeleton's root (or the extra root joint)
    glm::vec3 translation;
};

// A body facing +Z, Y up, left toward +X, hips 1 above the feet.
const std::vector<Spec> kBody = {
    {B::Hips, B::Count, {0.0f, 1.0f, 0.0f}},
    {B::Spine, B::Hips, {0.0f, 0.1f, 0.0f}},
    {B::Chest, B::Spine, {0.0f, 0.2f, 0.0f}},
    {B::Neck, B::Chest, {0.0f, 0.25f, 0.0f}},
    {B::Head, B::Neck, {0.0f, 0.1f, 0.0f}},
    {B::LeftShoulder, B::Chest, {0.05f, 0.2f, 0.0f}},
    {B::LeftUpperArm, B::LeftShoulder, {0.1f, 0.0f, 0.0f}},
    {B::LeftLowerArm, B::LeftUpperArm, {0.25f, 0.0f, 0.0f}},
    {B::LeftHand, B::LeftLowerArm, {0.25f, 0.0f, 0.0f}},
    {B::RightShoulder, B::Chest, {-0.05f, 0.2f, 0.0f}},
    {B::RightUpperArm, B::RightShoulder, {-0.1f, 0.0f, 0.0f}},
    {B::RightLowerArm, B::RightUpperArm, {-0.25f, 0.0f, 0.0f}},
    {B::RightHand, B::RightLowerArm, {-0.25f, 0.0f, 0.0f}},
    {B::LeftUpperLeg, B::Hips, {0.1f, -0.05f, 0.0f}},
    {B::LeftLowerLeg, B::LeftUpperLeg, {0.0f, -0.45f, 0.0f}},
    {B::LeftFoot, B::LeftLowerLeg, {0.0f, -0.5f, 0.0f}},
    {B::RightUpperLeg, B::Hips, {-0.1f, -0.05f, 0.0f}},
    {B::RightLowerLeg, B::RightUpperLeg, {0.0f, -0.45f, 0.0f}},
    {B::RightFoot, B::RightLowerLeg, {0.0f, -0.5f, 0.0f}},
};

std::string mixamoName(B bone) {
    static const std::map<B, std::string> names = {
        {B::Hips, "Hips"}, {B::Spine, "Spine"}, {B::Chest, "Spine1"}, {B::Neck, "Neck"}, {B::Head, "Head"},
        {B::LeftShoulder, "LeftShoulder"}, {B::LeftUpperArm, "LeftArm"}, {B::LeftLowerArm, "LeftForeArm"},
        {B::LeftHand, "LeftHand"}, {B::RightShoulder, "RightShoulder"}, {B::RightUpperArm, "RightArm"},
        {B::RightLowerArm, "RightForeArm"}, {B::RightHand, "RightHand"}, {B::LeftUpperLeg, "LeftUpLeg"},
        {B::LeftLowerLeg, "LeftLeg"}, {B::LeftFoot, "LeftFoot"}, {B::RightUpperLeg, "RightUpLeg"},
        {B::RightLowerLeg, "RightLeg"}, {B::RightFoot, "RightFoot"}};
    return names.at(bone);
}

std::string numberedName(B bone) { return "joint_" + std::to_string(static_cast<int>(bone)); }

// Build kBody with names from `name`. With `aPose`, the upper arms hang 45
// degrees down. `root`, when not identity, is an extra root joint "Root" above
// the hips (for a turned or scaled skeleton space).
std::shared_ptr<Skeleton> makeBody(const std::function<std::string(B)>& name, bool aPose = false,
                                   glm::quat rootRotation = glm::quat(1, 0, 0, 0), float rootScale = 1.0f,
                                   bool withRoot = false) {
    auto skeleton = std::make_shared<Skeleton>();
    std::map<B, int> index;
    if (withRoot) {
        Joint root;
        root.name = "Root";
        root.defaultRotation = rootRotation;
        root.defaultScale = glm::vec3(rootScale);
        skeleton->joints.push_back(root);
    }
    for (const Spec& spec : kBody) {
        Joint joint;
        joint.name = name(spec.bone);
        joint.parentIndex = spec.parent == B::Count ? (withRoot ? 0 : -1) : index.at(spec.parent);
        joint.defaultTranslation = spec.translation;
        if (aPose && spec.bone == B::LeftUpperArm) {
            joint.defaultRotation = glm::angleAxis(-glm::quarter_pi<float>(), glm::vec3(0, 0, 1));
        }
        if (aPose && spec.bone == B::RightUpperArm) {
            joint.defaultRotation = glm::angleAxis(glm::quarter_pi<float>(), glm::vec3(0, 0, 1));
        }
        index[spec.bone] = static_cast<int>(skeleton->joints.size());
        skeleton->joints.push_back(joint);
    }
    skeleton->buildNameLookup();
    skeleton->buildEvaluationOrder();
    return skeleton;
}

std::vector<std::pair<std::string, std::string>> numberedMapping() {
    std::vector<std::pair<std::string, std::string>> mapping;
    for (const Spec& spec : kBody) mapping.emplace_back(humanoidBoneName(spec.bone), numberedName(spec.bone));
    return mapping;
}

// A clip turning `bone`'s joint to `rotation` (local) and moving the hips by
// `hipsMove`, constant over one second.
std::shared_ptr<AnimationClip> poseClip(const Skeleton& skeleton, const HumanoidAvatar& avatar, B bone,
                                        glm::quat rotation, glm::vec3 hipsMove = glm::vec3(0.0f)) {
    auto clip = std::make_shared<AnimationClip>();
    clip->name = "pose";
    clip->duration = 1.0f;
    AnimationChannel r;
    r.targetJointIndex = avatar.joint(bone);
    r.property = AnimationChannel::Property::Rotation;
    r.timestamps = {0.0f, 1.0f};
    r.values = {glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w),
                glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w)};
    clip->channels.push_back(r);
    AnimationChannel t;
    t.targetJointIndex = avatar.joint(B::Hips);
    t.property = AnimationChannel::Property::Translation;
    const glm::vec3 hips = skeleton.joints[avatar.joint(B::Hips)].defaultTranslation + hipsMove;
    t.timestamps = {0.0f, 1.0f};
    t.values = {glm::vec4(hips, 0.0f), glm::vec4(hips, 0.0f)};
    clip->channels.push_back(t);
    return clip;
}

// Direction from `bone` to `next` in the avatar's body frame, for a clip at 0.5 s.
glm::vec3 bodyDirection(const HumanoidAvatar& avatar, const AnimationClip& clip, B bone, B next) {
    Pose pose;
    std::vector<glm::mat4> globals;
    PoseOps::sample(clip, 0.5f, *avatar.skeleton, pose);
    PoseOps::globalTransforms(*avatar.skeleton, pose, globals);
    const glm::vec3 d = glm::vec3(globals[avatar.joint(next)][3]) - glm::vec3(globals[avatar.joint(bone)][3]);
    return glm::normalize(avatar.bodyFrame * d);
}

// Hips position relative to rest, in body frame, in hip heights.
glm::vec3 bodyHipsMove(const HumanoidAvatar& avatar, const AnimationClip& clip) {
    Pose pose;
    std::vector<glm::mat4> globals;
    PoseOps::sample(clip, 0.5f, *avatar.skeleton, pose);
    PoseOps::globalTransforms(*avatar.skeleton, pose, globals);
    const int hips = avatar.joint(B::Hips);
    const glm::vec3 d = glm::vec3(globals[hips][3]) - glm::vec3(avatar.restGlobals[hips][3]);
    return avatar.bodyFrame * d / avatar.hipHeight;
}

std::string errorOf(const std::function<void()>& f) {
    try {
        f();
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

} // namespace

// ═══════════════════════════════════════════════════════════════
// Bones
// ═══════════════════════════════════════════════════════════════

TEST(HumanoidBones, NamesRoundTripAndParents) {
    for (int b = 0; b < kHumanoidBoneCount; ++b) {
        HumanoidBone bone;
        ASSERT_TRUE(humanoidBoneFromName(humanoidBoneName(static_cast<B>(b)), bone));
        EXPECT_EQ(static_cast<int>(bone), b);
    }
    EXPECT_EQ(humanoidBoneParent(B::LeftHand), B::LeftLowerArm);
    EXPECT_EQ(humanoidBoneParent(B::RightIndexIntermediate), B::RightIndexProximal);
    EXPECT_EQ(humanoidBoneParent(B::LeftThumbProximal), B::LeftHand);
    EXPECT_EQ(humanoidBoneParent(B::Hips), B::Count);
    EXPECT_TRUE(humanoidBoneRequired(B::RightFoot));
    EXPECT_FALSE(humanoidBoneRequired(B::Chest));
}

// ═══════════════════════════════════════════════════════════════
// Mapping
// ═══════════════════════════════════════════════════════════════

TEST(HumanoidMapping, MixamoPrefixes) {
    for (const char* prefix : {"mixamorig:", "mixamorig_", "mixamorig1:"}) {
        auto skeleton = makeBody([&](B bone) { return std::string(prefix) + mixamoName(bone); });
        const auto joints = autoMapHumanoid(*skeleton);
        for (const Spec& spec : kBody) {
            const int j = joints[static_cast<int>(spec.bone)];
            ASSERT_GE(j, 0) << prefix << humanoidBoneName(spec.bone);
            EXPECT_EQ(skeleton->joints[j].name, prefix + mixamoName(spec.bone));
        }
    }
}

TEST(HumanoidMapping, UnrealMannequin) {
    static const std::map<B, std::string> names = {
        {B::Hips, "pelvis"}, {B::Spine, "spine_01"}, {B::Chest, "spine_02"}, {B::Neck, "neck_01"}, {B::Head, "head"},
        {B::LeftShoulder, "clavicle_l"}, {B::LeftUpperArm, "upperarm_l"}, {B::LeftLowerArm, "lowerarm_l"},
        {B::LeftHand, "hand_l"}, {B::RightShoulder, "clavicle_r"}, {B::RightUpperArm, "upperarm_r"},
        {B::RightLowerArm, "lowerarm_r"}, {B::RightHand, "hand_r"}, {B::LeftUpperLeg, "thigh_l"},
        {B::LeftLowerLeg, "calf_l"}, {B::LeftFoot, "foot_l"}, {B::RightUpperLeg, "thigh_r"},
        {B::RightLowerLeg, "calf_r"}, {B::RightFoot, "foot_r"}};
    auto skeleton = makeBody([](B bone) { return names.at(bone); });
    const auto joints = autoMapHumanoid(*skeleton);
    for (const Spec& spec : kBody) {
        ASSERT_GE(joints[static_cast<int>(spec.bone)], 0) << humanoidBoneName(spec.bone);
        EXPECT_EQ(skeleton->joints[joints[static_cast<int>(spec.bone)]].name, names.at(spec.bone));
    }
}

TEST(HumanoidMapping, NamePartsAndSpineChain) {
    static const std::map<B, std::string> names = {
        {B::Hips, "Pelvis"}, {B::Spine, "Torso1"}, {B::Chest, "Torso2"}, {B::Neck, "Neck"}, {B::Head, "Head"},
        {B::LeftShoulder, "Clavicle.L"}, {B::LeftUpperArm, "UpperArm.L"}, {B::LeftLowerArm, "Forearm.L"},
        {B::LeftHand, "Hand.L"}, {B::RightShoulder, "Clavicle.R"}, {B::RightUpperArm, "UpperArm.R"},
        {B::RightLowerArm, "Forearm.R"}, {B::RightHand, "Hand.R"}, {B::LeftUpperLeg, "Thigh.L"},
        {B::LeftLowerLeg, "Shin.L"}, {B::LeftFoot, "Foot.L"}, {B::RightUpperLeg, "Thigh.R"},
        {B::RightLowerLeg, "Shin.R"}, {B::RightFoot, "Foot.R"}};
    auto skeleton = makeBody([](B bone) { return names.at(bone); });
    const auto joints = autoMapHumanoid(*skeleton);
    for (const Spec& spec : kBody) {
        ASSERT_GE(joints[static_cast<int>(spec.bone)], 0) << humanoidBoneName(spec.bone);
        EXPECT_EQ(skeleton->joints[joints[static_cast<int>(spec.bone)]].name, names.at(spec.bone));
    }
}

TEST(HumanoidMapping, NumberedNamesNeedAMapping) {
    auto skeleton = makeBody(numberedName);
    const std::string error = errorOf([&] { makeHumanoidAvatar(skeleton); });
    EXPECT_NE(error.find("humanoid: no joint for Hips, Spine, Head"), std::string::npos) << error;

    auto avatar = makeHumanoidAvatar(skeleton, numberedMapping());
    EXPECT_EQ(avatar->joint(B::LeftHand), skeleton->findJoint(numberedName(B::LeftHand)));
    EXPECT_EQ(humanoidMapping(*avatar).size(), kBody.size());

    EXPECT_NE(errorOf([&] { makeHumanoidAvatar(skeleton, {{"Tail", "joint_0"}}); }).find("no bone 'Tail'"),
              std::string::npos);
    EXPECT_NE(errorOf([&] { makeHumanoidAvatar(skeleton, {{"Hips", "nope"}}); }).find("Hips: no joint 'nope'"),
              std::string::npos);
}

TEST(HumanoidAvatar, BodyFrameAndHipHeight) {
    auto avatar = makeHumanoidAvatar(makeBody(mixamoName));
    // Facing +Z, Y up, left +X: the body frame is the skeleton space.
    const glm::quat identity(1, 0, 0, 0);
    EXPECT_NEAR(std::abs(glm::dot(avatar->bodyFrame, identity)), 1.0f, 1e-5f);
    EXPECT_NEAR(avatar->hipHeight, 1.0f, 1e-5f);

    // Lying on its back (Z up in skeleton space), 100 times larger.
    auto turned = makeHumanoidAvatar(
        makeBody(mixamoName, false, glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0)), 100.0f, true));
    TestHelpers::ExpectVec3Near(turned->bodyFrame * glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), 1e-5f);
    EXPECT_NEAR(turned->hipHeight, 100.0f, 1e-3f);
}

// ═══════════════════════════════════════════════════════════════
// Retargeting
// ═══════════════════════════════════════════════════════════════

TEST(Retarget, OntoItselfKeepsThePose) {
    auto skeleton = makeBody(mixamoName);
    auto avatar = makeHumanoidAvatar(skeleton);
    const glm::quat raise = glm::angleAxis(0.7f, glm::normalize(glm::vec3(0.2f, 0.3f, 1.0f)));
    auto clip = poseClip(*skeleton, *avatar, B::LeftUpperArm, raise, glm::vec3(0.1f, -0.05f, 0.2f));

    auto result = retargetClip(*clip, *avatar, *avatar);
    EXPECT_EQ(result->name, "pose");
    EXPECT_FLOAT_EQ(result->duration, 1.0f);

    Pose a, b;
    PoseOps::sample(*clip, 0.5f, *skeleton, a);
    PoseOps::sample(*result, 0.5f, *skeleton, b);
    for (uint32_t j = 0; j < skeleton->jointCount(); ++j) {
        EXPECT_NEAR(std::abs(glm::dot(a.rotations[j], b.rotations[j])), 1.0f, 1e-5f) << skeleton->joints[j].name;
        TestHelpers::ExpectVec3Near(a.translations[j], b.translations[j], 1e-5f);
    }
}

TEST(Retarget, AcrossTurnedAndScaledSkeleton) {
    auto source = makeHumanoidAvatar(makeBody(mixamoName));
    auto targetSkeleton = makeBody(numberedName, false, glm::angleAxis(glm::half_pi<float>(), glm::vec3(1, 0, 0)),
                                   100.0f, true);
    auto target = makeHumanoidAvatar(targetSkeleton, numberedMapping());

    const glm::quat raise = glm::angleAxis(1.0f, glm::vec3(0, 0, 1));
    auto clip = poseClip(*source->skeleton, *source, B::LeftUpperArm, raise, glm::vec3(0.0f, 0.0f, 0.3f));
    auto result = retargetClip(*clip, *source, *target);

    TestHelpers::ExpectVec3Near(bodyDirection(*target, *result, B::LeftUpperArm, B::LeftLowerArm),
                                bodyDirection(*source, *clip, B::LeftUpperArm, B::LeftLowerArm), 1e-4f);
    TestHelpers::ExpectVec3Near(bodyDirection(*target, *result, B::LeftLowerLeg, B::LeftFoot),
                                bodyDirection(*source, *clip, B::LeftLowerLeg, B::LeftFoot), 1e-4f);
    // The hips move 0.3 hip heights forward in both.
    TestHelpers::ExpectVec3Near(bodyHipsMove(*target, *result), glm::vec3(0.0f, 0.0f, 0.3f), 1e-4f);
}

TEST(Retarget, APoseSourceOntoTPoseTarget) {
    auto source = makeHumanoidAvatar(makeBody(mixamoName, true));
    auto target = makeHumanoidAvatar(makeBody(numberedName, false), numberedMapping());

    // The source at rest: the target's arms hang as the source's do.
    const glm::quat aPoseArm = source->skeleton->joints[source->joint(B::LeftUpperArm)].defaultRotation;
    auto rest = poseClip(*source->skeleton, *source, B::LeftUpperArm, aPoseArm);
    auto atRest = retargetClip(*rest, *source, *target);
    TestHelpers::ExpectVec3Near(bodyDirection(*target, *atRest, B::LeftUpperArm, B::LeftLowerArm),
                                glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f)), 1e-4f);
    TestHelpers::ExpectVec3Near(bodyDirection(*target, *atRest, B::RightUpperArm, B::RightLowerArm),
                                glm::normalize(glm::vec3(-1.0f, -1.0f, 0.0f)), 1e-4f);

    // The source raising its arm to horizontal: the target's is horizontal too.
    auto raised = poseClip(*source->skeleton, *source, B::LeftUpperArm, glm::quat(1, 0, 0, 0));
    auto atT = retargetClip(*raised, *source, *target);
    TestHelpers::ExpectVec3Near(bodyDirection(*target, *atT, B::LeftUpperArm, B::LeftLowerArm),
                                glm::vec3(1.0f, 0.0f, 0.0f), 1e-4f);
}

TEST(Retarget, CesiumManWalkOntoBody) {
    const std::filesystem::path path = std::filesystem::path(SHOONYAKASHA_SOURCE_DIR)
        / "glTF-Sample-Assets/Models/CesiumMan/glTF-Binary/CesiumMan.glb";
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "missing " << path.string();

    AnimationFile file = loadAnimationFile(path);
    ASSERT_EQ(file.skeleton->jointCount(), 19u);
    ASSERT_EQ(file.clips.size(), 1u);
    auto cesium = makeHumanoidAvatar(file.skeleton, {
        {"Hips", "Skeleton_torso_joint_1"}, {"Spine", "Skeleton_torso_joint_2"}, {"Chest", "torso_joint_3"},
        {"Neck", "Skeleton_neck_joint_1"}, {"Head", "Skeleton_neck_joint_2"},
        {"LeftUpperArm", "Skeleton_arm_joint_L__4_"}, {"LeftLowerArm", "Skeleton_arm_joint_L__3_"},
        {"LeftHand", "Skeleton_arm_joint_L__2_"}, {"RightUpperArm", "Skeleton_arm_joint_R"},
        {"RightLowerArm", "Skeleton_arm_joint_R__2_"}, {"RightHand", "Skeleton_arm_joint_R__3_"},
        {"LeftUpperLeg", "leg_joint_L_1"}, {"LeftLowerLeg", "leg_joint_L_2"}, {"LeftFoot", "leg_joint_L_3"},
        {"LeftToes", "leg_joint_L_5"}, {"RightUpperLeg", "leg_joint_R_1"}, {"RightLowerLeg", "leg_joint_R_2"},
        {"RightFoot", "leg_joint_R_3"}, {"RightToes", "leg_joint_R_5"}});
    auto body = makeHumanoidAvatar(makeBody(mixamoName));

    auto walk = retargetClip(*file.clips[0], *cesium, *body);
    EXPECT_FLOAT_EQ(walk->duration, file.clips[0]->duration);
    // Thighs swing in step: at every time the body's legs point as CesiumMan's do.
    for (float t : {0.2f, 0.6f, 1.0f}) {
        Pose pose;
        std::vector<glm::mat4> g;
        auto dir = [&](const HumanoidAvatar& a, const AnimationClip& c, B from, B to) {
            PoseOps::sample(c, t, *a.skeleton, pose);
            PoseOps::globalTransforms(*a.skeleton, pose, g);
            return glm::normalize(a.bodyFrame * (glm::vec3(g[a.joint(to)][3]) - glm::vec3(g[a.joint(from)][3])));
        };
        TestHelpers::ExpectVec3Near(dir(*body, *walk, B::LeftUpperLeg, B::LeftLowerLeg),
                                    dir(*cesium, *file.clips[0], B::LeftUpperLeg, B::LeftLowerLeg), 1e-3f);
        TestHelpers::ExpectVec3Near(dir(*body, *walk, B::RightLowerArm, B::RightHand),
                                    dir(*cesium, *file.clips[0], B::RightLowerArm, B::RightHand), 1e-3f);
    }
}
