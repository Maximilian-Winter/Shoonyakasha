//
// ShadowCascades.h
//
// Cascaded shadow maps for a directional light: where the cascades split the
// view, and the light-space matrix that covers each one.
//
// Each cascade is fitted with a bounding sphere of its slice of the view
// frustum. The sphere depends only on the slice's depths, the field of view
// and the aspect ratio, so turning the camera leaves the shadow map's
// resolution unchanged; and the matrix is snapped so the world moves across the
// map in whole texels. Together they keep shadow edges from shimmering as the
// camera moves.
//
// Pure math, no device: RenderGraph fills SceneContext::sunShadow with it each
// frame, and the unit tests call it directly.
//

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

namespace Shoonyakasha {

/// Most cascades a sun shadow can have (splits are published as a vec4).
constexpr uint32_t MAX_SUN_CASCADES = 4;

struct SunShadowSettings {
    uint32_t cascadeCount    = 4;      // 1..MAX_SUN_CASCADES
    float    maxDistance     = 60.0f;  // view depth the cascades cover; capped at the camera's far plane
    float    splitLambda     = 0.75f;  // 0 = evenly spaced splits, 1 = logarithmic
    uint32_t resolution      = 2048;   // shadow map texels per side, for snapping; match the map
    float    casterExtension = 50.0f;  // how far towards the light casters outside a cascade still count
};

struct SunShadowCascades {
    bool     valid = false;
    uint32_t count = 0;
    /// World to light clip space, depth mapped to [0, 1], one per cascade.
    std::array<glm::mat4, MAX_SUN_CASCADES> viewProj{
        glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
    /// View depth at which cascade i ends. Unused entries repeat the last.
    glm::vec4 splits{0.0f};
    /// World size of one shadow texel in cascade i, for normal-offset bias.
    glm::vec4 texelWorldSize{0.0f};
};

/// Depths bounding each cascade: `count + 1` values from `nearPlane` to
/// `farDistance`, blended between logarithmic and even spacing by `lambda`.
std::array<float, MAX_SUN_CASCADES + 1> cascadeSplitDepths(float nearPlane, float farDistance,
                                                            uint32_t count, float lambda);

/// Fit the cascades of a directional light travelling along `lightDirection`
/// to a camera. `cameraView` maps world to view space (looking down -Z);
/// `fovYDegrees` is the vertical field of view.
SunShadowCascades computeSunCascades(const glm::mat4& cameraView,
                                     float fovYDegrees, float aspect,
                                     float nearPlane, float farPlane,
                                     const glm::vec3& lightDirection,
                                     const SunShadowSettings& settings);

} // namespace Shoonyakasha
