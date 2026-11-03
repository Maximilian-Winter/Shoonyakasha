//
// AnimatorEvaluator.cpp - Advance and evaluate AnimatorComponents
//

#include "Animation/AnimatorEvaluator.h"
#include "Animation/AnimationEvaluator.h"
#include "Animation/ClipBinding.h"
#include "Animation/ControllerEvaluator.h"
#include "Animation/IK.h"
#include "ECS/Core.h"
#include "ECS/SkeletonComponents.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <functional>

namespace Shoonyakasha {

namespace {

void step(AnimatorClipState& state, const AnimationClip* clip, float deltaTime) {
    state.previousTime = state.time;
    state.wraps = 0;
    if (!state.playing || !clip || clip->duration <= 0.0f) return;
    state.time += deltaTime * state.speed;
    if (state.loop) {
        const float cycles = std::floor(state.time / clip->duration);
        state.wraps = static_cast<int>(cycles);
        state.time -= cycles * clip->duration;
        if (state.time < 0.0f) state.time = 0.0f;
        if (state.time >= clip->duration) state.time = 0.0f;
    } else if (state.time >= clip->duration) {
        state.time = clip->duration;
        state.playing = false;
    } else if (state.time < 0.0f) {
        state.time = 0.0f;
        state.playing = false;
    }
}

glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(r[c]);
        if (length > 0.0f) r[c] /= length;
    }
    return glm::normalize(glm::quat_cast(r));
}

glm::quat tiltBetween(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    const float d = glm::dot(a, b);
    if (d > 0.999999f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -0.999999f) return glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 axis = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, axis.x, axis.y, axis.z));
}

bool isBelow(const Skeleton& skeleton, int joint, int ancestor) {
    const int count = static_cast<int>(skeleton.joints.size());
    int steps = 0;
    for (int p = joint >= 0 && joint < count ? skeleton.joints[joint].parentIndex : -1;
         p >= 0 && p < count && steps <= count; p = skeleton.joints[p].parentIndex, ++steps) {
        if (p == ancestor) return true;
    }
    return false;
}

void updateDescendants(entt::registry& registry, entt::entity entity, const glm::mat4& world) {
    const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(entity);
    if (!hierarchy) return;
    for (entt::entity child : hierarchy->children) {
        auto* transform = registry.try_get<ECS::TransformComponent>(child);
        if (!transform) continue;
        transform->worldMatrix = world * transform->localMatrix;
        updateDescendants(registry, child, transform->worldMatrix);
    }
}

// The root motion joint's movement over the state's last advance.
glm::vec3 stateDisplacement(const AnimatorComponent& animator, const AnimatorClipState& state) {
    const AnimationClip* clip = animator.clipAt(state.clip);
    if (!clip || !animator.skeleton) return glm::vec3(0.0f);
    return AnimatorEvaluator::rootDisplacement(*clip, *animator.skeleton, animator.rootMotionJoint,
                                               state.previousTime, state.time, state.wraps, animator.rootMotionUp);
}

} // namespace

void AnimatorEvaluator::sampleClip(const AnimatorComponent& animator, const AnimationClip& clip, float time,
                                   Pose& out) {
    PoseOps::sample(clip, time, *animator.skeleton, out);
    const int joint = animator.rootMotionJoint;
    if (animator.rootMotion && joint >= 0 && joint < static_cast<int>(out.jointCount())) {
        const glm::vec3 start = AnimationEvaluator::sampleJointTranslation(clip, joint, 0.0f, *animator.skeleton);
        const glm::vec3 moved = out.translations[joint] - start;
        const glm::vec3& up = animator.rootMotionUp;
        out.translations[joint] -= moved - glm::dot(moved, up) * up;
    }
}

glm::vec3 AnimatorEvaluator::rootDisplacement(const AnimationClip& clip, const Skeleton& skeleton, int joint,
                                              float t0, float t1, int wraps, const glm::vec3& up) {
    if (joint < 0) return glm::vec3(0.0f);
    auto at = [&](float t) { return AnimationEvaluator::sampleJointTranslation(clip, joint, t, skeleton); };
    const float end = clip.duration;
    glm::vec3 d(0.0f);
    if (wraps == 0) {
        d = at(t1) - at(t0);
    } else if (wraps > 0) {
        const glm::vec3 cycle = at(end) - at(0.0f);
        d = (at(end) - at(t0)) + static_cast<float>(wraps - 1) * cycle + (at(t1) - at(0.0f));
    } else {
        const glm::vec3 cycle = at(0.0f) - at(end);
        d = (at(0.0f) - at(t0)) + static_cast<float>(-wraps - 1) * cycle + (at(t1) - at(end));
    }
    return d - glm::dot(d, up) * up;
}

