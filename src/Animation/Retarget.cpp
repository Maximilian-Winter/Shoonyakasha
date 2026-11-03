//
// Retarget.cpp - Bake a clip of one humanoid onto another
//

#include "Animation/Retarget.h"
#include "Animation/Pose.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <vector>

namespace Shoonyakasha {

namespace {

using B = HumanoidBone;

// Bones further down the limb or spine, nearest first, that give a bone's
// direction.
std::vector<B> nextBones(B bone) {
    switch (bone) {
        case B::Hips: return {B::Spine, B::Chest, B::Neck, B::Head};
        case B::Spine: return {B::Chest, B::UpperChest, B::Neck, B::Head};
        case B::Chest: return {B::UpperChest, B::Neck, B::Head};
        case B::UpperChest: return {B::Neck, B::Head};
        case B::Neck: return {B::Head};
        case B::LeftShoulder: return {B::LeftUpperArm};
        case B::RightShoulder: return {B::RightUpperArm};
        case B::LeftUpperArm: return {B::LeftLowerArm};
        case B::RightUpperArm: return {B::RightLowerArm};
        case B::LeftLowerArm: return {B::LeftHand};
        case B::RightLowerArm: return {B::RightHand};
        case B::LeftHand: return {B::LeftMiddleProximal, B::LeftIndexProximal};
        case B::RightHand: return {B::RightMiddleProximal, B::RightIndexProximal};
        case B::LeftUpperLeg: return {B::LeftLowerLeg};
        case B::RightUpperLeg: return {B::RightLowerLeg};
        case B::LeftLowerLeg: return {B::LeftFoot};
        case B::RightLowerLeg: return {B::RightFoot};
        case B::LeftFoot: return {B::LeftToes};
        case B::RightFoot: return {B::RightToes};
        default: return {};
    }
}

B fingerNext(B bone) {
    const int first[2] = {static_cast<int>(B::LeftThumbProximal), static_cast<int>(B::RightThumbProximal)};
    for (int side = 0; side < 2; ++side) {
        const int offset = static_cast<int>(bone) - first[side];
        if (offset >= 0 && offset < 15 && offset % 3 < 2) return static_cast<B>(static_cast<int>(bone) + 1);
    }
    return B::Count;
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(r[c]);
        if (length > 0.0f) r[c] /= length;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// The rotation turning unit vector a onto unit vector b along the shorter arc.
glm::quat arcBetween(const glm::vec3& a, const glm::vec3& b) {
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

struct MappedBone {
    B bone;
    int source;
    int target;
    glm::quat sourceRest;
    glm::quat targetRest;
    glm::quat correction;   // body frame
};

void pushKey(AnimationChannel& channel, float time, glm::vec4 value) {
    channel.timestamps.push_back(time);
    channel.values.push_back(value);
}

} // namespace

std::shared_ptr<AnimationClip> retargetClip(const AnimationClip& clip,
                                            const HumanoidAvatar& source,
                                            const HumanoidAvatar& target) {
    auto result = std::make_shared<AnimationClip>();
    result->name = clip.name;
    result->duration = clip.duration;
    if (!source.skeleton || !target.skeleton) return result;
    const Skeleton& sourceSkeleton = *source.skeleton;
    const Skeleton& targetSkeleton = *target.skeleton;
    const int targetCount = static_cast<int>(targetSkeleton.joints.size());

    const glm::quat sourceFrame = source.bodyFrame;
    const glm::quat targetFrame = target.bodyFrame;
    const glm::quat targetFrameInverse = glm::inverse(targetFrame);

    // Bones mapped in both avatars, and the turn matching their rest directions.
    std::vector<MappedBone> bones;
    std::vector<int> boneOfJoint(targetCount, -1);
    for (int b = 0; b < kHumanoidBoneCount; ++b) {
        const B bone = static_cast<B>(b);
        const int s = source.joint(bone);
        const int t = target.joint(bone);
        if (s < 0 || t < 0) continue;
        MappedBone mapped{bone, s, t, rotationOf(source.restGlobals[s]), rotationOf(target.restGlobals[t]),
                          glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};

        std::vector<B> candidates = nextBones(bone);
        if (const B next = fingerNext(bone); next != B::Count) candidates.push_back(next);
        for (B next : candidates) {
            const int sn = source.joint(next);
            const int tn = target.joint(next);
            if (sn < 0 || tn < 0) continue;
            glm::vec3 ds = sourceFrame * (positionOf(source.restGlobals[sn]) - positionOf(source.restGlobals[s]));
            glm::vec3 dt = targetFrame * (positionOf(target.restGlobals[tn]) - positionOf(target.restGlobals[t]));
            if (glm::dot(ds, ds) > 1e-12f && glm::dot(dt, dt) > 1e-12f) {
                mapped.correction = arcBetween(glm::normalize(dt), glm::normalize(ds));
            }
            break;
        }
        boneOfJoint[t] = static_cast<int>(bones.size());
        bones.push_back(mapped);
    }

    // Keyframe times of the clip.
    std::vector<float> times;
    for (const auto& channel : clip.channels) times.insert(times.end(), channel.timestamps.begin(), channel.timestamps.end());
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return b - a < 1e-6f; }), times.end());
    if (times.empty()) times.push_back(0.0f);

