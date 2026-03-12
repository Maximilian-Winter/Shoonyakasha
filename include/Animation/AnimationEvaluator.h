//
// AnimationEvaluator.h - Keyframe interpolation and bone matrix computation
//
// CPU-side animation evaluation:
//   1. Sample animation channels at a given time -> per-joint TRS
//   2. Walk the joint hierarchy to compute global transforms
//   3. Multiply by inverse bind matrices to get final bone matrices
//
// 動之算 — The calculation of motion
//

#pragma once

#include "Resources/AnimationData.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace Shoonyakasha {

class AnimationEvaluator {
public:
    // ─── Clip Sampling ──────────────────────────────────────────

    /// Sample all channels of a clip at the given time.
    /// Writes interpolated TRS values into the output arrays.
    /// Arrays must be pre-sized to skeleton->jointCount().
    /// Joints without channels in this clip retain their current values.
    static void sampleClip(
        const AnimationClip& clip,
        float time,
        const Skeleton& skeleton,
        std::vector<glm::vec3>& outTranslations,
        std::vector<glm::quat>& outRotations,
        std::vector<glm::vec3>& outScales
    );

    // ─── Bone Matrix Computation ────────────────────────────────

    /// Compute final bone matrices from local TRS values.
    /// Result: boneMatrices[i] = globalTransform[i] * inverseBindMatrix[i]
    /// Assumes joints are ordered such that parent indices < child indices
    /// (guaranteed by glTF skin joint ordering).
    static void computeBoneMatrices(
        const Skeleton& skeleton,
        const std::vector<glm::vec3>& translations,
        const std::vector<glm::quat>& rotations,
        const std::vector<glm::vec3>& scales,
        std::vector<glm::mat4>& outBoneMatrices
    );

    // ─── Default Pose ───────────────────────────────────────────

    /// Initialize TRS arrays with the skeleton's default (bind) pose
    static void setDefaultPose(
        const Skeleton& skeleton,
        std::vector<glm::vec3>& outTranslations,
        std::vector<glm::quat>& outRotations,
        std::vector<glm::vec3>& outScales
    );

private:
    // ─── Channel Interpolation ──────────────────────────────────

    /// Sample a single channel at time t
    static glm::vec4 sampleChannel(
        const AnimationChannel& channel,
        float time
    );

    /// Binary search for the keyframe pair surrounding time t
    /// Returns the index of the keyframe at or before t
    static uint32_t findKeyframe(
        const std::vector<float>& timestamps,
        float time
    );

    // ─── Interpolation Methods ──────────────────────────────────

    static glm::vec4 interpolateStep(
        const std::vector<glm::vec4>& values,
        uint32_t keyIndex
    );

    static glm::vec4 interpolateLinear(
        const std::vector<glm::vec4>& values,
        uint32_t keyIndex,
        float t  // normalized [0, 1] between keyframes
    );

    static glm::vec4 interpolateCubicSpline(
        const std::vector<glm::vec4>& values,
        uint32_t keyIndex,
        float t,
        float deltaTime  // time between the two keyframes
    );
};

} // namespace Shoonyakasha
