//
// Humanoid.h - Standard humanoid bones and their mapping onto a skeleton
//
// A HumanoidAvatar names which joint of a skeleton plays each HumanoidBone,
// and keeps what retargeting (Animation/Retarget.h) needs from the rest pose:
// the joints' rest transforms, a body frame and the hip height.
//

#pragma once

#include "Resources/AnimationData.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Shoonyakasha {

enum class HumanoidBone : int {
    Hips, Spine, Chest, UpperChest, Neck, Head, Jaw, LeftEye, RightEye,
    LeftShoulder, LeftUpperArm, LeftLowerArm, LeftHand,
    RightShoulder, RightUpperArm, RightLowerArm, RightHand,
    LeftUpperLeg, LeftLowerLeg, LeftFoot, LeftToes,
    RightUpperLeg, RightLowerLeg, RightFoot, RightToes,
    LeftThumbProximal, LeftThumbIntermediate, LeftThumbDistal,
    LeftIndexProximal, LeftIndexIntermediate, LeftIndexDistal,
    LeftMiddleProximal, LeftMiddleIntermediate, LeftMiddleDistal,
    LeftRingProximal, LeftRingIntermediate, LeftRingDistal,
    LeftLittleProximal, LeftLittleIntermediate, LeftLittleDistal,
    RightThumbProximal, RightThumbIntermediate, RightThumbDistal,
    RightIndexProximal, RightIndexIntermediate, RightIndexDistal,
    RightMiddleProximal, RightMiddleIntermediate, RightMiddleDistal,
    RightRingProximal, RightRingIntermediate, RightRingDistal,
    RightLittleProximal, RightLittleIntermediate, RightLittleDistal,
    Count
};

constexpr int kHumanoidBoneCount = static_cast<int>(HumanoidBone::Count);

/// The bone's name, e.g. "LeftUpperArm".
const char* humanoidBoneName(HumanoidBone bone);

/// The bone called `name` (as humanoidBoneName writes it); false if none.
bool humanoidBoneFromName(const std::string& name, HumanoidBone& out);

/// Whether a humanoid avatar must map the bone: Hips, Spine, Head, and the
/// upper and lower arms and legs, hands and feet of both sides.
bool humanoidBoneRequired(HumanoidBone bone);

/// The nearest bone above `bone` in the humanoid hierarchy (Hips has none:
/// returns Count). Fingers hang from their hand, a finger's later segments
/// from the earlier ones.
HumanoidBone humanoidBoneParent(HumanoidBone bone);

struct HumanoidAvatar {
    std::shared_ptr<const Skeleton> skeleton;

    // Joint index per HumanoidBone; -1 when the bone is not mapped
    std::array<int, kHumanoidBoneCount> joints;

    // Skeleton-space rest transform of every joint
    std::vector<glm::mat4> restGlobals;

    // Rotates skeleton-space directions into the body frame: +X toward the
    // body's left, +Y up (hips to head), +Z forward
    glm::quat bodyFrame{1.0f, 0.0f, 0.0f, 0.0f};

    // Height of the hips above the feet at rest, in skeleton units
    float hipHeight = 1.0f;

    HumanoidAvatar() { joints.fill(-1); }

    int joint(HumanoidBone bone) const { return joints[static_cast<int>(bone)]; }
};

/// Joint indices for the humanoid bones of `skeleton`, found by name: the
/// preset (Mixamo, Unreal mannequin, VRM, Rigify) matching the most joints,
/// then, for bones still missing, a match on name parts (side, body part,
/// finger and segment) whose joint lies below the bone's humanoid parent.
/// Bones without a match are -1.
std::array<int, kHumanoidBoneCount> autoMapHumanoid(const Skeleton& skeleton);

/// An avatar for `skeleton`: autoMapHumanoid with `overrides` (humanoid bone
/// name, joint name) applied on top; an empty joint name unmaps the bone.
/// @throws std::runtime_error for an unknown bone or joint name, or naming
///         the required bones left unmapped, or when the rest pose gives no
///         body frame (hips, head and upper legs at one point or in a line).
std::shared_ptr<HumanoidAvatar> makeHumanoidAvatar(
    std::shared_ptr<const Skeleton> skeleton,
    const std::vector<std::pair<std::string, std::string>>& overrides = {});

/// The avatar's mapping as (humanoid bone name, joint name) pairs, in
/// HumanoidBone order, mapped bones only.
std::vector<std::pair<std::string, std::string>> humanoidMapping(const HumanoidAvatar& avatar);

} // namespace Shoonyakasha
