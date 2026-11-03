//
// AnimatorEvaluator.h - Advance and evaluate AnimatorComponents
//
// CPU-side; no GPU work. SkeletalAnimationSystem calls update() each frame and
// uploads the bone matrices it leaves in the skinned meshes' SkeletonComponents.
//

#pragma once

#include "ECS/AnimatorComponents.h"
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

class AnimatorEvaluator {
public:
    /// With a controller, ControllerEvaluator::advance. Otherwise advance the
    /// animator's clip states by deltaTime * speed of each state *
    /// animator.speed, and the fade by |deltaTime * animator.speed|. Looping
    /// clips wrap; others stop at their end (or start, when playing
    /// backwards) and hold that frame. A finished fade clears `previous`.
    static void advance(AnimatorComponent& animator, float deltaTime);

    /// Compute animator.pose: with a controller, by ControllerEvaluator::
    /// evaluate; otherwise the current clip, blended from the previous one
    /// while fading, or the rest pose with no clip, and with root motion on,
    /// rootMotionDelta from those clips by their blend weights. Then
    /// globalTransforms and boneMatrices.
    static void evaluate(AnimatorComponent& animator);

    /// For every AnimatorComponent (applyIK after evaluate): with root motion on, set rootMotionUp to
    /// world +Y in the root motion joint's parent space (through the world
    /// matrix of the animator's first skinned mesh, or of its own entity when
    /// it has none, and the parent's last global transform); advance(),
    /// evaluate(), add rootMotionDelta to rootMotionWorld through the same
    /// transforms, and copy its boneMatrices into the
    /// SkeletonComponent of each of its skinned meshes that holds the same
    /// skeleton, marking it dirty. Then place every entity with a
    /// JointAttachmentComponent and a transform (see placeAttachments).
    static void update(entt::registry& registry, float deltaTime);

    /// Set the world matrix of each entity with a JointAttachmentComponent to
    /// space * globalTransforms[joint] * offset, where space is the world
    /// matrix of the animator's first skinned mesh (or of the animator's own
    /// entity when it has none), and recompute the world matrices of the
    /// entity's descendants from their local matrices.
    static void placeAttachments(entt::registry& registry);

    /// Add an AnimatorComponent to `root`. Its skeleton is that of the first
    /// skinned mesh entity found in root's subtree (root first, then children
    /// depth-first); every skinned mesh in the subtree with that skeleton is
    /// driven by it. The clips and playback state are taken from the first of
    /// those meshes' AnimationPlaybackComponent, when it has one. Returns
    /// nullptr when the subtree has no skinned mesh; returns the existing
    /// component when root has one already.
    static AnimatorComponent* createAnimator(entt::registry& registry, entt::entity root);

    /// PoseOps::sample of the clip at `time`; with the animator's root motion
    /// on, the root motion joint's translation then has its movement since
    /// time 0 perpendicular to animator.rootMotionUp taken out.
    static void sampleClip(const AnimatorComponent& animator, const AnimationClip& clip, float time, Pose& out);

    /// The movement of joint `joint` perpendicular to `up` (unit length) from
    /// time t0 to t1 of the clip, passing its end `wraps` times (negative: its
    /// start, playing backwards).
    static glm::vec3 rootDisplacement(const AnimationClip& clip, const Skeleton& skeleton, int joint,
                                      float t0, float t1, int wraps,
                                      const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

    /// Copy globalTransforms into animatedGlobals; then move the hips by
    /// hipsOffset (world space, through `spaceWorld`, the world matrix the
    /// skeleton is drawn with), solve each goal's chain by two-bone IK with
    /// its target, pole (or the chain's default pole) and end rotation
    /// brought into skeleton space, and recompute globalTransforms and
    /// boneMatrices. Goals naming no chain are skipped.
    static void applyIK(AnimatorComponent& animator, const glm::mat4& spaceWorld);

    /// The chain called `name`. On a humanoid, "LeftFoot" and "RightFoot"
    /// (upper leg, lower leg, foot; knee bending forward) and "LeftHand" and
    /// "RightHand" (upper arm, lower arm, hand; elbow bending back) are added
    /// on first use. nullptr when there is no such chain.
    static const IKChain* chainFor(AnimatorComponent& animator, const std::string& name);

    /// Add or replace a chain of joints `upper`, `middle`, `end`. Returns
    /// false when middle is not below upper or end not below middle.
    static bool setChain(AnimatorComponent& animator, const std::string& name, int upper, int middle, int end);

    /// Turn root motion on or off. `joint` < 0 picks the skeleton's root joint.
    /// Clears the collected movement.
    static void setRootMotion(AnimatorComponent& animator, bool enabled, int joint = -1);

    /// Bind each of `clips` to the animator's skeleton (bindClip) and append
    /// those with at least one channel. With a non-empty `name`, a single
    /// added clip is called `name`, and several are called "<name>:<clip name>".
    /// Returns the names of the clips added.
    static std::vector<std::string> addClips(AnimatorComponent& animator,
                                             const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                             const std::string& name = "");
};

} // namespace Shoonyakasha
