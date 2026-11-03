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
    /// such clip.
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

    // Impl is forward-declared (opaque) — public so EngineAPI can wire internals
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace Facade
} // namespace Shoonyakasha
