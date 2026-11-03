//
// Facade/AnimationAPI.h - Python-friendly control of per-model animators
//
// An animator lives on a model's root entity and drives every skinned mesh
// of one skeleton below it. Clips are addressed by name. No EnTT in this
// header.
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Shoonyakasha {
namespace Facade {

class AnimationAPI {
public:
    AnimationAPI();
    ~AnimationAPI();

    AnimationAPI(const AnimationAPI&) = delete;
    AnimationAPI& operator=(const AnimationAPI&) = delete;

    // ═══════════════════════════════════════════════════════════
    // Animators
    // ═══════════════════════════════════════════════════════════

    /// Add an animator to `root`, driving the skinned meshes of the first
    /// skeleton found in root's subtree. It takes over the clips and playback
    /// state of the first such mesh. Returns false when the subtree has no
    /// skinned mesh; true when root already has an animator.
    bool createAnimator(EntityHandle root);

    bool hasAnimator(EntityHandle entity) const;

    // ═══════════════════════════════════════════════════════════
    // Clips
    // ═══════════════════════════════════════════════════════════

    /// Read the animations of a glTF / GLB file and add those that animate
    /// joints of the animator's skeleton, matched by joint name. With a
    /// non-empty `name`, a single added clip is called `name` and several are
    /// called "<name>:<clip name>". Returns the names of the clips added.
    /// @throws std::runtime_error when the file cannot be read, or the entity
    ///         has no animator.
    std::vector<std::string> loadClips(EntityHandle animator, const std::string& path,
                                       const std::string& name = "");

    std::vector<std::string> getClipNames(EntityHandle animator) const;

    /// Duration in seconds of the first clip called `clip`; 0 if there is none.
    float getClipDuration(EntityHandle animator, const std::string& clip) const;

    // ═══════════════════════════════════════════════════════════
    // Playback
    // ═══════════════════════════════════════════════════════════

    /// Play the first clip called `clip` from its start, blending from the
    /// clip playing before over `fadeSeconds`. Returns false when there is no
    /// such clip, or while a controller is set.
    bool play(EntityHandle animator, const std::string& clip, float fadeSeconds = 0.0f,
              bool loop = true);

    /// Stop playback; the skeleton returns to its rest pose.
    void stop(EntityHandle animator);

    /// Whether the current clip is advancing. A clip played without looping
    /// stops at its end and holds its last frame.
    bool isPlaying(EntityHandle animator) const;

    /// Name of the current clip; empty when there is none.
    std::string getCurrentClip(EntityHandle animator) const;

    /// Whether the animator is blending from a previous clip.
    bool isFading(EntityHandle animator) const;

    float getTime(EntityHandle animator) const;
    void setTime(EntityHandle animator, float seconds);

    /// Multiplier on the advance of every clip; 0 pauses.
    float getSpeed(EntityHandle animator) const;
    void setSpeed(EntityHandle animator, float speed);

    // ═══════════════════════════════════════════════════════════
    // Joints
    // ═══════════════════════════════════════════════════════════

    std::vector<std::string> getJointNames(EntityHandle animator) const;

    /// World position of a joint as of the last evaluation.
    /// @throws std::runtime_error for an unknown animator or joint.
    glm::vec3 getJointWorldPosition(EntityHandle animator, const std::string& joint) const;

    /// Place `entity` on a joint each frame, offset in the joint's space by a
    /// translation and Euler rotation (radians, applied as the transform
    /// component applies them: yaw, then pitch, then roll). Returns false for
    /// an unknown animator or joint, or an entity without a transform.
    bool attach(EntityHandle entity, EntityHandle animator, const std::string& joint,
                const glm::vec3& offsetPosition = glm::vec3(0.0f),
                const glm::vec3& offsetRotation = glm::vec3(0.0f));

    /// Stop placing `entity` on a joint.
    void detach(EntityHandle entity);

    // ═══════════════════════════════════════════════════════════
    // Controller (state machine)
    // ═══════════════════════════════════════════════════════════

    /// Play an animation state machine on the animator, from its JSON form
    /// (Animation/AnimationController.h). Its clip and joint names must be
    /// the animator's. While a controller is set, play() returns false.
    /// @throws std::runtime_error "<path>: <message>" for an invalid
    ///         definition, or when the entity has no animator.
    void setController(EntityHandle animator, const std::string& json);

    /// Remove the controller; the animator plays clips again.
    void clearController(EntityHandle animator);

    bool hasController(EntityHandle animator) const;

    /// Set a parameter of the animator's controller. setFloat sets float
    /// parameters, setInt int ones, setBool bool ones; setTrigger sets a
    /// trigger until a transition uses it.
    /// @throws std::runtime_error without a controller, for an unknown
    ///         parameter, or for a parameter of another type.
    void setFloat(EntityHandle animator, const std::string& parameter, float value);
    void setInt(EntityHandle animator, const std::string& parameter, int value);
    void setBool(EntityHandle animator, const std::string& parameter, bool value);
    void setTrigger(EntityHandle animator, const std::string& parameter);
    void resetTrigger(EntityHandle animator, const std::string& parameter);

    /// Value of a parameter of any type (bools and triggers as 0 or 1).
    /// @throws std::runtime_error without a controller or for an unknown parameter.
    float getParameter(EntityHandle animator, const std::string& parameter) const;

    /// Name of the layer's current state.
    /// @throws std::runtime_error without a controller or for an unknown layer.
    std::string getCurrentState(EntityHandle animator, const std::string& layer) const;

    /// Whether the layer is fading from its previous state.
    /// @throws std::runtime_error without a controller or for an unknown layer.
    bool isInTransition(EntityHandle animator, const std::string& layer) const;

    /// Weight of a layer, 0..1 as a rule; starts at the definition's.
    /// @throws std::runtime_error without a controller or for an unknown layer.
    void setLayerWeight(EntityHandle animator, const std::string& layer, float weight);
    float getLayerWeight(EntityHandle animator, const std::string& layer) const;

    /// Names of the controller's events fired since the last call, oldest
    /// first, and clear them. At most the last 256 are kept.
    std::vector<std::string> takeEvents(EntityHandle animator);

    // ═══════════════════════════════════════════════════════════
    // Root motion
    // ═══════════════════════════════════════════════════════════

    /// Turn root motion on or off. While on, the horizontal translation
    /// (perpendicular to world up) of `joint` (the skeleton's root joint when
    /// empty) stays at each clip's first frame, and its movement is collected
    /// in world space instead.
    /// Clears the collected movement.
    /// @throws std::runtime_error when the entity has no animator or the
    ///         skeleton has no such joint.
    void setRootMotion(EntityHandle animator, bool enabled, const std::string& joint = "");

    bool isRootMotion(EntityHandle animator) const;

    /// The world-space movement collected since the last call, and clear it.
    glm::vec3 takeRootMotion(EntityHandle animator);

    // ═══════════════════════════════════════════════════════════
    // Humanoid
    // ═══════════════════════════════════════════════════════════

    using BoneMapping = std::vector<std::pair<std::string, std::string>>;

    /// Map the animator's skeleton onto the humanoid bones (Animation/
    /// Humanoid.h): by joint name, with `mapping` (humanoid bone name, joint
    /// name) on top; an empty joint name unmaps a bone. Returns the mapping
    /// made, mapped bones only.
    /// @throws std::runtime_error for an unknown bone or joint, a required
    ///         bone left unmapped, or an entity without an animator.
    BoneMapping makeHumanoid(EntityHandle animator, const BoneMapping& mapping = {});

    bool isHumanoid(EntityHandle animator) const;

    /// The humanoid mapping; empty when the animator has none.
    BoneMapping getHumanoidMapping(EntityHandle animator) const;

    /// Retarget clips of humanoid animator `source` (all of them when `clips`
    /// is empty) onto humanoid animator `target` (Animation/Retarget.h) and
    /// add them, named as loadClips names. Returns the names added.
    /// @throws std::runtime_error when either is not a humanoid, or for an
    ///         unknown clip.
    std::vector<std::string> retargetClips(EntityHandle target, EntityHandle source,
                                           const std::vector<std::string>& clips = {},
                                           const std::string& name = "");

    /// Read a glTF / GLB file with a skin and animations, map its skeleton
    /// onto the humanoid bones (with `mapping` on top, as makeHumanoid), and
    /// retarget its clips onto humanoid animator `target`, named as loadClips
    /// names. Returns the names added.
    /// @throws std::runtime_error when the file cannot be read or has no
    ///         skin, its skeleton cannot be mapped, or the target is not a
    ///         humanoid.
    std::vector<std::string> loadHumanoidClips(EntityHandle target, const std::string& path,
                                               const std::string& name = "", const BoneMapping& mapping = {});

    // Impl is forward-declared (opaque) — public so EngineAPI can wire internals
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
