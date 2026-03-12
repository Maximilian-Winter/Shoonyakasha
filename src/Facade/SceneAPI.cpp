//
// Facade/SceneAPI.cpp - Implementation delegating to ECS::Scene + entt::registry
//

// entt + Core MUST come before SceneAPI.h so the SHOONYAKASHA_TESTING
// constructor declaration sees entt::registry as a complete type.
#include <entt/entt.hpp>
#include "ECS/Core.h"

#include "Facade/SceneAPI.h"
#include "FacadeInternal.h"

#include "ECS/Scene.h"
#include "ECS/RenderComponents.h"
#include "ECS/SkeletonComponents.h"

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// PIMPL Implementation
// ═══════════════════════════════════════════════════════════════

struct SceneAPI::Impl {
    ECS::Scene* scene;       // nullptr in test mode
    entt::registry& registry;
    ECS::ComponentRegistry& componentRegistry;

    // Production mode: wraps a full Scene
    explicit Impl(ECS::Scene& s)
        : scene(&s)
        , registry(s.getRegistry())
        , componentRegistry(s.getComponentRegistry())
    {}

    // Test mode: raw registry, no Scene (no GPU needed)
    Impl(entt::registry& r, ECS::ComponentRegistry& cr)
        : scene(nullptr)
        , registry(r)
        , componentRegistry(cr)
    {}

    bool valid(EntityHandle h) const {
        return registry.valid(toEntt(h));
    }

    bool hasScene() const { return scene != nullptr; }
};

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

SceneAPI::SceneAPI(ECS::Scene& scene)
    : m_impl(std::make_unique<Impl>(scene))
{}

#ifdef SHOONYAKASHA_TESTING
SceneAPI::SceneAPI(entt::registry& registry, ECS::ComponentRegistry& componentRegistry)
    : m_impl(std::make_unique<Impl>(registry, componentRegistry))
{}
#endif

SceneAPI::~SceneAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Entity Lifecycle
// ═══════════════════════════════════════════════════════════════

EntityHandle SceneAPI::createEntity(const std::string& name) {
    if (m_impl->hasScene()) {
        auto builder = m_impl->scene->createEntity(name);
        return toHandle(builder.build());
    }
    // Test mode: manual entity creation with Transform + Active + optional Name
    auto e = m_impl->registry.create();
    m_impl->registry.emplace<ECS::TransformComponent>(e);
    m_impl->registry.emplace<ECS::ActiveComponent>(e);
    if (!name.empty()) {
        m_impl->registry.emplace<ECS::NameComponent>(e, name);
    }
    return toHandle(e);
}

void SceneAPI::destroyEntity(EntityHandle entity) {
    if (!m_impl->valid(entity)) return;
    if (m_impl->hasScene()) {
        m_impl->scene->destroyEntity(toEntt(entity));
    } else {
        m_impl->registry.destroy(toEntt(entity));
    }
}

bool SceneAPI::isValid(EntityHandle entity) const {
    return m_impl->valid(entity);
}

size_t SceneAPI::getEntityCount() const {
    if (m_impl->hasScene()) return m_impl->scene->getEntityCount();
    return m_impl->registry.view<entt::entity>().size();
}

// ═══════════════════════════════════════════════════════════════
// Entity Queries
// ═══════════════════════════════════════════════════════════════

EntityHandle SceneAPI::findEntityByName(const std::string& name) const {
    if (m_impl->hasScene()) {
        return toHandle(m_impl->scene->findEntityByName(name));
    }
    // Test mode: manual search
    auto view = m_impl->registry.view<ECS::NameComponent>();
    for (auto entity : view) {
        if (view.get<ECS::NameComponent>(entity).name == name)
            return toHandle(entity);
    }
    return NullEntity;
}

std::vector<EntityHandle> SceneAPI::findEntitiesWithTag(const std::string& tag) const {
    std::vector<entt::entity> entities;
    if (m_impl->hasScene()) {
        entities = m_impl->scene->findEntitiesWithTag(tag);
    } else {
        entities = ECS::EntityHelper::findEntitiesWithTag(m_impl->registry, tag);
    }
    std::vector<EntityHandle> result;
    result.reserve(entities.size());
    for (auto e : entities) {
        result.push_back(toHandle(e));
    }
    return result;
}

