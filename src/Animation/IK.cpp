//
// IK.cpp - Two-bone inverse kinematics on a pose
//

#include "Animation/IK.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

namespace {

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(r[c]);
        if (length > 0.0f) r[c] /= length;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

glm::quat arcBetween(glm::vec3 a, glm::vec3 b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-12f || lb < 1e-12f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    a /= la;
    b /= lb;
    const float d = glm::dot(a, b);
    if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.999999f) {
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), a);
        if (glm::dot(axis, axis) < 1e-6f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), a);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    }
    const glm::vec3 axis = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
}

glm::quat parentRotation(const Skeleton& skeleton, const std::vector<glm::mat4>& globals, int joint) {
    const int parent = skeleton.joints[joint].parentIndex;
    return parent >= 0 && parent < static_cast<int>(globals.size()) ? rotationOf(globals[parent])
                                                                   : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::quat slerpShortest(const glm::quat& a, glm::quat b, float t) {
    if (glm::dot(a, b) < 0.0f) b = -b;
    return glm::normalize(glm::slerp(a, b, t));
}

} // namespace

void solveTwoBoneIK(const Skeleton& skeleton, Pose& pose, std::vector<glm::mat4>& globals,
                    const TwoBoneIKGoal& goal) {
    const int count = static_cast<int>(std::min<size_t>(skeleton.joints.size(), globals.size()));
    if (goal.upper < 0 || goal.middle < 0 || goal.end < 0 || goal.upper >= count || goal.middle >= count ||
        goal.end >= count || goal.weight <= 0.0f) {
        return;
    }
    const float weight = std::min(goal.weight, 1.0f);

    const glm::vec3 a = positionOf(globals[goal.upper]);
    const glm::vec3 b = positionOf(globals[goal.middle]);
    const glm::vec3 c = positionOf(globals[goal.end]);
    const float upperLength = glm::length(b - a);
    const float lowerLength = glm::length(c - b);
    if (upperLength < 1e-8f || lowerLength < 1e-8f) return;

    glm::vec3 toTarget = goal.target - a;
    float distance = glm::length(toTarget);
    const glm::vec3 direction = distance > 1e-8f ? toTarget / distance : glm::normalize(c - a);
    const float reach = upperLength + lowerLength;
    const float nearest = std::abs(upperLength - lowerLength);
    distance = std::clamp(distance, nearest + 1e-5f * reach, reach * (1.0f - 1e-5f));

    // The bend direction: the pole (or the current bend) perpendicular to the direction.
    glm::vec3 bend = goal.hasPole ? goal.pole : (b - a);
    bend -= glm::dot(bend, direction) * direction;
    if (glm::dot(bend, bend) < 1e-12f) {
        bend = (b - a) - glm::dot(b - a, direction) * direction;
        if (glm::dot(bend, bend) < 1e-12f) {
            bend = glm::cross(direction, glm::vec3(1.0f, 0.0f, 0.0f));
            if (glm::dot(bend, bend) < 1e-12f) bend = glm::cross(direction, glm::vec3(0.0f, 0.0f, 1.0f));
        }
    }
    bend = glm::normalize(bend);

    const float cosA = std::clamp((upperLength * upperLength + distance * distance - lowerLength * lowerLength) /
                                      (2.0f * upperLength * distance), -1.0f, 1.0f);
    const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
    const glm::vec3 newMiddle = a + upperLength * (cosA * direction + sinA * bend);
    const glm::vec3 newEnd = a + distance * direction;

    // Global rotations before and after.
    const glm::quat upperGlobal = rotationOf(globals[goal.upper]);
    const glm::quat middleGlobal = rotationOf(globals[goal.middle]);
    const glm::quat turnUpper = arcBetween(b - a, newMiddle - a);
    const glm::quat turnMiddle = arcBetween(turnUpper * (c - b), newEnd - newMiddle);

    const glm::quat upperParent = parentRotation(skeleton, globals, goal.upper);
    const glm::quat middleParent = parentRotation(skeleton, globals, goal.middle);
    const glm::quat endParent = parentRotation(skeleton, globals, goal.end);

    const glm::quat newUpperGlobal = turnUpper * upperGlobal;
    const glm::quat newMiddleGlobal = turnMiddle * turnUpper * middleGlobal;
    // Joints between upper and middle, and between middle and end, turn with them.
    const glm::quat newMiddleParent = turnUpper * middleParent;
    const glm::quat newEndParent = turnMiddle * turnUpper * endParent;

    auto setLocal = [&](int joint, const glm::quat& parent, const glm::quat& global) {
        const glm::quat solved = glm::normalize(glm::inverse(parent) * global);
        pose.rotations[joint] = slerpShortest(pose.rotations[joint], solved, weight);
    };
    setLocal(goal.upper, upperParent, newUpperGlobal);
    setLocal(goal.middle, newMiddleParent, newMiddleGlobal);
    if (goal.hasEndRotation) setLocal(goal.end, newEndParent, goal.endRotation);

    PoseOps::globalTransforms(skeleton, pose, globals);
}

} // namespace Shoonyakasha
