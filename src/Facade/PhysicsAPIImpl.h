//
// Facade/PhysicsAPIImpl.h - PhysicsAPI::Impl definition (internal only)
//
// Shared between PhysicsAPI.cpp and EngineAPI.cpp so that EngineAPI
// can wire the internal pointers during onInit().
//

#pragma once

#include "Facade/PhysicsAPI.h"
#include "ECS/PhysicsSystem.h"
#include <entt/entt.hpp>

namespace Shoonyakasha {
namespace Facade {

struct PhysicsAPI::Impl {
    ECS::PhysicsSystem* physicsSystem = nullptr;
    entt::registry*     registry      = nullptr;

    // Wire from EngineAPI — Impl is a nested class so it can access private m_impl
    static void wire(PhysicsAPI& api,
                     ECS::PhysicsSystem* system,
                     entt::registry* reg) {
        api.m_impl->physicsSystem = system;
        api.m_impl->registry      = reg;
    }
};

} // namespace Facade
} // namespace Shoonyakasha
