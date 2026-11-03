//
// AnimatorComponents.h - ECS components for per-model skeletal animation
//
// Defines:
//   - AnimatorClipState: one clip's playback position
//   - AnimatorComponent: plays clips on one skeleton and drives every skinned
//     mesh entity of that skeleton listed in skinnedMeshes
//   - JointAttachmentComponent: places an entity on a joint of an animator
//
// AnimatorEvaluator (Animation/AnimatorEvaluator.h) advances and evaluates
// these components.
//

#pragma once

#include "Resources/AnimationData.h"
#include "Animation/Pose.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

struct AnimatorClipState {
    int clip = -1;          // index into AnimatorComponent::clips; -1 = none
    float time = 0.0f;      // seconds
    float speed = 1.0f;
    bool loop = true;
    bool playing = false;
};

struct AnimatorComponent {
    std::shared_ptr<Skeleton> skeleton;

    // Clips bound to `skeleton`
    std::vector<std::shared_ptr<AnimationClip>> clips;

    // Skinned mesh entities whose SkeletonComponent holds `skeleton`. They take
    // their bone matrices from this animator.
    std::vector<entt::entity> skinnedMeshes;

    // Multiplies the speed of every clip state
    float speed = 1.0f;

    // The clip being played, and the clip it fades in from
    AnimatorClipState current;
    AnimatorClipState previous;
    float fadeDuration = 0.0f;
    float fadeElapsed = 0.0f;

    // Results of the last evaluation
    Pose pose;
    std::vector<glm::mat4> globalTransforms;  // skeleton space, per joint
    std::vector<glm::mat4> boneMatrices;

    // Scratch pose for the clip fading out
    Pose fadePose;

    /// Index of the first clip called `name`; -1 if there is none.
    int findClip(const std::string& name) const {
        for (size_t i = 0; i < clips.size(); ++i) {
            if (clips[i] && clips[i]->name == name) return static_cast<int>(i);
        }
        return -1;
    }

    /// Play clip `clipIndex` from its start. With `fadeSeconds` > 0 and a clip
    /// already set, the pose blends from that clip, which keeps playing, to the
    /// new one over `fadeSeconds`. Returns false for an index out of range.
    bool play(int clipIndex, float fadeSeconds = 0.0f, bool loop = true) {
        if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size())) return false;
        if (fadeSeconds > 0.0f && current.clip >= 0) {
            previous = current;
            fadeDuration = fadeSeconds;
            fadeElapsed = 0.0f;
        } else {
            previous = AnimatorClipState{};
            fadeDuration = 0.0f;
            fadeElapsed = 0.0f;
        }
        current = AnimatorClipState{};
        current.clip = clipIndex;
        current.loop = loop;
        current.playing = true;
        return true;
    }

    /// Stop playback: no clip, no fade. The pose returns to the rest pose.
    void stop() {
        current = AnimatorClipState{};
        previous = AnimatorClipState{};
        fadeDuration = 0.0f;
        fadeElapsed = 0.0f;
    }

    bool isFading() const { return previous.clip >= 0 && fadeElapsed < fadeDuration; }

    const AnimationClip* clipAt(int index) const {
        return (index >= 0 && index < static_cast<int>(clips.size())) ? clips[index].get() : nullptr;
    }
};

// Places the entity on joint `joint` of the animator on entity `animator`:
// each frame its world matrix becomes the skinned mesh's world matrix times
// the joint's skeleton-space transform times `offset`.
struct JointAttachmentComponent {
    entt::entity animator = entt::null;
    int joint = -1;
    glm::mat4 offset{1.0f};
};

} // namespace Shoonyakasha
