//
// AnimatorComponents.h - ECS components for per-model skeletal animation
//
// Defines:
//   - AnimatorClipState: one clip's playback position
//   - AnimatorComponent: plays clips on one skeleton and drives every skinned
//     mesh entity of that skeleton listed in skinnedMeshes
//   - JointAttachmentComponent: places an entity on a joint of an animator
//   - ControllerRuntime: the state of an animation state machine playing on
//     an animator
//
// AnimatorEvaluator (Animation/AnimatorEvaluator.h) advances and evaluates
// these components.
//

#pragma once

#include "Resources/AnimationData.h"
#include "Animation/AnimationController.h"
#include "Animation/Humanoid.h"
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

    // Time before the last advance, and how many times that advance wrapped
    // past the clip's end (negative: past its start)
    float previousTime = 0.0f;
    int wraps = 0;
};

// One state of a controller layer being played. Phase is normalized time.
struct ControllerStatePlayback {
    int state = -1;               // index into the layer's states; -1 = none
    float phase = 0.0f;
    float previousPhase = 0.0f;
    int wraps = 0;                // as AnimatorClipState::wraps
    bool entered = false;         // set until the state's first advance
};

struct ControllerLayerRuntime {
    ControllerStatePlayback current;
    ControllerStatePlayback previous;   // the state faded out of
    float fadeDuration = 0.0f;
    float fadeElapsed = 0.0f;
    float weight = 1.0f;
    std::vector<float> mask;            // per joint, 0 or 1; empty = every joint

    bool isFading() const { return previous.state >= 0 && fadeElapsed < fadeDuration; }
};

struct ControllerRuntime {
    std::shared_ptr<const ControllerDefinition> definition;
    std::vector<float> parameters;                 // by definition parameter index
    std::vector<ControllerLayerRuntime> layers;    // by definition layer index

    // Animator clip index of each motion child: [layer][state][child]
    std::vector<std::vector<std::vector<int>>> stateClips;

    // Names of the events fired, oldest first; at most kMaxEvents are kept
    static constexpr size_t kMaxEvents = 256;
    std::vector<std::string> events;

    // Scratch poses
    Pose childPose;
    Pose statePose;
    Pose fadePose;
    Pose additiveDelta;
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

    // When set, the controller produces the pose and play() is not used
    std::shared_ptr<ControllerRuntime> controller;

    // The skeleton's humanoid mapping, for retargeting; nullptr when none
    std::shared_ptr<HumanoidAvatar> humanoid;

    // Root motion: with rootMotion set, the horizontal translation of joint
    // rootMotionJoint (perpendicular to rootMotionUp, in the joint's parent
    // space) is held at each clip's first frame in the pose, and its movement
    // is collected instead. rootMotionUp is world up in that space, set each
    // update. rootMotionDelta is the movement of the last evaluation, in the
    // joint's parent space; rootMotionWorld sums it in world space until taken.
    bool rootMotion = false;
    int rootMotionJoint = -1;
    glm::vec3 rootMotionUp{0.0f, 1.0f, 0.0f};
    glm::vec3 rootMotionDelta{0.0f};
    glm::vec3 rootMotionWorld{0.0f};

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
