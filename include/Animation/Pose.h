//
// Pose.h - Local joint transforms of a skeleton, and operations on them
//
// A Pose holds one translation, rotation and scale per joint, in the joint's
// parent space. Sampling, blending and additive layering read and write Poses;
// globalTransforms() and boneMatrices() turn one into skinning matrices.
//

#pragma once

#include "Resources/AnimationData.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>

namespace Shoonyakasha {

struct Pose {
    std::vector<glm::vec3> translations;
    std::vector<glm::quat> rotations;
    std::vector<glm::vec3> scales;

    uint32_t jointCount() const { return static_cast<uint32_t>(rotations.size()); }

    /// Size to the skeleton and set every joint to its default transform.
    void setRest(const Skeleton& skeleton);

    /// Size to `count` joints of identity transform.
    void setIdentity(uint32_t count);
};

namespace PoseOps {

/// The skeleton's rest pose with the clip's channels sampled at `time` on top.
/// Channels whose targetJointIndex is outside the skeleton are skipped.
void sample(const AnimationClip& clip, float time, const Skeleton& skeleton, Pose& out);

/// out = a blended toward b by `weight` (0 gives a, 1 gives b). Translations
/// and scales are interpolated linearly, rotations along the shorter arc.
/// `out` may be `a` or `b`. Poses must have the same joint count.
void blend(const Pose& a, const Pose& b, float weight, Pose& out);

/// blend() with a weight per joint. Joints past the end of `weights` take
/// weight 0.
void blendPerJoint(const Pose& a, const Pose& b, const std::vector<float>& weights, Pose& out);

/// The difference of `pose` from `reference`: translation pose - reference,
/// rotation inverse(reference) * pose, scale pose / reference.
void makeAdditive(const Pose& pose, const Pose& reference, Pose& outDelta);

/// base with `weight` of an additive delta applied: translation base + w * d,
/// rotation base * slerp(identity, d, w), scale base * mix(1, d, w).
/// `out` may be `base`.
void applyAdditive(const Pose& base, const Pose& delta, float weight, Pose& out);

/// Each joint's transform in skeleton space: the product of its local
/// transforms from the root down. Joints are visited in the skeleton's
/// evaluationOrder, or in a parents-first order computed here when that is
/// empty. A joint whose parent has not been visited yet is treated as a root.
void globalTransforms(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& out);

/// Skinning matrices: globals[i] * inverseBindMatrix[i].
void boneMatrices(const Skeleton& skeleton, const std::vector<glm::mat4>& globals,
                  std::vector<glm::mat4>& out);

} // namespace PoseOps

} // namespace Shoonyakasha
