//
// AnimationEvaluatorTest.cpp - Tests for keyframe sampling and bone matrix computation
//
// Tier 2: CPU-side animation evaluation — no GPU
//

#include <gtest/gtest.h>
#include "Animation/AnimationEvaluator.h"
#include "TestHelpers.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// Helper: Create a simple 2-joint skeleton (root + child)
// ═══════════════════════════════════════════════════════════════

static std::shared_ptr<Skeleton> makeTwoJointSkeleton() {
    auto skel = std::make_shared<Skeleton>();

    Joint root;
    root.name = "Root";
    root.parentIndex = -1;
    root.defaultTranslation = glm::vec3(0.0f);
    root.defaultRotation = glm::quat(1, 0, 0, 0);
    root.defaultScale = glm::vec3(1.0f);
    skel->joints.push_back(root);

    Joint child;
    child.name = "Child";
    child.parentIndex = 0;
    child.defaultTranslation = glm::vec3(0.0f, 1.0f, 0.0f);  // 1 unit up from root
    child.defaultRotation = glm::quat(1, 0, 0, 0);
    child.defaultScale = glm::vec3(1.0f);
    skel->joints.push_back(child);

    skel->buildNameLookup();
    return skel;
}

// ═══════════════════════════════════════════════════════════════
// setDefaultPose() Tests
// ═══════════════════════════════════════════════════════════════

TEST(AnimationEvaluator, SetDefaultPose_SizesMatchJointCount) {
    auto skel = makeTwoJointSkeleton();
    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;

    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    EXPECT_EQ(translations.size(), 2u);
    EXPECT_EQ(rotations.size(), 2u);
    EXPECT_EQ(scales.size(), 2u);
}

TEST(AnimationEvaluator, SetDefaultPose_ValuesMatchJointDefaults) {
    auto skel = makeTwoJointSkeleton();
    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;

    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    // Root: default translation (0,0,0)
    TestHelpers::ExpectVec3Near(translations[0], glm::vec3(0.0f));
    // Child: default translation (0,1,0)
    TestHelpers::ExpectVec3Near(translations[1], glm::vec3(0.0f, 1.0f, 0.0f));

    // Scales should all be (1,1,1)
    TestHelpers::ExpectVec3Near(scales[0], glm::vec3(1.0f));
    TestHelpers::ExpectVec3Near(scales[1], glm::vec3(1.0f));
}

// ═══════════════════════════════════════════════════════════════
// sampleClip() Tests
// ═══════════════════════════════════════════════════════════════

static AnimationClip makeTranslationClip(int jointIndex,
                                          const std::vector<float>& times,
                                          const std::vector<glm::vec3>& positions,
                                          AnimationInterpolation interp = AnimationInterpolation::Linear) {
    AnimationClip clip;
    clip.name = "TestClip";
    clip.duration = times.back();

    AnimationChannel ch;
    ch.targetJointIndex = jointIndex;
    ch.property = AnimationChannel::Property::Translation;
    ch.interpolation = interp;
    ch.timestamps = times;
    for (const auto& p : positions) {
        ch.values.push_back(glm::vec4(p, 0.0f));
    }
    clip.channels.push_back(ch);

    return clip;
}

TEST(AnimationEvaluator, SampleClip_Linear_Midpoint) {
    auto skel = makeTwoJointSkeleton();
    auto clip = makeTranslationClip(0,
        {0.0f, 1.0f},
        {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0)});

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 0.5f, *skel, translations, rotations, scales);

    // At t=0.5, linear interpolation between (0,0,0) and (10,0,0) = (5,0,0)
    EXPECT_NEAR(translations[0].x, 5.0f, 1e-3f);
}

TEST(AnimationEvaluator, SampleClip_Step_HoldsFirstValue) {
    auto skel = makeTwoJointSkeleton();
    auto clip = makeTranslationClip(0,
        {0.0f, 1.0f},
        {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0)},
        AnimationInterpolation::Step);

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 0.5f, *skel, translations, rotations, scales);

    // Step interpolation: holds the first keyframe value until the next
    EXPECT_NEAR(translations[0].x, 0.0f, 1e-3f);
}

TEST(AnimationEvaluator, SampleClip_BeforeFirstKeyframe_ClampsToFirst) {
    auto skel = makeTwoJointSkeleton();
    auto clip = makeTranslationClip(0,
        {0.5f, 1.0f},
        {glm::vec3(5, 0, 0), glm::vec3(10, 0, 0)});

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 0.0f, *skel, translations, rotations, scales);

    // Before first keyframe: clamp to first value
    EXPECT_NEAR(translations[0].x, 5.0f, 1e-3f);
}

