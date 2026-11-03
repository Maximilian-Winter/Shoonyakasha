//
// Retarget.h - Bake a clip of one humanoid onto another
//

#pragma once

#include "Animation/Humanoid.h"
#include <memory>

namespace Shoonyakasha {

/// A copy of `clip` (bound to source.skeleton) for target.skeleton, sampled at
/// the clip's keyframe times:
///   - every bone mapped in both avatars gets a rotation channel: the
///     source bone's rotation away from its rest pose, taken through the
///     two body frames, applied to the target bone's rest rotation after
///     turning it so that its rest direction (toward its next mapped bone
///     down the limb) matches the source's;
///   - the target hips get a translation channel: their rest position plus
///     the source hips' movement from rest, scaled by target.hipHeight /
///     source.hipHeight;
///   - target joints not mapped in both keep their rest transform.
/// The copy keeps the clip's name and duration.
std::shared_ptr<AnimationClip> retargetClip(const AnimationClip& clip,
                                            const HumanoidAvatar& source,
                                            const HumanoidAvatar& target);

} // namespace Shoonyakasha
