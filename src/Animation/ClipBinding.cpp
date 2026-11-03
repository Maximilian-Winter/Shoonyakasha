//
// ClipBinding.cpp - Map an animation clip's channels onto a skeleton's joints
//

#include "Animation/ClipBinding.h"
#include <algorithm>

namespace Shoonyakasha {

std::shared_ptr<AnimationClip> bindClip(const AnimationClip& clip, const Skeleton& skeleton) {
    auto bound = std::make_shared<AnimationClip>();
    bound->name = clip.name;
    bound->duration = 0.0f;
    for (const AnimationChannel& channel : clip.channels) {
        const int joint = skeleton.findJoint(channel.targetJointName);
        if (joint < 0) continue;
        AnimationChannel copy = channel;
        copy.targetJointIndex = joint;
        for (float t : copy.timestamps) bound->duration = std::max(bound->duration, t);
        bound->channels.push_back(std::move(copy));
    }
    return bound;
}

} // namespace Shoonyakasha