EntityHandle SceneAPI::getMainCamera() {
    if (m_impl->hasScene()) {
        return toHandle(m_impl->scene->getMainCamera());
    }
    // Test mode: find first entity with CameraComponent + isMainCamera
    auto view = m_impl->registry.view<ECS::CameraComponent>();
    for (auto entity : view) {
        if (view.get<ECS::CameraComponent>(entity).isMainCamera)
            return toHandle(entity);
    }
    return NullEntity;
}

std::vector<EntityHandle> SceneAPI::getAllEntities() const {
    std::vector<EntityHandle> result;
    auto view = m_impl->registry.view<entt::entity>();
    for (auto entity : view) {
        result.push_back(toHandle(entity));
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Component Management (string-based)
// ═══════════════════════════════════════════════════════════════

bool SceneAPI::addComponent(EntityHandle entity, const std::string& componentName) {
    if (!m_impl->valid(entity)) return false;
    return m_impl->componentRegistry.createComponent(componentName, m_impl->registry, toEntt(entity));
}

bool SceneAPI::removeComponent(EntityHandle entity, const std::string& componentName) {
    if (!m_impl->valid(entity)) return false;
    return m_impl->componentRegistry.removeComponent(componentName, m_impl->registry, toEntt(entity));
}

bool SceneAPI::hasComponent(EntityHandle entity, const std::string& componentName) const {
    if (!m_impl->valid(entity)) return false;
    return m_impl->componentRegistry.hasComponent(componentName, m_impl->registry, toEntt(entity));
}

std::vector<std::string> SceneAPI::getComponentNames() const {
    return m_impl->componentRegistry.getAllComponentNames();
}

// ═══════════════════════════════════════════════════════════════
// Name / Tag / Active
// ═══════════════════════════════════════════════════════════════

std::string SceneAPI::getName(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return "";
    auto* comp = m_impl->registry.try_get<ECS::NameComponent>(toEntt(entity));
    return comp ? comp->name : "";
}

void SceneAPI::setName(EntityHandle entity, const std::string& name) {
    if (!m_impl->valid(entity)) return;
    auto e = toEntt(entity);
    auto* comp = m_impl->registry.try_get<ECS::NameComponent>(e);
    if (comp) {
        comp->name = name;
    } else {
        m_impl->registry.emplace<ECS::NameComponent>(e, name);
    }
}

std::string SceneAPI::getTag(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return "";
    auto* comp = m_impl->registry.try_get<ECS::TagComponent>(toEntt(entity));
    return comp ? comp->tag : "";
}

void SceneAPI::setTag(EntityHandle entity, const std::string& tag) {
    if (!m_impl->valid(entity)) return;
    auto e = toEntt(entity);
    auto* comp = m_impl->registry.try_get<ECS::TagComponent>(e);
    if (comp) {
        comp->tag = tag;
    } else {
        m_impl->registry.emplace<ECS::TagComponent>(e, tag);
    }
}

bool SceneAPI::isActive(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* comp = m_impl->registry.try_get<ECS::ActiveComponent>(toEntt(entity));
    return comp ? comp->active : false;
}

void SceneAPI::setActive(EntityHandle entity, bool active) {
    if (!m_impl->valid(entity)) return;
    auto e = toEntt(entity);
    auto* comp = m_impl->registry.try_get<ECS::ActiveComponent>(e);
    if (comp) {
        comp->active = active;
    } else {
        auto& ac = m_impl->registry.emplace<ECS::ActiveComponent>(e);
        ac.active = active;
    }
}

// ═══════════════════════════════════════════════════════════════
// Transform Access
// ═══════════════════════════════════════════════════════════════

glm::vec3 SceneAPI::getPosition(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(0.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->position : glm::vec3(0.0f);
}

void SceneAPI::setPosition(EntityHandle entity, const glm::vec3& pos) {
    if (!m_impl->valid(entity)) return;
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    if (t) { t->position = pos; t->isDirty = true; }
}

glm::vec3 SceneAPI::getRotation(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(0.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->rotation : glm::vec3(0.0f);
}

void SceneAPI::setRotation(EntityHandle entity, const glm::vec3& eulerRadians) {
    if (!m_impl->valid(entity)) return;
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    if (t) { t->rotation = eulerRadians; t->isDirty = true; }
}

glm::vec3 SceneAPI::getScale(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(1.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->scale : glm::vec3(1.0f);
}

void SceneAPI::setScale(EntityHandle entity, const glm::vec3& scale) {
    if (!m_impl->valid(entity)) return;
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    if (t) { t->scale = scale; t->isDirty = true; }
}

glm::vec3 SceneAPI::getWorldPosition(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(0.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    if (!t) return glm::vec3(0.0f);
    return glm::vec3(t->worldMatrix[3]);
}

glm::mat4 SceneAPI::getWorldMatrix(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::mat4(1.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->worldMatrix : glm::mat4(1.0f);
}

glm::vec3 SceneAPI::getForward(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(0.0f, 0.0f, -1.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->getForward() : glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::vec3 SceneAPI::getRight(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(1.0f, 0.0f, 0.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->getRight() : glm::vec3(1.0f, 0.0f, 0.0f);
}

glm::vec3 SceneAPI::getUp(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(0.0f, 1.0f, 0.0f);
    auto* t = m_impl->registry.try_get<ECS::TransformComponent>(toEntt(entity));
    return t ? t->getUp() : glm::vec3(0.0f, 1.0f, 0.0f);
}

// ═══════════════════════════════════════════════════════════════
// Camera Access
// ═══════════════════════════════════════════════════════════════

CameraType SceneAPI::getCameraType(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return CameraType::Perspective;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (!c) return CameraType::Perspective;
    return (c->type == ECS::CameraComponent::Orthographic)
        ? CameraType::Orthographic : CameraType::Perspective;
}

void SceneAPI::setCameraType(EntityHandle entity, CameraType type) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) {
        c->type = (type == CameraType::Orthographic)
            ? ECS::CameraComponent::Orthographic : ECS::CameraComponent::Perspective;
    }
}

float SceneAPI::getCameraFov(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 45.0f;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    return c ? c->fov : 45.0f;
}

void SceneAPI::setCameraFov(EntityHandle entity, float fov) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) c->fov = fov;
}

float SceneAPI::getCameraNear(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 0.1f;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    return c ? c->nearPlane : 0.1f;
}

void SceneAPI::setCameraNear(EntityHandle entity, float nearPlane) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) c->nearPlane = nearPlane;
}

float SceneAPI::getCameraFar(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 1000.0f;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    return c ? c->farPlane : 1000.0f;
}

void SceneAPI::setCameraFar(EntityHandle entity, float farPlane) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) c->farPlane = farPlane;
}

float SceneAPI::getCameraOrthoSize(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 10.0f;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    return c ? c->orthoSize : 10.0f;
}

void SceneAPI::setCameraOrthoSize(EntityHandle entity, float size) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) c->orthoSize = size;
}

