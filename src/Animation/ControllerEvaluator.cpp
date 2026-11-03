//
// ControllerEvaluator.cpp - Run an animation state machine on an animator
//

#include "Animation/ControllerEvaluator.h"
#include "Animation/AnimatorEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Shoonyakasha {

namespace {

float parameterValue(const ControllerRuntime& runtime, int index, float fallback = 0.0f) {
    return (index >= 0 && index < static_cast<int>(runtime.parameters.size())) ? runtime.parameters[index]
                                                                                : fallback;
}

// Blend weights of a state's motion children with the current parameters.
std::vector<float> motionWeights(const ControllerRuntime& runtime, const ControllerMotion& motion) {
    if (motion.kind == ControllerMotion::Kind::Clip || motion.children.size() == 1) {
        return std::vector<float>(motion.children.size(), 1.0f);
    }
    const float x = parameterValue(runtime, motion.parameterX);
    if (motion.kind == ControllerMotion::Kind::Blend1D) {
        std::vector<float> thresholds;
        for (const auto& child : motion.children) thresholds.push_back(child.position.x);
        return ControllerEvaluator::blend1DWeights(thresholds, x);
    }
    std::vector<glm::vec2> points;
    for (const auto& child : motion.children) points.push_back(child.position);
    return ControllerEvaluator::blend2DWeights(points, glm::vec2(x, parameterValue(runtime, motion.parameterY)));
}

// Duration of a state's motion: its children's durations by their weights.
float stateDuration(const AnimatorComponent& animator, const std::vector<int>& clips,
                    const std::vector<float>& weights) {
    float duration = 0.0f;
    for (size_t i = 0; i < clips.size() && i < weights.size(); ++i) {
        if (const AnimationClip* clip = animator.clipAt(clips[i])) duration += weights[i] * clip->duration;
    }
    return duration;
}

void advanceState(ControllerStatePlayback& playback, const ControllerLayer& layer, int layerIndex,
                  const ControllerRuntime& runtime, const AnimatorComponent& animator, float scaled) {
    playback.previousPhase = playback.phase;
    playback.wraps = 0;
    if (playback.state < 0) return;
    const ControllerState& state = layer.states[playback.state];
    const auto& clips = runtime.stateClips[layerIndex][playback.state];
    const float duration = stateDuration(animator, clips, motionWeights(runtime, state.motion));
    if (duration <= 0.0f) return;

    float speed = state.speed;
    if (state.speedParameter >= 0) speed *= parameterValue(runtime, state.speedParameter, 1.0f);
    float phase = playback.phase + scaled * speed / duration;
    if (state.loop) {
        const float cycles = std::floor(phase);
        playback.wraps = static_cast<int>(cycles);
        phase -= cycles;
        if (phase >= 1.0f || phase < 0.0f) phase = 0.0f;
    } else {
        phase = std::clamp(phase, 0.0f, 1.0f);
    }
    playback.phase = phase;
}

// Whether the phase passed normalized time `t` in the last advance. On entry
// the phase counts as starting just before 0.
bool passed(const ControllerStatePlayback& playback, float t) {
    const float from = playback.entered ? -std::numeric_limits<float>::min() : playback.previousPhase;
    const float to = playback.phase;
    if (playback.wraps > 0) return t > from || t <= to;
    if (playback.wraps < 0) return t < from || t >= to;
    if (to >= from) return from < t && t <= to;
    return to <= t && t < from;
}

bool conditionHolds(const ControllerRuntime& runtime, const ControllerCondition& condition) {
    const float v = parameterValue(runtime, condition.parameter);
    switch (condition.op) {
        case ControllerConditionOp::IsSet:    return v != 0.0f;
        case ControllerConditionOp::Greater:  return v > condition.value;
        case ControllerConditionOp::Less:     return v < condition.value;
        case ControllerConditionOp::Equal:    return v == condition.value;
        case ControllerConditionOp::NotEqual: return v != condition.value;
    }
    return false;
}

// Sample a state's motion into runtime.statePose, and add the root motion
// joint's movement over the last advance, times `weight`, to `displacement`.
void statePose(ControllerRuntime& runtime, AnimatorComponent& animator, int layerIndex,
               const ControllerStatePlayback& playback, float weight, glm::vec3* displacement, Pose& out) {
    const ControllerLayer& layer = runtime.definition->layers[layerIndex];
    if (playback.state < 0) {
        out.setRest(*animator.skeleton);
        return;
    }
    const ControllerState& state = layer.states[playback.state];
    const auto& clips = runtime.stateClips[layerIndex][playback.state];
    const std::vector<float> weights = motionWeights(runtime, state.motion);

    float total = 0.0f;
    for (size_t i = 0; i < clips.size(); ++i) {
        const AnimationClip* clip = animator.clipAt(clips[i]);
        if (!clip || weights[i] <= 0.0f) continue;
        const float time = playback.phase * clip->duration;
        AnimatorEvaluator::sampleClip(animator, *clip, time, total == 0.0f ? out : runtime.childPose);
        if (total > 0.0f) PoseOps::blend(out, runtime.childPose, weights[i] / (total + weights[i]), out);
        total += weights[i];
        if (displacement) {
            *displacement += weight * weights[i] * AnimatorEvaluator::rootDisplacement(
                *clip, *animator.skeleton, animator.rootMotionJoint,
                playback.previousPhase * clip->duration, time, playback.wraps);
        }
    }
    if (total == 0.0f) out.setRest(*animator.skeleton);
}

void markSubtree(const Skeleton& skeleton, int joint, std::vector<float>& mask) {
    const int count = static_cast<int>(skeleton.joints.size());
    for (int i = 0; i < count; ++i) {
        int steps = 0;
        for (int j = i; j >= 0 && j < count && steps <= count; j = skeleton.joints[j].parentIndex, ++steps) {
            if (j == joint) {
                mask[i] = 1.0f;
                break;
            }
        }
    }
}

} // namespace

