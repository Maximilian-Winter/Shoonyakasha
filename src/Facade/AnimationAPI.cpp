//
// Facade/AnimationAPI.cpp - Delegates to AnimatorComponent / AnimatorEvaluator
//

#include "AnimationAPIImpl.h"
#include "FacadeInternal.h"
#include "Animation/AnimatorEvaluator.h"
#include "ECS/Core.h"
#include "Resources/AnimationClipLoader.h"

#include <stdexcept>

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

namespace {

AnimatorComponent* animatorOf(entt::registry* registry, EntityHandle handle) {
    if (!registry) return nullptr;
    const entt::entity entity = toEntt(handle);
    if (!registry->valid(entity)) return nullptr;
    return registry->try_get<AnimatorComponent>(entity);
}

} // namespace

AnimationAPI::AnimationAPI()
    : m_impl(std::make_unique<Impl>())
{}

AnimationAPI::~AnimationAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Animators
// ═══════════════════════════════════════════════════════════════

bool AnimationAPI::createAnimator(EntityHandle root) {
    if (!m_impl->registry) return false;
    return AnimatorEvaluator::createAnimator(*m_impl->registry, toEntt(root)) != nullptr;
}

bool AnimationAPI::hasAnimator(EntityHandle entity) const {
    return animatorOf(m_impl->registry, entity) != nullptr;
}

// ═══════════════════════════════════════════════════════════════
// Clips
// ═══════════════════════════════════════════════════════════════

std::vector<std::string> AnimationAPI::loadClips(EntityHandle animator, const std::string& path,
                                                 const std::string& name) {
    AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component) throw std::runtime_error("entity has no animator");
    return AnimatorEvaluator::addClips(*component, loadAnimationClips(path), name);
}

std::vector<std::string> AnimationAPI::getClipNames(EntityHandle animator) const {
    std::vector<std::string> names;
    if (const AnimatorComponent* component = animatorOf(m_impl->registry, animator)) {
        for (const auto& clip : component->clips) names.push_back(clip ? clip->name : "");
    }
    return names;
}

float AnimationAPI::getClipDuration(EntityHandle animator, const std::string& clip) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component) return 0.0f;
    const AnimationClip* found = component->clipAt(component->findClip(clip));
    return found ? found->duration : 0.0f;
}

// ═══════════════════════════════════════════════════════════════
// Playback
// ═══════════════════════════════════════════════════════════════

bool AnimationAPI::play(EntityHandle animator, const std::string& clip, float fadeSeconds, bool loop) {
    AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->play(component->findClip(clip), fadeSeconds, loop);
}

void AnimationAPI::stop(EntityHandle animator) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) component->stop();
}

bool AnimationAPI::isPlaying(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->current.playing;
}

std::string AnimationAPI::getCurrentClip(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component) return "";
    const AnimationClip* clip = component->clipAt(component->current.clip);
    return clip ? clip->name : "";
}

bool AnimationAPI::isFading(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->isFading();
}

float AnimationAPI::getTime(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component ? component->current.time : 0.0f;
}

void AnimationAPI::setTime(EntityHandle animator, float seconds) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) {
        component->current.time = seconds;
    }
}

float AnimationAPI::getSpeed(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component ? component->speed : 1.0f;
}

void AnimationAPI::setSpeed(EntityHandle animator, float speed) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) component->speed = speed;
}

// ═══════════════════════════════════════════════════════════════
// Joints
// ═══════════════════════════════════════════════════════════════

std::vector<std::string> AnimationAPI::getJointNames(EntityHandle animator) const {
    std::vector<std::string> names;
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (component && component->skeleton) {
        for (const Joint& joint : component->skeleton->joints) names.push_back(joint.name);
    }
    return names;
}

glm::vec3 AnimationAPI::getJointWorldPosition(EntityHandle animator, const std::string& joint) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component || !component->skeleton) throw std::runtime_error("entity has no animator");
    const int index = component->skeleton->findJoint(joint);
    if (index < 0 || index >= static_cast<int>(component->globalTransforms.size())) {
        throw std::runtime_error("unknown joint: " + joint);
    }

    entt::registry& registry = *m_impl->registry;
    const ECS::TransformComponent* space = nullptr;
    for (entt::entity mesh : component->skinnedMeshes) {
        if (registry.valid(mesh) && (space = registry.try_get<ECS::TransformComponent>(mesh))) break;
    }
    if (!space) space = registry.try_get<ECS::TransformComponent>(toEntt(animator));
    const glm::mat4 world = (space ? space->worldMatrix : glm::mat4(1.0f)) * component->globalTransforms[index];
    return glm::vec3(world[3]);
}

bool AnimationAPI::attach(EntityHandle entity, EntityHandle animator, const std::string& joint,
                          const glm::vec3& offsetPosition, const glm::vec3& offsetRotation) {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component || !component->skeleton) return false;
    entt::registry& registry = *m_impl->registry;
    const entt::entity target = toEntt(entity);
    if (!registry.valid(target) || !registry.all_of<ECS::TransformComponent>(target)) return false;
    const int index = component->skeleton->findJoint(joint);
    if (index < 0) return false;

    JointAttachmentComponent attachment;
    attachment.animator = toEntt(animator);
    attachment.joint = index;
    attachment.offset = ECS::TransformComponent(offsetPosition, offsetRotation, glm::vec3(1.0f)).getLocalMatrix();
    registry.emplace_or_replace<JointAttachmentComponent>(target, attachment);
    return true;
}

void AnimationAPI::detach(EntityHandle entity) {
    if (!m_impl->registry) return;
    const entt::entity target = toEntt(entity);
    if (m_impl->registry->valid(target)) m_impl->registry->remove<JointAttachmentComponent>(target);
}

} // namespace Facade
} // namespace Shoonyakasha
