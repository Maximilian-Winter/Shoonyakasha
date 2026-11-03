//
// AnimationClipLoader.h - Read animation clips from glTF / GLB files
//
// Clips read here are not bound to a skeleton: each channel carries the name
// of the node it animates (targetJointName) and a targetJointIndex of -1.
// bindClip() (Animation/ClipBinding.h) maps them onto a skeleton.
//

#pragma once

#include "Resources/AnimationData.h"
#include <filesystem>
#include <memory>
#include <vector>

struct cgltf_data;
struct cgltf_skin;

namespace Shoonyakasha {

/// Every animation in `data`, one clip each. Translation, rotation and scale
/// channels become AnimationChannels; with `withWeights`, morph target weight
/// channels of mesh nodes become MorphWeightChannels. A clip's duration is its
/// latest keyframe time. Clips with no channels are left out. Animations
/// without a name are called "animation_<index>".
std::vector<std::shared_ptr<AnimationClip>> readGltfAnimations(const cgltf_data* data, bool withWeights);

/// The skin's joints as a Skeleton: names (unnamed nodes are "joint_<i>"),
/// inverse bind matrices, the nodes' transforms as default transforms, parents
/// among the skin's joints, name lookup and evaluation order. nullptr for a
/// skin without joints.
std::shared_ptr<Skeleton> readGltfSkeleton(const cgltf_skin* skin);

struct AnimationFile {
    std::shared_ptr<Skeleton> skeleton;                    // the file's first skin
    std::vector<std::shared_ptr<AnimationClip>> clips;     // bound to skeleton
};

/// The first skin of the file at `path` and its clips, bound to it.
/// @throws std::runtime_error when the file cannot be read or parsed, or
///         has no skin.
AnimationFile loadAnimationFile(const std::filesystem::path& path);

/// readGltfAnimations() of the file at `path`, without morph weight channels.
/// @throws std::runtime_error when the file cannot be read or parsed.
std::vector<std::shared_ptr<AnimationClip>> loadAnimationClips(const std::filesystem::path& path);

} // namespace Shoonyakasha
