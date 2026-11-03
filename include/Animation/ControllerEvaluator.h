//
// ControllerEvaluator.h - Run an animation state machine on an animator
//
// Each layer plays one state, or blends from its previous state to its
// current one while a transition fades. A state's motion is a clip or a blend
// of clips weighted by parameters; the clips of a blend share one normalized
// phase. Layers apply in order over the skeleton's rest pose: override layers
// blend toward their pose, additive layers add their difference from the rest
// pose, each by its weight times its mask.
//

#pragma once

#include "ECS/AnimatorComponents.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Shoonyakasha {

class ControllerEvaluator {
public:
    /// Resolve the definition's clip and joint names against the animator and
    /// return a runtime with every parameter at its default and every layer in
    /// its default state, about to enter it.
    /// @throws std::runtime_error "<path>: <message>" for a clip the animator
    ///         does not have or a mask joint its skeleton does not have.
    static std::shared_ptr<ControllerRuntime> bind(std::shared_ptr<const ControllerDefinition> definition,
                                                   const AnimatorComponent& animator);

    /// Advance animator.controller by deltaTime * animator.speed:
    ///   - each layer's current state, and its previous one while fading; a
    ///     state advances by its speed (times its speed parameter) over its
    ///     duration (a blend's: the children's durations weighted by their
    ///     blend weights). Looping states wrap; others stop at the end;
    ///   - the fade, which ends once it has run for its duration;
    ///   - the current state's events whose time the phase passed (on entry,
    ///     including time 0), appended to the runtime's events;
    ///   - when the layer is not fading, the first transition, in list order,
    ///     that leaves the current state (or any other state, for "any") whose
    ///     conditions hold and whose exit time the phase passed (or reached,
    ///     in a state that does not loop). Taking it clears the triggers its
    ///     conditions test and enters the target state at phase 0.
    static void advance(AnimatorComponent& animator, float deltaTime);

    /// Compute animator.pose from animator.controller, and with root motion
    /// on, animator.rootMotionDelta from the first layer's clips.
    static void evaluate(AnimatorComponent& animator);

    /// Blend weights of 1D thresholds (ascending) at x: the two around x share
    /// the weight linearly; beyond either end, that end has weight 1.
    static std::vector<float> blend1DWeights(const std::vector<float>& thresholds, float x);

    /// Blend weights of 2D positions at p by gradient band interpolation
    /// (freeform cartesian), normalized to sum to 1.
    static std::vector<float> blend2DWeights(const std::vector<glm::vec2>& points, glm::vec2 p);
};

} // namespace Shoonyakasha
