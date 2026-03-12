//
// AnimationPlaybackTest.cpp - Tests for AnimationPlaybackComponent and SkeletonComponent
//
// Tier 2: ECS data structures — no GPU
//

#include <gtest/gtest.h>
#include "ECS/SkeletonComponents.h"

using namespace Shoonyakasha;

// ═══════════════════════════════════════════════════════════════
// Helper: Create a simple skeleton
// ═══════════════════════════════════════════════════════════════

static std::shared_ptr<Skeleton> makeTestSkeleton(uint32_t jointCount = 3) {
    auto skel = std::make_shared<Skeleton>();
    skel->name = "TestSkeleton";

    for (uint32_t i = 0; i < jointCount; ++i) {
        Joint j;
        j.name = "Joint_" + std::to_string(i);
        j.parentIndex = (i == 0) ? -1 : static_cast<int>(i - 1);
        skel->joints.push_back(j);
    }

    skel->buildNameLookup();
    return skel;
}

static std::shared_ptr<AnimationClip> makeTestClip(const std::string& name, float duration) {
    auto clip = std::make_shared<AnimationClip>();
    clip->name = name;
    clip->duration = duration;
    return clip;
}

// ═══════════════════════════════════════════════════════════════
// AnimationPlaybackComponent — Play/Stop
// ═══════════════════════════════════════════════════════════════

TEST(AnimationPlayback, Default_NotPlaying) {
    AnimationPlaybackComponent playback;
    EXPECT_FALSE(playback.playing);
    EXPECT_EQ(playback.currentClipIndex, -1);
}

TEST(AnimationPlayback, Play_SetsState) {
    AnimationPlaybackComponent playback;
    playback.clips.push_back(makeTestClip("Walk", 2.0f));

    playback.play(0);

    EXPECT_TRUE(playback.playing);
    EXPECT_EQ(playback.currentClipIndex, 0);
    EXPECT_FLOAT_EQ(playback.currentTime, 0.0f);
}

TEST(AnimationPlayback, Play_InvalidIndex_DoesNothing) {
    AnimationPlaybackComponent playback;
    playback.play(5);  // No clips at all

    EXPECT_FALSE(playback.playing);
    EXPECT_EQ(playback.currentClipIndex, -1);
}

TEST(AnimationPlayback, Stop_ResetsState) {
    AnimationPlaybackComponent playback;
    playback.clips.push_back(makeTestClip("Walk", 2.0f));
    playback.play(0);
    playback.currentTime = 1.5f;

    playback.stop();

    EXPECT_FALSE(playback.playing);
    EXPECT_FLOAT_EQ(playback.currentTime, 0.0f);
}

TEST(AnimationPlayback, GetCurrentClip_Valid) {
    AnimationPlaybackComponent playback;
    auto clip = makeTestClip("Idle", 3.0f);
    playback.clips.push_back(clip);
    playback.play(0);

    const auto* current = playback.getCurrentClip();
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->name, "Idle");
    EXPECT_FLOAT_EQ(current->duration, 3.0f);
}

TEST(AnimationPlayback, GetCurrentClip_InvalidIndex_ReturnsNull) {
    AnimationPlaybackComponent playback;
    EXPECT_EQ(playback.getCurrentClip(), nullptr);
}

TEST(AnimationPlayback, GetCurrentClip_NegativeIndex_ReturnsNull) {
    AnimationPlaybackComponent playback;
    playback.clips.push_back(makeTestClip("Walk", 2.0f));
    // currentClipIndex is -1 by default
    EXPECT_EQ(playback.getCurrentClip(), nullptr);
}

// ═══════════════════════════════════════════════════════════════
// AnimationPlaybackComponent — Allocate
// ═══════════════════════════════════════════════════════════════

TEST(AnimationPlayback, Allocate_SizesArrays) {
    AnimationPlaybackComponent playback;
    playback.allocate(5);

    EXPECT_EQ(playback.jointTranslations.size(), 5u);
    EXPECT_EQ(playback.jointRotations.size(), 5u);
    EXPECT_EQ(playback.jointScales.size(), 5u);
}

TEST(AnimationPlayback, Allocate_DefaultValues) {
    AnimationPlaybackComponent playback;
    playback.allocate(2);

    // Translations default to (0,0,0)
    EXPECT_FLOAT_EQ(playback.jointTranslations[0].x, 0.0f);
    // Rotations default to identity quaternion (w=1, x=y=z=0)
    EXPECT_FLOAT_EQ(playback.jointRotations[0].w, 1.0f);
    EXPECT_FLOAT_EQ(playback.jointRotations[0].x, 0.0f);
    // Scales default to (1,1,1)
    EXPECT_FLOAT_EQ(playback.jointScales[0].x, 1.0f);
}

// ═══════════════════════════════════════════════════════════════
// SkeletonComponent Tests
// ═══════════════════════════════════════════════════════════════

TEST(SkeletonComponent, Allocate_SizesBoneMatrices) {
    SkeletonComponent comp;
    comp.skeleton = makeTestSkeleton(4);
    comp.allocate();

    EXPECT_EQ(comp.boneMatrices.size(), 4u);
}

TEST(SkeletonComponent, Allocate_IdentityMatrices) {
    SkeletonComponent comp;
    comp.skeleton = makeTestSkeleton(2);
    comp.allocate();

    // Allocated matrices should be identity
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_FLOAT_EQ(comp.boneMatrices[0][c][r], (c == r) ? 1.0f : 0.0f);
}

TEST(SkeletonComponent, JointCount_WithSkeleton) {
    SkeletonComponent comp;
    comp.skeleton = makeTestSkeleton(5);
    EXPECT_EQ(comp.jointCount(), 5u);
}

TEST(SkeletonComponent, JointCount_NoSkeleton_ReturnsZero) {
    SkeletonComponent comp;
    EXPECT_EQ(comp.jointCount(), 0u);
}

TEST(SkeletonComponent, SsboSize) {
    SkeletonComponent comp;
    comp.skeleton = makeTestSkeleton(4);
    // 4 joints × sizeof(mat4) = 4 × 64 = 256
    EXPECT_EQ(comp.ssboSize(), 4u * 64u);
}

TEST(SkeletonComponent, SsboSize_NoSkeleton_ReturnsZero) {
    SkeletonComponent comp;
    EXPECT_EQ(comp.ssboSize(), 0u);
}

TEST(SkeletonComponent, DirtyFlag_DefaultTrue) {
    SkeletonComponent comp;
    EXPECT_TRUE(comp.dirty);
}