TEST(AnimationEvaluator, SampleClip_AfterLastKeyframe_ClampsToLast) {
    auto skel = makeTwoJointSkeleton();
    auto clip = makeTranslationClip(0,
        {0.0f, 1.0f},
        {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0)});

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 5.0f, *skel, translations, rotations, scales);

    // After last keyframe: clamp to last value
    EXPECT_NEAR(translations[0].x, 10.0f, 1e-3f);
}

TEST(AnimationEvaluator, SampleClip_SingleKeyframe) {
    auto skel = makeTwoJointSkeleton();
    auto clip = makeTranslationClip(0,
        {0.0f},
        {glm::vec3(7, 0, 0)});

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 0.5f, *skel, translations, rotations, scales);

    // Single keyframe: always returns that value
    EXPECT_NEAR(translations[0].x, 7.0f, 1e-3f);
}

TEST(AnimationEvaluator, SampleClip_UntargetedJoint_Unchanged) {
    auto skel = makeTwoJointSkeleton();
    // Only animate joint 0, not joint 1
    auto clip = makeTranslationClip(0,
        {0.0f, 1.0f},
        {glm::vec3(0, 0, 0), glm::vec3(10, 0, 0)});

    std::vector<glm::vec3> translations, scales;
    std::vector<glm::quat> rotations;
    AnimationEvaluator::setDefaultPose(*skel, translations, rotations, scales);

    AnimationEvaluator::sampleClip(clip, 0.5f, *skel, translations, rotations, scales);

    // Joint 1 should retain its default pose
    TestHelpers::ExpectVec3Near(translations[1], glm::vec3(0.0f, 1.0f, 0.0f));
}

// ═══════════════════════════════════════════════════════════════
// computeBoneMatrices() Tests
// ═══════════════════════════════════════════════════════════════

TEST(AnimationEvaluator, ComputeBoneMatrices_AllIdentity) {
    auto skel = makeTwoJointSkeleton();

    std::vector<glm::vec3> translations = {glm::vec3(0), glm::vec3(0)};
    std::vector<glm::quat> rotations = {glm::quat(1,0,0,0), glm::quat(1,0,0,0)};
    std::vector<glm::vec3> scales = {glm::vec3(1), glm::vec3(1)};
    std::vector<glm::mat4> boneMatrices(2);

    AnimationEvaluator::computeBoneMatrices(*skel, translations, rotations, scales, boneMatrices);

    // With all-identity transforms and identity inverse bind matrices,
    // bone matrices should be identity
    TestHelpers::ExpectMat4Near(boneMatrices[0], glm::mat4(1.0f));
    TestHelpers::ExpectMat4Near(boneMatrices[1], glm::mat4(1.0f));
}

TEST(AnimationEvaluator, ComputeBoneMatrices_RootTranslation) {
    auto skel = makeTwoJointSkeleton();

    std::vector<glm::vec3> translations = {glm::vec3(5, 0, 0), glm::vec3(0)};
    std::vector<glm::quat> rotations = {glm::quat(1,0,0,0), glm::quat(1,0,0,0)};
    std::vector<glm::vec3> scales = {glm::vec3(1), glm::vec3(1)};
    std::vector<glm::mat4> boneMatrices(2);

    AnimationEvaluator::computeBoneMatrices(*skel, translations, rotations, scales, boneMatrices);

    // Root bone: globalTransform * inverseBindMatrix
    // globalTransform = T(5,0,0), inverseBindMatrix = identity
    // So boneMatrix[0] should have T(5,0,0)
    EXPECT_NEAR(boneMatrices[0][3][0], 5.0f, 1e-4f);
}

TEST(AnimationEvaluator, ComputeBoneMatrices_ParentChildHierarchy) {
    auto skel = makeTwoJointSkeleton();

    // Root at (5,0,0), child at (0,1,0) local
    std::vector<glm::vec3> translations = {glm::vec3(5, 0, 0), glm::vec3(0, 1, 0)};
    std::vector<glm::quat> rotations = {glm::quat(1,0,0,0), glm::quat(1,0,0,0)};
    std::vector<glm::vec3> scales = {glm::vec3(1), glm::vec3(1)};
    std::vector<glm::mat4> boneMatrices(2);

    AnimationEvaluator::computeBoneMatrices(*skel, translations, rotations, scales, boneMatrices);

    // Child global transform: parent(T(5,0,0)) * local(T(0,1,0)) = T(5,1,0)
    // boneMatrix[1] = T(5,1,0) * inverseBindMatrix(identity) = T(5,1,0)
    EXPECT_NEAR(boneMatrices[1][3][0], 5.0f, 1e-4f);
    EXPECT_NEAR(boneMatrices[1][3][1], 1.0f, 1e-4f);
}