std::shared_ptr<ControllerRuntime> ControllerEvaluator::bind(std::shared_ptr<const ControllerDefinition> definition,
                                                             const AnimatorComponent& animator) {
    if (!definition) throw std::runtime_error("controller: no definition");
    if (!animator.skeleton) throw std::runtime_error("controller: the animator has no skeleton");

    auto runtime = std::make_shared<ControllerRuntime>();
    runtime->definition = definition;
    for (const auto& parameter : definition->parameters) runtime->parameters.push_back(parameter.defaultValue);

    for (size_t l = 0; l < definition->layers.size(); ++l) {
        const ControllerLayer& layer = definition->layers[l];
        const std::string path = "layers[" + std::to_string(l) + "]";
        auto& layerClips = runtime->stateClips.emplace_back();
        for (const ControllerState& state : layer.states) {
            auto& clips = layerClips.emplace_back();
            for (const auto& child : state.motion.children) {
                const int index = animator.findClip(child.clip);
                if (index < 0) throw std::runtime_error(path + ".states." + state.name + ": no clip '" + child.clip + "'");
                clips.push_back(index);
            }
        }

        ControllerLayerRuntime layerRuntime;
        layerRuntime.weight = layer.weight;
        if (!layer.mask.empty()) {
            layerRuntime.mask.assign(animator.skeleton->jointCount(), 0.0f);
            for (size_t m = 0; m < layer.mask.size(); ++m) {
                const int joint = animator.skeleton->findJoint(layer.mask[m]);
                if (joint < 0) {
                    throw std::runtime_error(path + ".mask[" + std::to_string(m) + "]: no joint '" + layer.mask[m] + "'");
                }
                markSubtree(*animator.skeleton, joint, layerRuntime.mask);
            }
        }
        layerRuntime.current.state = layer.defaultState;
        layerRuntime.current.entered = true;
        runtime->layers.push_back(std::move(layerRuntime));
    }
    return runtime;
}

void ControllerEvaluator::advance(AnimatorComponent& animator, float deltaTime) {
    if (!animator.controller) return;
    ControllerRuntime& runtime = *animator.controller;
    const ControllerDefinition& definition = *runtime.definition;
    const float scaled = deltaTime * animator.speed;

    for (size_t l = 0; l < runtime.layers.size(); ++l) {
        const ControllerLayer& layer = definition.layers[l];
        ControllerLayerRuntime& lr = runtime.layers[l];
        const int layerIndex = static_cast<int>(l);

        advanceState(lr.current, layer, layerIndex, runtime, animator, scaled);
        if (lr.previous.state >= 0) {
            advanceState(lr.previous, layer, layerIndex, runtime, animator, scaled);
            lr.fadeElapsed += std::abs(scaled);
            if (lr.fadeElapsed >= lr.fadeDuration) {
                lr.previous = ControllerStatePlayback{};
                lr.fadeElapsed = 0.0f;
                lr.fadeDuration = 0.0f;
            }
        }

        if (lr.current.state >= 0) {
            const ControllerState& state = layer.states[lr.current.state];
            for (const ControllerEvent& event : state.events) {
                if (!passed(lr.current, event.time)) continue;
                if (runtime.events.size() >= ControllerRuntime::kMaxEvents) runtime.events.erase(runtime.events.begin());
                runtime.events.push_back(event.name);
            }
        }

        bool took = false;
        if (!lr.isFading() && lr.current.state >= 0) {
            const bool loops = layer.states[lr.current.state].loop;
            for (const ControllerTransition& transition : layer.transitions) {
                if (transition.from < 0 ? transition.to == lr.current.state : transition.from != lr.current.state) {
                    continue;
                }
                if (transition.exitTime >= 0.0f && !passed(lr.current, transition.exitTime) &&
                    !(!loops && lr.current.phase >= transition.exitTime)) {
                    continue;
                }
                bool holds = true;
                for (const auto& condition : transition.conditions) holds = holds && conditionHolds(runtime, condition);
                if (!holds) continue;

                for (const auto& condition : transition.conditions) {
                    if (definition.parameters[condition.parameter].type == ControllerParameterType::Trigger) {
                        runtime.parameters[condition.parameter] = 0.0f;
                    }
                }
                lr.current.entered = false;
                if (transition.duration > 0.0f) {
                    lr.previous = lr.current;
                    lr.fadeDuration = transition.duration;
                } else {
                    lr.previous = ControllerStatePlayback{};
                    lr.fadeDuration = 0.0f;
                }
                lr.fadeElapsed = 0.0f;
                lr.current = ControllerStatePlayback{};
                lr.current.state = transition.to;
                lr.current.entered = true;
                took = true;
                break;
            }
        }
        if (!took) lr.current.entered = false;
    }
}