bool SceneAPI::isCameraMain(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    return c ? c->isMainCamera : false;
}

void SceneAPI::setCameraMain(EntityHandle entity, bool isMain) {
    if (!m_impl->valid(entity)) return;
    auto* c = m_impl->registry.try_get<ECS::CameraComponent>(toEntt(entity));
    if (c) c->isMainCamera = isMain;
}

// ═══════════════════════════════════════════════════════════════
// Light Access
// ═══════════════════════════════════════════════════════════════

LightType SceneAPI::getLightType(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return LightType::Point;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (!l) return LightType::Point;
    switch (l->type) {
        case ECS::LightComponent::Directional: return LightType::Directional;
        case ECS::LightComponent::Point:       return LightType::Point;
        case ECS::LightComponent::Spot:        return LightType::Spot;
        default: return LightType::Point;
    }
}

void SceneAPI::setLightType(EntityHandle entity, LightType type) {
    if (!m_impl->valid(entity)) return;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (!l) return;
    switch (type) {
        case LightType::Directional: l->type = ECS::LightComponent::Directional; break;
        case LightType::Point:       l->type = ECS::LightComponent::Point;       break;
        case LightType::Spot:        l->type = ECS::LightComponent::Spot;        break;
    }
}

glm::vec3 SceneAPI::getLightColor(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return glm::vec3(1.0f);
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    return l ? l->color : glm::vec3(1.0f);
}

