//
// IK.h - Two-bone inverse kinematics on a pose
//

#pragma once

#include "Animation/Pose.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace Shoonyakasha {

struct TwoBoneIKGoal {
    int upper = -1;    // e.g. the upper leg
    int middle = -1;   // the knee: below upper
    int end = -1;      // the foot: below middle

    glm::vec3 target{0.0f};          // skeleton space
    float weight = 1.0f;             // 0 leaves the pose as it is

    // Direction the middle joint bends toward, skeleton space. Without one
    // the limb keeps the plane it bends in.
    bool hasPole = false;
    glm::vec3 pole{0.0f, 0.0f, 1.0f};

    // Skeleton-space rotation for the end joint. Without one the end joint
    // keeps its rotation relative to the middle joint.
    bool hasEndRotation = false;
    glm::quat endRotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/// Turn the goal's upper and middle joints in `pose` so the end joint reaches
/// the target: the middle joint bends (law of cosines) in the plane through
/// the upper joint, the target and the pole, and the upper joint turns the
/// limb toward the target. A target beyond the limb's reach straightens it
/// toward the target; one nearer than |upper length - lower length| bends it
/// fully. Bone lengths are kept. Each joint's rotation is blended from the
/// pose's by the goal's weight. `globals` must hold the pose's
/// globalTransforms; it is recomputed afterwards.
void solveTwoBoneIK(const Skeleton& skeleton, Pose& pose, std::vector<glm::mat4>& globals,
                    const TwoBoneIKGoal& goal);

} // namespace Shoonyakasha
