//
// Facade/AnimationAPI.cpp - Delegates to AnimatorComponent / AnimatorEvaluator
//

#include "AnimationAPIImpl.h"
#include "FacadeInternal.h"
#include "Animation/AnimationController.h"
#include "Animation/AnimatorEvaluator.h"
#include "Animation/ControllerEvaluator.h"
#include "Animation/Humanoid.h"
#include "Animation/Retarget.h"
#include "ECS/Core.h"
#include "Resources/AnimationClipLoader.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

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
    return component && !component->controller && component->play(component->findClip(clip), fadeSeconds, loop);
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

// ═══════════════════════════════════════════════════════════════
// Controller
// ═══════════════════════════════════════════════════════════════

namespace {

AnimatorComponent& requireAnimator(entt::registry* registry, EntityHandle handle) {
    AnimatorComponent* component = animatorOf(registry, handle);
    if (!component) throw std::runtime_error("entity has no animator");
    return *component;
}

ControllerRuntime& requireController(entt::registry* registry, EntityHandle handle) {
    AnimatorComponent& component = requireAnimator(registry, handle);
    if (!component.controller) throw std::runtime_error("the animator has no controller");
    return *component.controller;
}

int requireParameter(const ControllerRuntime& runtime, const std::string& name) {
    const int index = runtime.definition->findParameter(name);
    if (index < 0) throw std::runtime_error("no parameter '" + name + "'");
    return index;
}

int requireTyped(const ControllerRuntime& runtime, const std::string& name, ControllerParameterType type,
                 const char* typeName) {
    const int index = requireParameter(runtime, name);
    if (runtime.definition->parameters[index].type != type) {
        throw std::runtime_error("parameter '" + name + "' is not a " + typeName);
    }
    return index;
}

ControllerLayerRuntime& requireLayer(ControllerRuntime& runtime, const std::string& name) {
    const int index = runtime.definition->findLayer(name);
    if (index < 0) throw std::runtime_error("no layer '" + name + "'");
    return runtime.layers[index];
}

} // namespace

void AnimationAPI::setController(EntityHandle animator, const std::string& json) {
    AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    component.controller = ControllerEvaluator::bind(parseAnimationController(json), component);
}

void AnimationAPI::clearController(EntityHandle animator) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) component->controller.reset();
}

bool AnimationAPI::hasController(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->controller;
}

void AnimationAPI::setFloat(EntityHandle animator, const std::string& parameter, float value) {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    runtime.parameters[requireTyped(runtime, parameter, ControllerParameterType::Float, "float")] = value;
}

void AnimationAPI::setInt(EntityHandle animator, const std::string& parameter, int value) {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    runtime.parameters[requireTyped(runtime, parameter, ControllerParameterType::Int, "int")] =
        static_cast<float>(value);
}

void AnimationAPI::setBool(EntityHandle animator, const std::string& parameter, bool value) {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    runtime.parameters[requireTyped(runtime, parameter, ControllerParameterType::Bool, "bool")] = value ? 1.0f : 0.0f;
}

void AnimationAPI::setTrigger(EntityHandle animator, const std::string& parameter) {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    runtime.parameters[requireTyped(runtime, parameter, ControllerParameterType::Trigger, "trigger")] = 1.0f;
}

void AnimationAPI::resetTrigger(EntityHandle animator, const std::string& parameter) {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    runtime.parameters[requireTyped(runtime, parameter, ControllerParameterType::Trigger, "trigger")] = 0.0f;
}

float AnimationAPI::getParameter(EntityHandle animator, const std::string& parameter) const {
    const ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    return runtime.parameters[requireParameter(runtime, parameter)];
}

std::string AnimationAPI::getCurrentState(EntityHandle animator, const std::string& layer) const {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    const ControllerLayerRuntime& lr = requireLayer(runtime, layer);
    const ControllerLayer& definition = runtime.definition->layers[runtime.definition->findLayer(layer)];
    return lr.current.state >= 0 ? definition.states[lr.current.state].name : "";
}