bool AnimatorEvaluator::setChain(AnimatorComponent& animator, const std::string& name, int upper, int middle,
                                 int end) {
    if (!animator.skeleton || !isBelow(*animator.skeleton, middle, upper) || !isBelow(*animator.skeleton, end, middle)) {
        return false;
    }
    IKChain chain;
    chain.name = name;
    chain.upper = upper;
    chain.middle = middle;
    chain.end = end;
    for (IKChain& existing : animator.ikChains) {
        if (existing.name == name) {
            existing = chain;
            return true;
        }
    }
    animator.ikChains.push_back(chain);
    return true;
}

const IKChain* AnimatorEvaluator::chainFor(AnimatorComponent& animator, const std::string& name) {
    if (const IKChain* chain = animator.findChain(name)) return chain;
    if (!animator.humanoid) return nullptr;
    const HumanoidAvatar& avatar = *animator.humanoid;
    using B = HumanoidBone;
    struct Limb { const char* name; B upper, middle, end; float forward; };
    static const Limb limbs[] = {
        {"LeftFoot", B::LeftUpperLeg, B::LeftLowerLeg, B::LeftFoot, 1.0f},
        {"RightFoot", B::RightUpperLeg, B::RightLowerLeg, B::RightFoot, 1.0f},
        {"LeftHand", B::LeftUpperArm, B::LeftLowerArm, B::LeftHand, -1.0f},
        {"RightHand", B::RightUpperArm, B::RightLowerArm, B::RightHand, -1.0f},
    };
    for (const Limb& limb : limbs) {
        if (name != limb.name) continue;
        if (!setChain(animator, name, avatar.joint(limb.upper), avatar.joint(limb.middle), avatar.joint(limb.end))) {
            return nullptr;
        }
        IKChain& chain = animator.ikChains.back();
        const glm::vec3 pole = glm::inverse(avatar.bodyFrame) * glm::vec3(0.0f, 0.0f, limb.forward);
        const int parent = animator.skeleton->joints[chain.upper].parentIndex;
        const glm::quat parentRest = parent >= 0 && parent < static_cast<int>(avatar.restGlobals.size())
                                         ? rotationOf(avatar.restGlobals[parent]) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        chain.hasDefaultPole = true;
        chain.defaultPole = glm::inverse(parentRest) * pole;
        return &chain;
    }
    return nullptr;
}

void AnimatorEvaluator::applyIK(AnimatorComponent& animator, const glm::mat4& spaceWorld) {
    animator.animatedGlobals = animator.globalTransforms;
    if (!animator.skeleton || (animator.ikGoals.empty() && animator.hipsOffset == glm::vec3(0.0f))) return;
    const Skeleton& skeleton = *animator.skeleton;
    const glm::mat4 toSkeleton = glm::inverse(spaceWorld);
    const glm::quat spaceRotation = rotationOf(spaceWorld);

    if (animator.hipsOffset != glm::vec3(0.0f)) {
        const int hips = animator.humanoid ? animator.humanoid->joint(HumanoidBone::Hips) : skeleton.rootJointIndex;
        if (hips >= 0 && hips < static_cast<int>(animator.pose.jointCount())) {
            const int parent = skeleton.joints[hips].parentIndex;
            glm::mat3 toWorld(spaceWorld);
            if (parent >= 0 && parent < static_cast<int>(animator.globalTransforms.size())) {
                toWorld = toWorld * glm::mat3(animator.globalTransforms[parent]);
            }
            animator.pose.translations[hips] += glm::inverse(toWorld) * animator.hipsOffset;
            PoseOps::globalTransforms(skeleton, animator.pose, animator.globalTransforms);
        }
    }

    for (const IKGoalState& goalState : animator.ikGoals) {
        const IKChain* chain = chainFor(animator, goalState.chain);
        if (!chain) continue;
        TwoBoneIKGoal goal;
        goal.upper = chain->upper;
        goal.middle = chain->middle;
        goal.end = chain->end;
        goal.weight = goalState.weight;
        goal.target = glm::vec3(toSkeleton * glm::vec4(goalState.position, 1.0f));
        if (goalState.hasPole) {
            goal.hasPole = true;
            goal.pole = glm::mat3(toSkeleton) * goalState.pole;
        } else if (chain->hasDefaultPole) {
            const int parent = skeleton.joints[chain->upper].parentIndex;
            const glm::quat parentNow = parent >= 0 ? rotationOf(animator.globalTransforms[parent])
                                                    : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            goal.hasPole = true;
            goal.pole = parentNow * chain->defaultPole;
        }
        if (goalState.rotation == IKGoalState::Rotation::World) {
            goal.hasEndRotation = true;
            goal.endRotation = glm::normalize(glm::inverse(spaceRotation) * goalState.worldRotation);
        } else if (goalState.rotation == IKGoalState::Rotation::Normal) {
            const glm::quat animatedWorld = spaceRotation * rotationOf(animator.animatedGlobals[chain->end]);
            const glm::quat tilted = tiltBetween(glm::vec3(0.0f, 1.0f, 0.0f), goalState.normal) * animatedWorld;
            goal.hasEndRotation = true;
            goal.endRotation = glm::normalize(glm::inverse(spaceRotation) * tilted);
        }
        solveTwoBoneIK(skeleton, animator.pose, animator.globalTransforms, goal);
    }
    PoseOps::boneMatrices(skeleton, animator.globalTransforms, animator.boneMatrices);
}