    std::vector<AnimationChannel> rotations(bones.size());
    for (size_t i = 0; i < bones.size(); ++i) {
        rotations[i].targetJointIndex = bones[i].target;
        rotations[i].targetJointName = targetSkeleton.joints[bones[i].target].name;
        rotations[i].property = AnimationChannel::Property::Rotation;
        rotations[i].interpolation = AnimationInterpolation::Linear;
    }
    const int sourceHips = source.joint(B::Hips);
    const int targetHips = target.joint(B::Hips);
    const bool moveHips = sourceHips >= 0 && targetHips >= 0;
    AnimationChannel hipsTranslation;
    if (moveHips) {
        hipsTranslation.targetJointIndex = targetHips;
        hipsTranslation.targetJointName = targetSkeleton.joints[targetHips].name;
        hipsTranslation.property = AnimationChannel::Property::Translation;
        hipsTranslation.interpolation = AnimationInterpolation::Linear;
    }
    const float scale = target.hipHeight / source.hipHeight;

    const std::vector<int> order = targetSkeleton.computeEvaluationOrder();
    Pose sourcePose;
    std::vector<glm::mat4> sourceGlobals;
    std::vector<glm::quat> targetRotations(targetCount);
    std::vector<glm::mat4> targetGlobals(targetCount);
    std::vector<char> visited(targetCount);
    std::vector<glm::quat> lastKey(bones.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

    for (size_t k = 0; k < times.size(); ++k) {
        const float time = times[k];
        PoseOps::sample(clip, time, sourceSkeleton, sourcePose);
        PoseOps::globalTransforms(sourceSkeleton, sourcePose, sourceGlobals);
        std::fill(visited.begin(), visited.end(), 0);

        for (int j : order) {
            const Joint& joint = targetSkeleton.joints[j];
            const int parent = joint.parentIndex;
            const bool hasParent = parent >= 0 && parent < targetCount && visited[parent];
            const glm::quat parentRotation = hasParent ? targetRotations[parent] : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const glm::mat4 parentGlobal = hasParent ? targetGlobals[parent] : glm::mat4(1.0f);

            glm::quat local = joint.defaultRotation;
            if (const int b = boneOfJoint[j]; b >= 0) {
                const MappedBone& mapped = bones[b];
                const glm::quat delta = sourceFrame * rotationOf(sourceGlobals[mapped.source]) *
                                        glm::inverse(mapped.sourceRest) * glm::inverse(sourceFrame);
                const glm::quat global =
                    targetFrameInverse * delta * mapped.correction * targetFrame * mapped.targetRest;
                local = glm::normalize(glm::inverse(parentRotation) * global);
                if (k > 0 && glm::dot(local, lastKey[b]) < 0.0f) local = -local;
                lastKey[b] = local;
                pushKey(rotations[b], time, glm::vec4(local.x, local.y, local.z, local.w));
            }

            glm::vec3 translation = joint.defaultTranslation;
            if (moveHips && j == targetHips) {
                const glm::vec3 moved = sourceFrame * (positionOf(sourceGlobals[sourceHips]) -
                                                       positionOf(source.restGlobals[sourceHips]));
                const glm::vec3 position = positionOf(target.restGlobals[targetHips]) + targetFrameInverse * (moved * scale);
                translation = glm::vec3(glm::inverse(parentGlobal) * glm::vec4(position, 1.0f));
                pushKey(hipsTranslation, time, glm::vec4(translation, 0.0f));
            }

            targetRotations[j] = glm::normalize(parentRotation * local);
            targetGlobals[j] = parentGlobal * glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(local) *
                               glm::scale(glm::mat4(1.0f), joint.defaultScale);
            visited[j] = 1;
        }
    }

    for (auto& channel : rotations) result->channels.push_back(std::move(channel));
    if (moveHips) result->channels.push_back(std::move(hipsTranslation));
    return result;
}

} // namespace Shoonyakasha
