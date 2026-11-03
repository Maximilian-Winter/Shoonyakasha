#include "JoltRuntime.h"

// Jolt.h comes first: it sets up the macros every other Jolt header uses.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <mutex>

namespace Shoonyakasha::Physics {

void initialiseJolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

} // namespace Shoonyakasha::Physics