void AnimatorEvaluator::setRootMotion(AnimatorComponent& animator, bool enabled, int joint) {
    animator.rootMotion = enabled;
    animator.rootMotionJoint = joint >= 0 ? joint : (animator.skeleton ? animator.skeleton->rootJointIndex : -1);
    animator.rootMotionDelta = glm::vec3(0.0f);
    animator.rootMotionWorld = glm::vec3(0.0f);
}

void AnimatorEvaluator::advance(AnimatorComponent& animator, float deltaTime) {
    if (animator.controller) {
        ControllerEvaluator::advance(animator, deltaTime);
        return;
    }
    const float scaled = deltaTime * animator.speed;
    step(animator.current, animator.clipAt(animator.current.clip), scaled);
    if (animator.previous.clip >= 0) {
        step(animator.previous, animator.clipAt(animator.previous.clip), scaled);
        animator.fadeElapsed += std::abs(scaled);
        if (animator.fadeElapsed >= animator.fadeDuration) {
            animator.previous = AnimatorClipState{};
            animator.fadeElapsed = 0.0f;
            animator.fadeDuration = 0.0f;
        }
    }
}

void AnimatorEvaluator::evaluate(AnimatorComponent& animator) {
    if (!animator.skeleton) return;
    const Skeleton& skeleton = *animator.skeleton;

    if (animator.controller) {
        ControllerEvaluator::evaluate(animator);
    } else {
        animator.rootMotionDelta = glm::vec3(0.0f);
        const bool motion = animator.rootMotion;
        if (const AnimationClip* clip = animator.clipAt(animator.current.clip)) {
            sampleClip(animator, *clip, animator.current.time, animator.pose);
            if (motion) animator.rootMotionDelta = stateDisplacement(animator, animator.current);
        } else {
            animator.pose.setRest(skeleton);
        }

        if (animator.isFading()) {
            if (const AnimationClip* from = animator.clipAt(animator.previous.clip)) {
                sampleClip(animator, *from, animator.previous.time, animator.fadePose);
                const float weight = animator.fadeDuration > 0.0f
                                         ? animator.fadeElapsed / animator.fadeDuration : 1.0f;
                PoseOps::blend(animator.fadePose, animator.pose, weight, animator.pose);
                if (motion) {
                    animator.rootMotionDelta = weight * animator.rootMotionDelta
                                             + (1.0f - weight) * stateDisplacement(animator, animator.previous);
                }
            }
        }
    }

    PoseOps::globalTransforms(skeleton, animator.pose, animator.globalTransforms);
    PoseOps::boneMatrices(skeleton, animator.globalTransforms, animator.boneMatrices);
}

void AnimatorEvaluator::update(entt::registry& registry, float deltaTime) {
    auto animators = registry.view<AnimatorComponent>();
    for (auto entity : animators) {
        auto& animator = animators.get<AnimatorComponent>(entity);
        if (!animator.skeleton) continue;

        // The root motion joint's parent space to world, linear part.
        glm::mat3 toWorld(1.0f);
        if (animator.rootMotion) {
            const ECS::TransformComponent* space = nullptr;
            for (entt::entity mesh : animator.skinnedMeshes) {
                if (registry.valid(mesh) && (space = registry.try_get<ECS::TransformComponent>(mesh))) break;
            }
            if (!space) space = registry.try_get<ECS::TransformComponent>(entity);
            if (space) toWorld = glm::mat3(space->worldMatrix);
            const int joint = animator.rootMotionJoint;
            if (joint >= 0 && joint < static_cast<int>(animator.skeleton->jointCount())) {
                const int parent = animator.skeleton->joints[joint].parentIndex;
                if (parent >= 0 && parent < static_cast<int>(animator.globalTransforms.size())) {
                    toWorld = toWorld * glm::mat3(animator.globalTransforms[parent]);
                }
            }
            const glm::vec3 up = glm::inverse(toWorld) * glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::dot(up, up) > 1e-20f) animator.rootMotionUp = glm::normalize(up);
        }

        advance(animator, deltaTime);
        evaluate(animator);
        if (animator.rootMotion) animator.rootMotionWorld += toWorld * animator.rootMotionDelta;

        const ECS::TransformComponent* drawn = nullptr;
        for (entt::entity mesh : animator.skinnedMeshes) {
            if (registry.valid(mesh) && (drawn = registry.try_get<ECS::TransformComponent>(mesh))) break;
        }
        if (!drawn) drawn = registry.try_get<ECS::TransformComponent>(entity);
        applyIK(animator, drawn ? drawn->worldMatrix : glm::mat4(1.0f));
        for (entt::entity mesh : animator.skinnedMeshes) {
            if (!registry.valid(mesh)) continue;
            auto* skeleton = registry.try_get<SkeletonComponent>(mesh);
            if (!skeleton || skeleton->skeleton != animator.skeleton) continue;
            skeleton->boneMatrices = animator.boneMatrices;
            skeleton->dirty = true;
        }
    }
    placeAttachments(registry);
}

