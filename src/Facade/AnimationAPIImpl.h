//
// Facade/AnimationAPIImpl.h - AnimationAPI::Impl definition (internal only)
//
// Shared between AnimationAPI.cpp and EngineAPI.cpp so that EngineAPI can
// wire the registry during onInit().
//

#pragma once

#include "Facade/AnimationAPI.h"
#include <entt/entt.hpp>

namespace Shoonyakasha {
namespace Facade {

struct AnimationAPI::Impl {
    entt::registry* registry = nullptr;

    static void wire(AnimationAPI& api, entt::registry* reg) {
        api.m_impl->registry = reg;
    }
};

} // namespace Facade
} // namespace Shoonyakasha
