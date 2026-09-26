//
// ViewCulling.h
//
// Frustum planes from a view-projection matrix, and a conservative test of
// world-space bounding boxes against them. Pure math, no device.
//

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace Shoonyakasha {

/// Clip-space depth range a projection matrix produces.
enum class ClipDepth {
    MinusOneToOne,  // glm::perspective without GLM_FORCE_DEPTH_ZERO_TO_ONE (the camera)
    ZeroToOne       // *_ZO projections (the sun cascades)
};

/// Planes with dot(xyz, p) + w >= 0 on the inside. Not normalised; only
/// signs are used.
struct Frustum {
    std::array<glm::vec4, 6> planes{};
    uint32_t count = 0;
};

/// The planes of `viewProj`'s clip volume. Without the near plane, anything
/// between the eye and the near plane still passes — what a shadow pass with
/// depth clamping needs, since casters there still write depth.
Frustum frustumFromViewProj(const glm::mat4& viewProj, ClipDepth depth, bool withNearPlane = true);

/// World-space bounds of a mesh-space box under `transform`.
void transformBounds(const glm::mat4& transform,
                     const glm::vec3& localMin, const glm::vec3& localMax,
                     glm::vec3& outMin, glm::vec3& outMax);

/// False only when the box lies entirely outside one plane. Boxes near a
/// corner of the frustum can pass while outside it, never the reverse.
bool boundsInFrustum(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max);

} // namespace Shoonyakasha
