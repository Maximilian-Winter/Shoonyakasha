//
// AnimatorEvaluator.cpp - Advance and evaluate AnimatorComponents
//

#include "Animation/AnimatorEvaluator.h"
#include "Animation/ClipBinding.h"
#include "ECS/Core.h"
#include "ECS/SkeletonComponents.h"

#include <cmath>
#include <functional>

namespace Shoonyakasha {

namespace {

void step(AnimatorClipState& state, const AnimationClip* clip, float deltaTime) {
    if (!state.playing || !clip || clip->duration <= 0.0f) return;
    state.time += deltaTime * state.speed;
    if (state.loop) {
        state.time = std::fmod(state.time, clip->duration);
        if (state.time < 0.0f) state.time += clip->duration;
    } else if (state.time >= clip->duration) {
        state.time = clip->duration;
        state.playing = false;
    } else if (state.time < 0.0f) {
        state.time = 0.0f;
        state.playing = false;
    }
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

} // namespace

void AnimatorEvaluator::advance(AnimatorComponent& animator, float deltaTime) {
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

    if (const AnimationClip* clip = animator.clipAt(animator.current.clip)) {
        PoseOps::sample(*clip, animator.current.time, skeleton, animator.pose);
    } else {
        animator.pose.setRest(skeleton);
    }

    if (animator.isFading()) {
        if (const AnimationClip* from = animator.clipAt(animator.previous.clip)) {
            PoseOps::sample(*from, animator.previous.time, skeleton, animator.fadePose);
            const float weight = animator.fadeDuration > 0.0f
                                     ? animator.fadeElapsed / animator.fadeDuration : 1.0f;
            PoseOps::blend(animator.fadePose, animator.pose, weight, animator.pose);
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
        advance(animator, deltaTime);
        evaluate(animator);
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