void SceneAPI::setLightColor(EntityHandle entity, const glm::vec3& color) {
    if (!m_impl->valid(entity)) return;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (l) l->color = color;
}

float SceneAPI::getLightIntensity(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 1.0f;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    return l ? l->intensity : 1.0f;
}

void SceneAPI::setLightIntensity(EntityHandle entity, float intensity) {
    if (!m_impl->valid(entity)) return;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (l) l->intensity = intensity;
}

float SceneAPI::getLightRange(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 10.0f;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    return l ? l->range : 10.0f;
}

void SceneAPI::setLightRange(EntityHandle entity, float range) {
    if (!m_impl->valid(entity)) return;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (l) l->range = range;
}

bool SceneAPI::getLightCastShadows(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    return l ? l->castShadows : false;
}

void SceneAPI::setLightCastShadows(EntityHandle entity, bool castShadows) {
    if (!m_impl->valid(entity)) return;
    auto* l = m_impl->registry.try_get<ECS::LightComponent>(toEntt(entity));
    if (l) l->castShadows = castShadows;
}

// ═══════════════════════════════════════════════════════════════
// Material Access
// ═══════════════════════════════════════════════════════════════

void SceneAPI::setMaterialFloat(EntityHandle entity, const std::string& param, float value) {
    if (!m_impl->valid(entity)) return;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    if (mat) mat->setParam(param, value);
}

float SceneAPI::getMaterialFloat(EntityHandle entity, const std::string& param,
                                  float defaultVal) const {
    if (!m_impl->valid(entity)) return defaultVal;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    return mat ? mat->getParam<float>(param, defaultVal) : defaultVal;
}

void SceneAPI::setMaterialVec3(EntityHandle entity, const std::string& param,
                                const glm::vec3& value) {
    if (!m_impl->valid(entity)) return;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    if (mat) mat->setParam(param, value);
}

glm::vec3 SceneAPI::getMaterialVec3(EntityHandle entity, const std::string& param,
                                     const glm::vec3& defaultVal) const {
    if (!m_impl->valid(entity)) return defaultVal;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    return mat ? mat->getParam<glm::vec3>(param, defaultVal) : defaultVal;
}

void SceneAPI::setMaterialVec4(EntityHandle entity, const std::string& param,
                                const glm::vec4& value) {
    if (!m_impl->valid(entity)) return;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    if (mat) mat->setParam(param, value);
}

glm::vec4 SceneAPI::getMaterialVec4(EntityHandle entity, const std::string& param,
                                     const glm::vec4& defaultVal) const {
    if (!m_impl->valid(entity)) return defaultVal;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    return mat ? mat->getParam<glm::vec4>(param, defaultVal) : defaultVal;
}

bool SceneAPI::hasMaterialParam(EntityHandle entity, const std::string& param) const {
    if (!m_impl->valid(entity)) return false;
    auto* mat = m_impl->registry.try_get<MaterialComponentV5>(toEntt(entity));
    return mat ? mat->hasParam(param) : false;
}

// ═══════════════════════════════════════════════════════════════
// Renderable Tag Access
// ═══════════════════════════════════════════════════════════════

bool SceneAPI::isVisible(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* tag = m_impl->registry.try_get<RenderableTagComponent>(toEntt(entity));
    return tag ? tag->visible : false;
}

void SceneAPI::setVisible(EntityHandle entity, bool visible) {
    if (!m_impl->valid(entity)) return;
    auto* tag = m_impl->registry.try_get<RenderableTagComponent>(toEntt(entity));
    if (tag) tag->visible = visible;
}

bool SceneAPI::getCastShadows(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* tag = m_impl->registry.try_get<RenderableTagComponent>(toEntt(entity));
    return tag ? tag->castShadows : false;
}

void SceneAPI::setCastShadows(EntityHandle entity, bool castShadows) {
    if (!m_impl->valid(entity)) return;
    auto* tag = m_impl->registry.try_get<RenderableTagComponent>(toEntt(entity));
    if (tag) tag->castShadows = castShadows;
}

// ═══════════════════════════════════════════════════════════════
// Hierarchy Access
// ═══════════════════════════════════════════════════════════════

EntityHandle SceneAPI::getParent(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return NullEntity;
    auto* h = m_impl->registry.try_get<ECS::HierarchyComponent>(toEntt(entity));
    return h ? toHandle(h->parent) : NullEntity;
}

