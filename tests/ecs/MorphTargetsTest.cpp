//
// MorphTargetsTest.cpp - Blending morph targets and sampling their weights
//
// Tier 2: CPU only - no GPU
//

#include <gtest/gtest.h>
#include "Animation/AnimationEvaluator.h"
#include "ECS/MorphComponents.h"

#include <cmath>
#include <cstring>

using namespace Shoonyakasha;

namespace {

// Two vertices of a layout with the position at 0, a pad, the normal at 16
// and the tangent at 28: 44 bytes.
struct TestVertex {
    glm::vec3 position;
    float pad;
    glm::vec3 normal;
    glm::vec4 tangent;
};
static_assert(sizeof(TestVertex) == 44);

MorphTargetSet twoVertexSet() {
    MorphTargetSet set;
    set.vertexCount = 2;
    set.targetCount = 2;
    set.vertexStride = sizeof(TestVertex);
    set.positionOffset = 0;
    set.normalOffset = 16;
    set.tangentOffset = 28;
    const TestVertex base[2] = {
        {glm::vec3(0, 0, 0), 7.0f, glm::vec3(0, 1, 0), glm::vec4(1, 0, 0, -1)},
        {glm::vec3(1, 0, 0), 7.0f, glm::vec3(0, 1, 0), glm::vec4(1, 0, 0, -1)},
    };
    const auto* bytes = reinterpret_cast<const uint8_t*>(base);
    set.baseVertices.assign(bytes, bytes + sizeof(base));
    // Target 0 lifts vertex 0 by 1; target 1 moves vertex 1 along z by 2.
    set.positionDeltas = {glm::vec3(0, 1, 0), glm::vec3(0), glm::vec3(0), glm::vec3(0, 0, 2)};
    // Target 0 tilts vertex 0's normal towards +x.
    set.normalDeltas = {glm::vec3(1, 0, 0), glm::vec3(0), glm::vec3(0), glm::vec3(0)};
    return set;
}

std::vector<TestVertex> blend(const MorphTargetSet& set, const std::vector<float>& weights) {
    std::vector<TestVertex> out(set.vertexCount);
    blendMorphTargets(set, weights, reinterpret_cast<uint8_t*>(out.data()));
    return out;
}

} // namespace

TEST(MorphTargets, WeightsAddTheirTargetsDeltas) {
    const auto set = twoVertexSet();
    const auto out = blend(set, {0.5f, 1.0f});
    EXPECT_EQ(out[0].position, glm::vec3(0, 0.5f, 0));
    EXPECT_EQ(out[1].position, glm::vec3(1, 0, 2));
    EXPECT_EQ(out[0].pad, 7.0f);                    // the rest of the vertex is kept
    EXPECT_EQ(out[0].tangent.w, -1.0f);
}

TEST(MorphTargets, NormalsAreNormalisedAgain) {
    const auto out = blend(twoVertexSet(), {1.0f, 0.0f});
    EXPECT_NEAR(out[0].normal.x, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(out[0].normal.y, std::sqrt(0.5f), 1e-6f);
    EXPECT_EQ(out[1].normal, glm::vec3(0, 1, 0));   // vertex 1 has no normal delta from target 0
}

TEST(MorphTargets, ZeroAndMissingWeightsLeaveTheBase) {
    const auto set = twoVertexSet();
    const auto base = blend(set, {});
    EXPECT_EQ(std::memcmp(base.data(), set.baseVertices.data(), set.baseVertices.size()), 0);
    const auto partly = blend(set, {0.0f});
    EXPECT_EQ(partly[1].position, glm::vec3(1, 0, 0));
}

TEST(MorphTargets, SettingWeightsCountsChangesOnly) {
    MorphComponent morph;
    morph.targets = std::make_shared<MorphTargetSet>(twoVertexSet());
    morph.setWeights({0.5f});
    EXPECT_EQ(morph.weights, (std::vector<float>{0.5f, 0.0f}));   // padded to the targets
    const uint64_t version = morph.version;
    morph.setWeights({0.5f, 0.0f, 9.0f});                        // extra one dropped: no change
    EXPECT_EQ(morph.version, version);
    morph.setWeights({0.25f, 0.0f});
    EXPECT_EQ(morph.version, version + 1);
}

TEST(MorphWeightSampling, LinearStepAndClamped) {
    MorphWeightChannel channel;
    channel.weightCount = 2;
    channel.timestamps = {0.0f, 1.0f};
    channel.values = {0.0f, 1.0f, 1.0f, 0.0f};   // keyframe 0: (0, 1), keyframe 1: (1, 0)
    std::vector<float> w;

    AnimationEvaluator::sampleWeights(channel, 0.25f, w);
    ASSERT_EQ(w.size(), 2u);
    EXPECT_FLOAT_EQ(w[0], 0.25f);
    EXPECT_FLOAT_EQ(w[1], 0.75f);

    channel.interpolation = AnimationInterpolation::Step;
    AnimationEvaluator::sampleWeights(channel, 0.75f, w);
    EXPECT_FLOAT_EQ(w[0], 0.0f);

    AnimationEvaluator::sampleWeights(channel, -1.0f, w);
    EXPECT_FLOAT_EQ(w[1], 1.0f);
    AnimationEvaluator::sampleWeights(channel, 5.0f, w);
    EXPECT_FLOAT_EQ(w[0], 1.0f);
}

TEST(MorphWeightSampling, CubicSplineUsesValuesAndTangents) {
    MorphWeightChannel channel;
    channel.interpolation = AnimationInterpolation::CubicSpline;
    channel.weightCount = 1;
    channel.timestamps = {0.0f, 2.0f};
    // Per keyframe: in-tangent, value, out-tangent. Flat tangents from 0 to 1.
    channel.values = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    std::vector<float> w;
    AnimationEvaluator::sampleWeights(channel, 1.0f, w);
    EXPECT_FLOAT_EQ(w[0], 0.5f);   // smoothstep halfway
    AnimationEvaluator::sampleWeights(channel, 0.5f, w);
    EXPECT_NEAR(w[0], 0.15625f, 1e-6f);   // 3t^2 - 2t^3 at t = 0.25

    // An out-tangent of 1 per second raises the start of the curve.
    channel.values[2] = 1.0f;
    AnimationEvaluator::sampleWeights(channel, 0.5f, w);
    EXPECT_NEAR(w[0], 0.15625f + (0.015625f - 0.125f + 0.25f) * 2.0f, 1e-6f);
}