bool AnimationAPI::isInTransition(EntityHandle animator, const std::string& layer) const {
    ControllerRuntime& runtime = requireController(m_impl->registry, animator);
    return requireLayer(runtime, layer).isFading();
}

void AnimationAPI::setLayerWeight(EntityHandle animator, const std::string& layer, float weight) {
    requireLayer(requireController(m_impl->registry, animator), layer).weight = weight;
}

float AnimationAPI::getLayerWeight(EntityHandle animator, const std::string& layer) const {
    return requireLayer(requireController(m_impl->registry, animator), layer).weight;
}

std::vector<std::string> AnimationAPI::takeEvents(EntityHandle animator) {
    AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component || !component->controller) return {};
    return std::exchange(component->controller->events, {});
}

// ═══════════════════════════════════════════════════════════════
// Root motion
// ═══════════════════════════════════════════════════════════════

void AnimationAPI::setRootMotion(EntityHandle animator, bool enabled, const std::string& joint) {
    AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    int index = -1;
    if (!joint.empty()) {
        index = component.skeleton ? component.skeleton->findJoint(joint) : -1;
        if (index < 0) throw std::runtime_error("unknown joint: " + joint);
    }
    AnimatorEvaluator::setRootMotion(component, enabled, index);
}

bool AnimationAPI::isRootMotion(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->rootMotion;
}

glm::vec3 AnimationAPI::takeRootMotion(EntityHandle animator) {
    AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component) return glm::vec3(0.0f);
    return std::exchange(component->rootMotionWorld, glm::vec3(0.0f));
}

// ═══════════════════════════════════════════════════════════════
// Humanoid
// ═══════════════════════════════════════════════════════════════

AnimationAPI::BoneMapping AnimationAPI::makeHumanoid(EntityHandle animator, const BoneMapping& mapping) {
    AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    component.humanoid = makeHumanoidAvatar(component.skeleton, mapping);
    return humanoidMapping(*component.humanoid);
}

bool AnimationAPI::isHumanoid(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component && component->humanoid;
}

AnimationAPI::BoneMapping AnimationAPI::getHumanoidMapping(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component || !component->humanoid) return {};
    return humanoidMapping(*component->humanoid);
}

std::vector<std::string> AnimationAPI::retargetClips(EntityHandle target, EntityHandle source,
                                                     const std::vector<std::string>& clips,
                                                     const std::string& name) {
    AnimatorComponent& to = requireAnimator(m_impl->registry, target);
    AnimatorComponent& from = requireAnimator(m_impl->registry, source);
    if (!to.humanoid) throw std::runtime_error("the target animator is not a humanoid");
    if (!from.humanoid) throw std::runtime_error("the source animator is not a humanoid");

    std::vector<std::shared_ptr<AnimationClip>> selected;
    if (clips.empty()) {
        selected = from.clips;
    } else {
        for (const std::string& clip : clips) {
            const int index = from.findClip(clip);
            if (index < 0) throw std::runtime_error("no clip '" + clip + "'");
            selected.push_back(from.clips[index]);
        }
    }
    std::vector<std::shared_ptr<AnimationClip>> retargeted;
    for (const auto& clip : selected) {
        if (clip) retargeted.push_back(retargetClip(*clip, *from.humanoid, *to.humanoid));
    }
    return AnimatorEvaluator::addClips(to, retargeted, name);
}

std::vector<std::string> AnimationAPI::loadHumanoidClips(EntityHandle target, const std::string& path,
                                                         const std::string& name, const BoneMapping& mapping) {
    AnimatorComponent& to = requireAnimator(m_impl->registry, target);
    if (!to.humanoid) throw std::runtime_error("the target animator is not a humanoid");
    AnimationFile file = loadAnimationFile(path);
    auto source = makeHumanoidAvatar(file.skeleton, mapping);
    std::vector<std::shared_ptr<AnimationClip>> retargeted;
    for (const auto& clip : file.clips) retargeted.push_back(retargetClip(*clip, *source, *to.humanoid));
    return AnimatorEvaluator::addClips(to, retargeted, name);
}

// ═══════════════════════════════════════════════════════════════
// IK
// ═══════════════════════════════════════════════════════════════