void SceneAPI::setParent(EntityHandle child, EntityHandle parent) {
    if (!m_impl->valid(child)) return;
    auto childEntt  = toEntt(child);
    auto parentEntt = toEntt(parent);

    // Ensure child has HierarchyComponent
    auto& childH = m_impl->registry.get_or_emplace<ECS::HierarchyComponent>(childEntt);

    // Remove from old parent
    if (childH.parent != entt::null && m_impl->registry.valid(childH.parent)) {
        auto* oldParentH = m_impl->registry.try_get<ECS::HierarchyComponent>(childH.parent);
        if (oldParentH) oldParentH->removeChild(childEntt);
    }

    // Set new parent
    childH.parent = parentEntt;

    // Add to new parent's children
    if (m_impl->registry.valid(parentEntt)) {
        auto& parentH = m_impl->registry.get_or_emplace<ECS::HierarchyComponent>(parentEntt);
        parentH.addChild(childEntt);
    }
}

std::vector<EntityHandle> SceneAPI::getChildren(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return {};
    auto* h = m_impl->registry.try_get<ECS::HierarchyComponent>(toEntt(entity));
    if (!h) return {};

    std::vector<EntityHandle> result;
    result.reserve(h->children.size());
    for (auto c : h->children) {
        result.push_back(toHandle(c));
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// Animation Access
// ═══════════════════════════════════════════════════════════════

int SceneAPI::getAnimationClipCount(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 0;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? static_cast<int>(pb->clips.size()) : 0;
}

std::string SceneAPI::getAnimationClipName(EntityHandle entity, int clipIndex) const {
    if (!m_impl->valid(entity)) return "";
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (!pb || clipIndex < 0 || clipIndex >= static_cast<int>(pb->clips.size())) return "";
    return pb->clips[clipIndex] ? pb->clips[clipIndex]->name : "";
}

float SceneAPI::getAnimationClipDuration(EntityHandle entity, int clipIndex) const {
    if (!m_impl->valid(entity)) return 0.0f;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (!pb || clipIndex < 0 || clipIndex >= static_cast<int>(pb->clips.size())) return 0.0f;
    return pb->clips[clipIndex] ? pb->clips[clipIndex]->duration : 0.0f;
}

void SceneAPI::playAnimation(EntityHandle entity, int clipIndex) {
    if (!m_impl->valid(entity)) return;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (pb) pb->play(clipIndex);
}

void SceneAPI::stopAnimation(EntityHandle entity) {
    if (!m_impl->valid(entity)) return;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (pb) pb->stop();
}

bool SceneAPI::isAnimationPlaying(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return false;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? pb->playing : false;
}

float SceneAPI::getAnimationSpeed(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 1.0f;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? pb->speed : 1.0f;
}

void SceneAPI::setAnimationSpeed(EntityHandle entity, float speed) {
    if (!m_impl->valid(entity)) return;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (pb) pb->speed = speed;
}

float SceneAPI::getAnimationTime(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return 0.0f;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? pb->currentTime : 0.0f;
}

void SceneAPI::setAnimationTime(EntityHandle entity, float time) {
    if (!m_impl->valid(entity)) return;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (pb) pb->currentTime = time;
}

bool SceneAPI::isAnimationLooping(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return true;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? pb->loop : true;
}

void SceneAPI::setAnimationLooping(EntityHandle entity, bool loop) {
    if (!m_impl->valid(entity)) return;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    if (pb) pb->loop = loop;
}

int SceneAPI::getCurrentAnimationClip(EntityHandle entity) const {
    if (!m_impl->valid(entity)) return -1;
    auto* pb = m_impl->registry.try_get<AnimationPlaybackComponent>(toEntt(entity));
    return pb ? pb->currentClipIndex : -1;
}

// ═══════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════

bool SceneAPI::saveToFile(const std::string& path) const {
    if (!m_impl->hasScene()) return false;
    return m_impl->scene->saveToFile(path);
}

bool SceneAPI::loadFromFile(const std::string& path) {
    if (!m_impl->hasScene()) return false;
    return m_impl->scene->loadFromFile(path);
}

} // namespace Facade
} // namespace Shoonyakasha