void AnimatorEvaluator::placeAttachments(entt::registry& registry) {
    auto attachments = registry.view<JointAttachmentComponent, ECS::TransformComponent>();
    for (auto entity : attachments) {
        const auto& attachment = attachments.get<JointAttachmentComponent>(entity);
        if (!registry.valid(attachment.animator)) continue;
        const auto* animator = registry.try_get<AnimatorComponent>(attachment.animator);
        if (!animator || attachment.joint < 0 ||
            attachment.joint >= static_cast<int>(animator->globalTransforms.size())) {
            continue;
        }

        const ECS::TransformComponent* space = nullptr;
        for (entt::entity mesh : animator->skinnedMeshes) {
            if (registry.valid(mesh) && (space = registry.try_get<ECS::TransformComponent>(mesh))) break;
        }
        if (!space) space = registry.try_get<ECS::TransformComponent>(attachment.animator);
        const glm::mat4 spaceWorld = space ? space->worldMatrix : glm::mat4(1.0f);

        auto& transform = attachments.get<ECS::TransformComponent>(entity);
        transform.worldMatrix = spaceWorld * animator->globalTransforms[attachment.joint] * attachment.offset;
        updateDescendants(registry, entity, transform.worldMatrix);
    }
}

AnimatorComponent* AnimatorEvaluator::createAnimator(entt::registry& registry, entt::entity root) {
    if (!registry.valid(root)) return nullptr;
    if (auto* existing = registry.try_get<AnimatorComponent>(root)) return existing;

    std::vector<entt::entity> skinned;
    std::function<void(entt::entity)> collect = [&](entt::entity entity) {
        if (registry.all_of<SkeletonComponent>(entity) && registry.get<SkeletonComponent>(entity).skeleton) {
            skinned.push_back(entity);
        }
        if (const auto* hierarchy = registry.try_get<ECS::HierarchyComponent>(entity)) {
            for (entt::entity child : hierarchy->children) {
                if (registry.valid(child)) collect(child);
            }
        }
    };
    collect(root);
    if (skinned.empty()) return nullptr;

    AnimatorComponent animator;
    animator.skeleton = registry.get<SkeletonComponent>(skinned.front()).skeleton;
    for (entt::entity mesh : skinned) {
        auto& skeleton = registry.get<SkeletonComponent>(mesh);
        if (skeleton.skeleton != animator.skeleton) continue;
        animator.skinnedMeshes.push_back(mesh);
    }

    if (const auto* playback = registry.try_get<AnimationPlaybackComponent>(skinned.front())) {
        animator.clips = playback->clips;
        animator.speed = playback->speed;
        if (playback->getCurrentClip()) {
            animator.current.clip = playback->currentClipIndex;
            animator.current.time = playback->currentTime;
            animator.current.loop = playback->loop;
            animator.current.playing = playback->playing;
        }
    }

    for (entt::entity mesh : animator.skinnedMeshes) {
        auto& skeleton = registry.get<SkeletonComponent>(mesh);
        skeleton.animator = root;
        skeleton.dirty = true;
    }

    auto& component = registry.emplace<AnimatorComponent>(root, std::move(animator));
    evaluate(component);
    return &component;
}

std::vector<std::string> AnimatorEvaluator::addClips(AnimatorComponent& animator,
                                                     const std::vector<std::shared_ptr<AnimationClip>>& clips,
                                                     const std::string& name) {
    std::vector<std::string> added;
    if (!animator.skeleton) return added;

    std::vector<std::shared_ptr<AnimationClip>> bound;
    for (const auto& clip : clips) {
        if (!clip) continue;
        auto copy = bindClip(*clip, *animator.skeleton);
        if (!copy->channels.empty()) bound.push_back(std::move(copy));
    }
    for (auto& clip : bound) {
        if (!name.empty()) clip->name = bound.size() == 1 ? name : name + ":" + clip->name;
        added.push_back(clip->name);
        animator.clips.push_back(std::move(clip));
    }
    return added;
}

} // namespace Shoonyakasha
