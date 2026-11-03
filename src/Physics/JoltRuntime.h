//
// Physics/JoltRuntime.h - Jolt Physics' process-wide set-up
//
// Every user of Jolt in the engine (ECS::PhysicsSystem, Planet::PlanetPhysics)
// calls initialiseJolt before making a JPH::PhysicsSystem.
//

#pragma once

namespace Shoonyakasha::Physics {

/// Jolt's default allocator, its factory and its types registered, once per
/// process; later calls do nothing.
void initialiseJolt();

} // namespace Shoonyakasha::Physics
