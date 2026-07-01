//
// Facade/EcsAPI.cpp - Implementation delegating to ECS::Scene + entt::registry
//

#include <entt/entt.hpp>
#include "ECS/Core.h"
#include "ECS/Systems.h"

#include "Facade/EcsAPI.h"
#include "FacadeInternal.h"

#include "ECS/Scene.h"
#include "ECS/ScriptComponentBag.h"

using namespace Shoonyakasha::Facade::Internal;

namespace Shoonyakasha {
namespace Facade {

// ═══════════════════════════════════════════════════════════════
// PIMPL Implementation
// ═══════════════════════════════════════════════════════════════

struct EcsAPI::Impl {
    entt::registry& registry;
    ECS::SystemManager& systemManager;

    Impl(entt::registry& r, ECS::SystemManager& sm)
        : registry(r), systemManager(sm)
    {}

    bool valid(EntityHandle h) const {
        return registry.valid(toEntt(h));
    }

    ScriptComponentBag* getBag(EntityHandle h) {
        if (!valid(h)) return nullptr;
        return registry.try_get<ScriptComponentBag>(toEntt(h));
    }

    const ScriptComponentBag* getBag(EntityHandle h) const {
        if (!valid(h)) return nullptr;
        return registry.try_get<ScriptComponentBag>(toEntt(h));
    }

    ScriptComponentBag& getOrCreateBag(EntityHandle h) {
        return registry.get_or_emplace<ScriptComponentBag>(toEntt(h));
    }
};

// ═══════════════════════════════════════════════════════════════
// Construction / Destruction
// ═══════════════════════════════════════════════════════════════

EcsAPI::EcsAPI(ECS::Scene& scene)
    : m_impl(std::make_unique<Impl>(scene.getRegistry(), scene.getSystemManager()))
{}

#ifdef SHOONYAKASHA_TESTING
EcsAPI::EcsAPI(entt::registry& registry, ECS::SystemManager& systemManager)
    : m_impl(std::make_unique<Impl>(registry, systemManager))
{}
#endif

EcsAPI::~EcsAPI() = default;

// ═══════════════════════════════════════════════════════════════
// Script Component Access
// ═══════════════════════════════════════════════════════════════

void EcsAPI::setComponent(EntityHandle entity, const std::string& name, std::shared_ptr<void> data) {
    if (!m_impl->valid(entity)) return;
    m_impl->getOrCreateBag(entity).set(name, std::move(data));
}

std::shared_ptr<void> EcsAPI::getComponent(EntityHandle entity, const std::string& name) const {
    auto* bag = m_impl->getBag(entity);
    return bag ? bag->get(name) : nullptr;
}

bool EcsAPI::hasComponent(EntityHandle entity, const std::string& name) const {
    auto* bag = m_impl->getBag(entity);
    return bag ? bag->has(name) : false;
}

bool EcsAPI::removeComponent(EntityHandle entity, const std::string& name) {
    auto* bag = m_impl->getBag(entity);
    return bag ? bag->remove(name) : false;
}

std::vector<std::string> EcsAPI::getComponentNames(EntityHandle entity) const {
    std::vector<std::string> names;
    auto* bag = m_impl->getBag(entity);
    if (bag) {
        names.reserve(bag->components.size());
        for (const auto& [name, value] : bag->components) {
            names.push_back(name);
        }
    }
    return names;
}

std::vector<EntityHandle> EcsAPI::findEntitiesWithComponent(const std::string& name) const {
    std::vector<EntityHandle> result;
    auto view = m_impl->registry.view<ScriptComponentBag>();
    for (auto entity : view) {
        const auto& bag = view.get<ScriptComponentBag>(entity);
        if (bag.has(name)) {
            result.push_back(toHandle(entity));
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// System Management
// ═══════════════════════════════════════════════════════════════

bool EcsAPI::addSystem(const std::string& name, SystemUpdateFn fn,
                        int priority, int maxConsecutiveFailures) {
    if (name.empty() || !fn) return false;
    if (m_impl->systemManager.findSystem(name) != nullptr) return false;

    m_impl->systemManager.addSystem<ECS::CallbackSystem>(
        name, std::move(fn), priority, maxConsecutiveFailures);
    return true;
}

bool EcsAPI::removeSystem(const std::string& name) {
    return m_impl->systemManager.removeSystem(name);
}

bool EcsAPI::hasSystem(const std::string& name) const {
    return m_impl->systemManager.findSystem(name) != nullptr;
}

bool EcsAPI::setSystemEnabled(const std::string& name, bool enabled) {
    auto* system = m_impl->systemManager.findSystem(name);
    if (!system) return false;
    system->enabled = enabled;
    return true;
}

bool EcsAPI::isSystemEnabled(const std::string& name) const {
    auto* system = m_impl->systemManager.findSystem(name);
    return system ? system->enabled : false;
}

int EcsAPI::getSystemFailureCount(const std::string& name) const {
    auto* system = dynamic_cast<ECS::CallbackSystem*>(m_impl->systemManager.findSystem(name));
    return system ? system->getConsecutiveFailures() : 0;
}

int EcsAPI::getSystemMaxFailures(const std::string& name) const {
    auto* system = dynamic_cast<ECS::CallbackSystem*>(m_impl->systemManager.findSystem(name));
    return system ? system->getMaxConsecutiveFailures() : 0;
}

void EcsAPI::setSystemMaxFailures(const std::string& name, int max) {
    auto* system = dynamic_cast<ECS::CallbackSystem*>(m_impl->systemManager.findSystem(name));
    if (system) system->setMaxConsecutiveFailures(max);
}

void EcsAPI::resetSystemFailureCount(const std::string& name) {
    auto* system = dynamic_cast<ECS::CallbackSystem*>(m_impl->systemManager.findSystem(name));
    if (system) system->resetFailureCount();
}

} // namespace Facade
} // namespace Shoonyakasha
