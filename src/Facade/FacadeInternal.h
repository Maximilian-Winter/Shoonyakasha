//
// Facade/FacadeInternal.h - Internal helpers for facade implementations
//
// NOT a public header — never included from include/Facade/*.h
// Provides entity handle conversion between Facade::EntityHandle and entt::entity
//

#pragma once

#include "Facade/FacadeTypes.h"
#include <entt/entt.hpp>

namespace Shoonyakasha {
namespace Facade {
namespace Internal {

inline entt::entity toEntt(EntityHandle handle) {
    return static_cast<entt::entity>(handle);
}

inline EntityHandle toHandle(entt::entity entity) {
    return static_cast<EntityHandle>(entity);
}

} // namespace Internal
} // namespace Facade
} // namespace Shoonyakasha