void ControllerEvaluator::evaluate(AnimatorComponent& animator) {
    if (!animator.controller || !animator.skeleton) return;
    ControllerRuntime& runtime = *animator.controller;
    const Skeleton& skeleton = *animator.skeleton;
    const uint32_t joints = skeleton.jointCount();

    Pose& result = animator.pose;
    result.setRest(skeleton);
    Pose rest = result;
    glm::vec3 displacement(0.0f);

    std::vector<float> weights;
    for (size_t l = 0; l < runtime.layers.size(); ++l) {
        const ControllerLayer& layer = runtime.definition->layers[l];
        const ControllerLayerRuntime& lr = runtime.layers[l];
        const int layerIndex = static_cast<int>(l);
        glm::vec3* motion = (l == 0 && animator.rootMotion) ? &displacement : nullptr;

        const float fade = lr.isFading() && lr.fadeDuration > 0.0f ? lr.fadeElapsed / lr.fadeDuration : 1.0f;
        statePose(runtime, animator, layerIndex, lr.current, fade, motion, runtime.statePose);
        if (lr.isFading()) {
            statePose(runtime, animator, layerIndex, lr.previous, 1.0f - fade, motion, runtime.fadePose);
            PoseOps::blend(runtime.fadePose, runtime.statePose, fade, runtime.statePose);
        }

        weights.assign(joints, lr.weight);
        if (!lr.mask.empty()) {
            for (uint32_t j = 0; j < joints && j < lr.mask.size(); ++j) weights[j] *= lr.mask[j];
        }
        if (layer.mode == ControllerLayerMode::Override) {
            PoseOps::blendPerJoint(result, runtime.statePose, weights, result);
        } else {
            PoseOps::makeAdditive(runtime.statePose, rest, runtime.additiveDelta);
            PoseOps::applyAdditivePerJoint(result, runtime.additiveDelta, weights, result);
        }
    }
    animator.rootMotionDelta = animator.rootMotion ? displacement : glm::vec3(0.0f);
}

std::vector<float> ControllerEvaluator::blend1DWeights(const std::vector<float>& thresholds, float x) {
    std::vector<float> weights(thresholds.size(), 0.0f);
    if (thresholds.empty()) return weights;
    if (x <= thresholds.front()) {
        weights.front() = 1.0f;
        return weights;
    }
    if (x >= thresholds.back()) {
        weights.back() = 1.0f;
        return weights;
    }
    for (size_t i = 0; i + 1 < thresholds.size(); ++i) {
        if (x >= thresholds[i] && x <= thresholds[i + 1]) {
            const float span = thresholds[i + 1] - thresholds[i];
            const float t = span > 0.0f ? (x - thresholds[i]) / span : 0.0f;
            weights[i] = 1.0f - t;
            weights[i + 1] = t;
            break;
        }
    }
    return weights;
}

std::vector<float> ControllerEvaluator::blend2DWeights(const std::vector<glm::vec2>& points, glm::vec2 p) {
    const size_t count = points.size();
    std::vector<float> weights(count, 0.0f);
    if (count == 0) return weights;
    if (count == 1) {
        weights[0] = 1.0f;
        return weights;
    }
    float total = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float h = 1.0f;
        for (size_t j = 0; j < count; ++j) {
            if (i == j) continue;
            const glm::vec2 edge = points[j] - points[i];
            const float lengthSquared = glm::dot(edge, edge);
            if (lengthSquared <= 0.0f) continue;
            h = std::min(h, 1.0f - glm::dot(p - points[i], edge) / lengthSquared);
        }
        weights[i] = std::max(h, 0.0f);
        total += weights[i];
    }
    if (total > 0.0f) {
        for (float& w : weights) w /= total;
        return weights;
    }
    size_t nearest = 0;
    for (size_t i = 1; i < count; ++i) {
        if (glm::dot(p - points[i], p - points[i]) < glm::dot(p - points[nearest], p - points[nearest])) nearest = i;
    }
    weights[nearest] = 1.0f;
    return weights;
}

} // namespace Shoonyakasha
