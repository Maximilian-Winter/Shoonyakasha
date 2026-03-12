//
// AnimationDataTest.cpp - Tests for Skeleton, Joint, AnimationClip data structures
//
// Tier 2: Pure data structures — no GPU, no EnTT registry needed
//

#include <gtest/gtest.h>
#include "Resources/AnimationData.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// Joint Default Values
// ═══════════════════════════════════════════════════════════════

TEST(Joint, DefaultValues) {
    Joint joint;
    EXPECT_EQ(joint.parentIndex, -1);
    EXPECT_TRUE(joint.name.empty());

    // Default identity inverse bind matrix
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_FLOAT_EQ(joint.inverseBindMatrix[c][r], (c == r) ? 1.0f : 0.0f);

    // Default TRS
    EXPECT_FLOAT_EQ(joint.defaultTranslation.x, 0.0f);
    EXPECT_FLOAT_EQ(joint.defaultScale.x, 1.0f);
    // Identity quaternion: w=1, x=y=z=0
    EXPECT_FLOAT_EQ(joint.defaultRotation.w, 1.0f);
    EXPECT_FLOAT_EQ(joint.defaultRotation.x, 0.0f);
}

// ═══════════════════════════════════════════════════════════════
// Skeleton Tests
// ═══════════════════════════════════════════════════════════════

static Skeleton makeTestSkeleton() {
    Skeleton skel;
    skel.name = "TestSkeleton";

    Joint root;
    root.name = "Hips";
    root.parentIndex = -1;
    skel.joints.push_back(root);

    Joint spine;
    spine.name = "Spine";
    spine.parentIndex = 0;
    skel.joints.push_back(spine);

    Joint head;
    head.name = "Head";
    head.parentIndex = 1;
    skel.joints.push_back(head);

    skel.buildNameLookup();
    return skel;
}

TEST(Skeleton, JointCount) {
    auto skel = makeTestSkeleton();
    EXPECT_EQ(skel.jointCount(), 3u);
}

TEST(Skeleton, BuildNameLookup_PopulatesMap) {
    auto skel = makeTestSkeleton();
    EXPECT_EQ(skel.jointNameToIndex.size(), 3u);
}

TEST(Skeleton, FindJoint_Found) {
    auto skel = makeTestSkeleton();
    EXPECT_EQ(skel.findJoint("Hips"), 0);
    EXPECT_EQ(skel.findJoint("Spine"), 1);
    EXPECT_EQ(skel.findJoint("Head"), 2);
}

TEST(Skeleton, FindJoint_NotFound) {
    auto skel = makeTestSkeleton();
    EXPECT_EQ(skel.findJoint("Nonexistent"), -1);
}

TEST(Skeleton, ParentIndices_Correct) {
    auto skel = makeTestSkeleton();
    EXPECT_EQ(skel.joints[0].parentIndex, -1);  // Root
    EXPECT_EQ(skel.joints[1].parentIndex, 0);   // Spine → Hips
    EXPECT_EQ(skel.joints[2].parentIndex, 1);   // Head → Spine
}

TEST(Skeleton, EmptySkeleton_JointCountZero) {
    Skeleton skel;
    EXPECT_EQ(skel.jointCount(), 0u);
}

TEST(Skeleton, BuildNameLookup_SkipsEmptyNames) {
    Skeleton skel;
    Joint unnamed;
    unnamed.name = "";
    skel.joints.push_back(unnamed);

    Joint named;
    named.name = "Root";
    skel.joints.push_back(named);

    skel.buildNameLookup();
    EXPECT_EQ(skel.jointNameToIndex.size(), 1u);
    EXPECT_EQ(skel.findJoint("Root"), 1);
}

// ═══════════════════════════════════════════════════════════════
// AnimationChannel Tests
// ═══════════════════════════════════════════════════════════════

TEST(AnimationChannel, KeyframeCount) {
    AnimationChannel ch;
    ch.timestamps = {0.0f, 0.5f, 1.0f};
    ch.values = {glm::vec4(0), glm::vec4(1), glm::vec4(2)};
    EXPECT_EQ(ch.keyframeCount(), 3u);
}

TEST(AnimationChannel, DefaultValues) {
    AnimationChannel ch;
    EXPECT_EQ(ch.targetJointIndex, -1);
    EXPECT_EQ(ch.property, AnimationChannel::Property::Translation);
    EXPECT_EQ(ch.interpolation, AnimationInterpolation::Linear);
    EXPECT_EQ(ch.keyframeCount(), 0u);
}

// ═══════════════════════════════════════════════════════════════
// AnimationClip Tests
// ═══════════════════════════════════════════════════════════════

TEST(AnimationClip, DefaultValues) {
    AnimationClip clip;
    EXPECT_TRUE(clip.name.empty());
    EXPECT_FLOAT_EQ(clip.duration, 0.0f);
    EXPECT_TRUE(clip.channels.empty());
}

TEST(AnimationClip, WithChannels) {
    AnimationClip clip;
    clip.name = "Walk";
    clip.duration = 2.0f;

    AnimationChannel ch;
    ch.targetJointIndex = 0;
    ch.timestamps = {0.0f, 1.0f, 2.0f};
    ch.values = {glm::vec4(0), glm::vec4(1), glm::vec4(0)};
    clip.channels.push_back(ch);

    EXPECT_EQ(clip.channels.size(), 1u);
    EXPECT_FLOAT_EQ(clip.duration, 2.0f);
}