namespace {

int requireJoint(const AnimatorComponent& component, const std::string& name) {
    const int joint = component.skeleton ? component.skeleton->findJoint(name) : -1;
    if (joint < 0) throw std::runtime_error("unknown joint: " + name);
    return joint;
}

glm::mat4 drawnWorld(entt::registry& registry, const AnimatorComponent& component, entt::entity animator) {
    for (entt::entity mesh : component.skinnedMeshes) {
        if (!registry.valid(mesh)) continue;
        if (const auto* transform = registry.try_get<ECS::TransformComponent>(mesh)) return transform->worldMatrix;
    }
    if (const auto* transform = registry.try_get<ECS::TransformComponent>(animator)) return transform->worldMatrix;
    return glm::mat4(1.0f);
}

} // namespace

void AnimationAPI::setIKChain(EntityHandle animator, const std::string& name, const std::string& upper,
                              const std::string& middle, const std::string& end) {
    AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    if (!AnimatorEvaluator::setChain(component, name, requireJoint(component, upper), requireJoint(component, middle),
                                     requireJoint(component, end))) {
        throw std::runtime_error("joints " + upper + ", " + middle + ", " + end + " are not a chain");
    }
}

void AnimationAPI::setIKGoal(EntityHandle animator, const std::string& chain, const glm::vec3& position, float weight,
                             bool hasPole, const glm::vec3& pole, bool hasRotation, const glm::vec3& rotation,
                             bool hasNormal, const glm::vec3& normal) {
    AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    if (!AnimatorEvaluator::chainFor(component, chain)) throw std::runtime_error("no IK chain '" + chain + "'");
    IKGoalState goal;
    goal.chain = chain;
    goal.position = position;
    goal.weight = weight;
    goal.hasPole = hasPole;
    goal.pole = pole;
    if (hasRotation) {
        goal.rotation = IKGoalState::Rotation::World;
        const glm::mat3 m(ECS::TransformComponent(glm::vec3(0.0f), rotation, glm::vec3(1.0f)).getLocalMatrix());
        goal.worldRotation = glm::normalize(glm::quat_cast(m));
    } else if (hasNormal && glm::dot(normal, normal) > 0.0f) {
        goal.rotation = IKGoalState::Rotation::Normal;
        goal.normal = glm::normalize(normal);
    }
    for (IKGoalState& existing : component.ikGoals) {
        if (existing.chain == chain) {
            existing = goal;
            return;
        }
    }
    component.ikGoals.push_back(goal);
}

void AnimationAPI::clearIKGoal(EntityHandle animator, const std::string& chain) {
    AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    if (!component) return;
    auto& goals = component->ikGoals;
    goals.erase(std::remove_if(goals.begin(), goals.end(), [&](const IKGoalState& g) { return g.chain == chain; }),
                goals.end());
}

void AnimationAPI::clearIKGoals(EntityHandle animator) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) component->ikGoals.clear();
}

void AnimationAPI::setHipsOffset(EntityHandle animator, const glm::vec3& offset) {
    if (AnimatorComponent* component = animatorOf(m_impl->registry, animator)) component->hipsOffset = offset;
}

glm::vec3 AnimationAPI::getHipsOffset(EntityHandle animator) const {
    const AnimatorComponent* component = animatorOf(m_impl->registry, animator);
    return component ? component->hipsOffset : glm::vec3(0.0f);
}

glm::vec3 AnimationAPI::getAnimatedJointWorldPosition(EntityHandle animator, const std::string& joint) const {
    const AnimatorComponent& component = requireAnimator(m_impl->registry, animator);
    const int index = requireJoint(component, joint);
    const auto& globals = component.animatedGlobals.empty() ? component.globalTransforms : component.animatedGlobals;
    if (index >= static_cast<int>(globals.size())) throw std::runtime_error("unknown joint: " + joint);
    const glm::mat4 world = drawnWorld(*m_impl->registry, component, toEntt(animator)) * globals[index];
    return glm::vec3(world[3]);
}

} // namespace Facade
} // namespace Shoonyakasha
