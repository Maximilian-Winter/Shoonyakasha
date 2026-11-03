//
// ClipBinding.h - Map an animation clip's channels onto a skeleton's joints
//

#pragma once

#include "Resources/AnimationData.h"
#include <memory>

namespace Shoonyakasha {

/// A copy of `clip` whose channels target `skeleton`: each channel's
/// targetJointName is looked up with Skeleton::findJoint and stored as
/// targetJointIndex. Channels with no matching joint are left out, as are
/// morph weight channels. The copy's duration is the latest keyframe time of
/// the channels kept.
std::shared_ptr<AnimationClip> bindClip(const AnimationClip& clip, const Skeleton& skeleton);

} // namespace Shoonyakasha
