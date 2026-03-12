//
// AnimationEvaluator.cpp - Keyframe interpolation and bone matrix computation
//

#include "Animation/AnimationEvaluator.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

// ============================================================================
// Default Pose
// ============================================================================

void AnimationEvaluator::setDefaultPose(
    const Skeleton& skeleton,
    std::vector<glm::vec3>& outTranslations,
    std::vector<glm::quat>& outRotations,
    std::vector<glm::vec3>& outScales)
{
    uint32_t count = skeleton.jointCount();
    outTranslations.resize(count);
    outRotations.resize(count);
    outScales.resize(count);

    for (uint32_t i = 0; i < count; ++i) {
        outTranslations[i] = skeleton.joints[i].defaultTranslation;
        outRotations[i] = skeleton.joints[i].defaultRotation;
        outScales[i] = skeleton.joints[i].defaultScale;
    }
}

// ============================================================================
// Clip Sampling
// ============================================================================

void AnimationEvaluator::sampleClip(
    const AnimationClip& clip,
    float time,
    const Skeleton& skeleton,
    std::vector<glm::vec3>& outTranslations,
    std::vector<glm::quat>& outRotations,
    std::vector<glm::vec3>& outScales)
{
    // Sample each channel and write to the appropriate joint's TRS
    for (const auto& channel : clip.channels) {
        if (channel.targetJointIndex < 0 ||
            channel.targetJointIndex >= static_cast<int>(skeleton.jointCount())) {
            continue;
        }

        glm::vec4 sampled = sampleChannel(channel, time);
        int idx = channel.targetJointIndex;

        switch (channel.property) {
            case AnimationChannel::Property::Translation:
                outTranslations[idx] = glm::vec3(sampled);
                break;
            case AnimationChannel::Property::Rotation:
                // glTF quaternion order: x, y, z, w
                outRotations[idx] = glm::normalize(glm::quat(sampled.w, sampled.x, sampled.y, sampled.z));
                break;
            case AnimationChannel::Property::Scale:
                outScales[idx] = glm::vec3(sampled);
                break;
        }
    }
}

// ============================================================================
// Bone Matrix Computation
// ============================================================================

void AnimationEvaluator::computeBoneMatrices(
    const Skeleton& skeleton,
    const std::vector<glm::vec3>& translations,
    const std::vector<glm::quat>& rotations,
    const std::vector<glm::vec3>& scales,
    std::vector<glm::mat4>& outBoneMatrices)
{
    uint32_t count = skeleton.jointCount();
    outBoneMatrices.resize(count);

    // Temporary array for global transforms
    std::vector<glm::mat4> globalTransforms(count);

    for (uint32_t i = 0; i < count; ++i) {
        // Compose local transform: T * R * S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), translations[i]);
        glm::mat4 R = glm::toMat4(rotations[i]);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), scales[i]);
        glm::mat4 localMatrix = T * R * S;

        // Compute global transform
        int parentIdx = skeleton.joints[i].parentIndex;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(i)) {
            globalTransforms[i] = globalTransforms[parentIdx] * localMatrix;
        } else {
            globalTransforms[i] = localMatrix;
        }

        // Final bone matrix = global * inverseBindMatrix
        outBoneMatrices[i] = globalTransforms[i] * skeleton.joints[i].inverseBindMatrix;
    }
}

// ============================================================================
// Channel Sampling
// ============================================================================

glm::vec4 AnimationEvaluator::sampleChannel(
    const AnimationChannel& channel,
    float time)
{
    if (channel.timestamps.empty() || channel.values.empty()) {
        return glm::vec4(0.0f);
    }

    // Clamp time to channel range
    if (time <= channel.timestamps.front()) {
        if (channel.interpolation == AnimationInterpolation::CubicSpline) {
            return channel.values[1]; // Skip inTangent, return value
        }
        return channel.values[0];
    }

    if (time >= channel.timestamps.back()) {
        if (channel.interpolation == AnimationInterpolation::CubicSpline) {
            // Last value is at index (keyframeCount-1)*3 + 1
            return channel.values[(channel.keyframeCount() - 1) * 3 + 1];
        }
        return channel.values.back();
    }

    // Find surrounding keyframes
    uint32_t keyIndex = findKeyframe(channel.timestamps, time);
    uint32_t nextKeyIndex = keyIndex + 1;

    float t0 = channel.timestamps[keyIndex];
    float t1 = channel.timestamps[nextKeyIndex];
    float deltaTime = t1 - t0;
    float t = (deltaTime > 0.0f) ? (time - t0) / deltaTime : 0.0f;

    switch (channel.interpolation) {
        case AnimationInterpolation::Step:
            return interpolateStep(channel.values, keyIndex);

        case AnimationInterpolation::Linear:
            return interpolateLinear(channel.values, keyIndex, t);

        case AnimationInterpolation::CubicSpline:
            return interpolateCubicSpline(channel.values, keyIndex, t, deltaTime);
    }

    return glm::vec4(0.0f);
}

// ============================================================================
// Keyframe Search
// ============================================================================

uint32_t AnimationEvaluator::findKeyframe(
    const std::vector<float>& timestamps,
    float time)
{
    // Binary search for the last keyframe at or before time
    auto it = std::upper_bound(timestamps.begin(), timestamps.end(), time);
    if (it == timestamps.begin()) return 0;
    uint32_t idx = static_cast<uint32_t>(std::distance(timestamps.begin(), it)) - 1;
    // Ensure we don't return the last index (need room for next keyframe)
    if (idx >= timestamps.size() - 1) {
        idx = static_cast<uint32_t>(timestamps.size()) - 2;
    }
    return idx;
}

// ============================================================================
// Interpolation Methods
// ============================================================================

glm::vec4 AnimationEvaluator::interpolateStep(
    const std::vector<glm::vec4>& values,
    uint32_t keyIndex)
{
    return values[keyIndex];
}

glm::vec4 AnimationEvaluator::interpolateLinear(
    const std::vector<glm::vec4>& values,
    uint32_t keyIndex,
    float t)
{
    const glm::vec4& v0 = values[keyIndex];
    const glm::vec4& v1 = values[keyIndex + 1];
    return glm::mix(v0, v1, t);
}

glm::vec4 AnimationEvaluator::interpolateCubicSpline(
    const std::vector<glm::vec4>& values,
    uint32_t keyIndex,
    float t,
    float deltaTime)
{
    // CubicSpline stores triples: [inTangent, value, outTangent] per keyframe
    // Index layout: keyIndex*3+0 = inTangent, keyIndex*3+1 = value, keyIndex*3+2 = outTangent
    uint32_t i0 = keyIndex * 3;
    uint32_t i1 = (keyIndex + 1) * 3;

    glm::vec4 v0 = values[i0 + 1];           // Value at keyIndex
    glm::vec4 outTangent0 = values[i0 + 2];  // Out-tangent at keyIndex
    glm::vec4 v1 = values[i1 + 1];           // Value at keyIndex+1
    glm::vec4 inTangent1 = values[i1 + 0];   // In-tangent at keyIndex+1

    // Hermite spline interpolation
    float t2 = t * t;
    float t3 = t2 * t;

    glm::vec4 result =
        (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 +
        deltaTime * (t3 - 2.0f * t2 + t) * outTangent0 +
        (-2.0f * t3 + 3.0f * t2) * v1 +
        deltaTime * (t3 - t2) * inTangent1;

    return result;
}

} // namespace Shoonyakasha
