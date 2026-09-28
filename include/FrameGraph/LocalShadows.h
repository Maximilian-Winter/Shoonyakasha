//
// LocalShadows.h
//
// Shadows for spot and point lights: which lights get one this frame, and the
// matrices that render and read them.
//
// A spot light's shadow is a perspective map covering its cone. A point
// light's is a cube map, rendered one face at a time; each face's matrix is
// built from the cube-map face selection table of the Vulkan specification
// (the same as OpenGL's), so a depth rendered through face F lands on the
// texel a cube-map lookup along the same direction reads. Depth is the usual
// 0..1 perspective depth of the distance along the face's major axis, which
// a shader recomputes from the light-to-point vector as `depthA + depthB / m`,
// m being that vector's largest absolute component.
//
// Lights compete for a fixed number of slots, a budget per type that must
// match the pipeline's shadow maps. Those whose range cannot reach the
// camera's view are skipped; the rest are ranked by intensity and by how
// close they are to the camera.
//
// Pure math, no device: SceneContext fills itself with it each frame, and the
// unit tests call it directly.
//

#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace Shoonyakasha {

constexpr uint32_t MAX_SPOT_SHADOWS  = 8;
constexpr uint32_t MAX_POINT_SHADOWS = 4;

struct LocalShadowSettings {
    uint32_t spotCount       = 4;     // slots for spot lights, 0..MAX_SPOT_SHADOWS; match the pipeline
    uint32_t pointCount      = 2;     // slots for point lights, 0..MAX_POINT_SHADOWS; match the pipeline
    uint32_t spotResolution  = 1024;  // texels per side of a spot map, for the published texel size
    uint32_t pointResolution = 512;   // texels per side of a cube face
    float    nearPlane       = 0.05f; // closest caster distance from the light
    float    defaultRange    = 50.0f; // far plane for lights with no range (range <= 0)
};

struct SpotShadow {
    int32_t   lightIndex = -1;          // index in SceneContext::lights; -1 for an empty slot
    glm::mat4 viewProj{1.0f};           // world to light clip space, depth 0..1
    /// x = world size of a texel one unit from the light, y = near, z = far
    glm::vec4 params{0.0f};
};

struct PointShadow {
    int32_t   lightIndex = -1;
    std::array<glm::mat4, 6> faceViewProj{};   // +X, -X, +Y, -Y, +Z, -Z: the cube array's layer order
    glm::vec4 positionFar{0.0f};               // xyz = light position, w = far
    /// x, y = depth from the major-axis distance m: x + y / m; z = world size
    /// of a texel one unit from the light; w = near
    glm::vec4 depthParams{0.0f};
};

struct LocalShadows {
    std::array<SpotShadow, MAX_SPOT_SHADOWS>   spot{};
    std::array<PointShadow, MAX_POINT_SHADOWS> point{};
    uint32_t spotCount  = 0;   // slots in use; they come first
    uint32_t pointCount = 0;
};

/// A light that asked for a shadow (LightComponent::castShadows).
struct LocalShadowCandidate {
    uint32_t  lightIndex = 0;
    bool      spot = false;         // else point
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};   // spot lights: the way the light shines
    float     range = 0.0f;         // <= 0: settings.defaultRange
    float     cosOuterCone = 0.7f;  // spot lights: cosine of the cone's half angle
    float     intensity = 1.0f;
};

/// World to clip for a spot light's shadow: a square perspective covering its
/// cone, depth 0..1 from `nearPlane` to `farPlane`.
glm::mat4 spotShadowViewProj(const glm::vec3& position, const glm::vec3& direction,
                             float cosOuterCone, float nearPlane, float farPlane);

/// World to clip for each face of a point light's cube map, in layer order.
std::array<glm::mat4, 6> pointShadowFaceViewProj(const glm::vec3& position, float nearPlane, float farPlane);

/// Pick which candidates get a slot and compute their shadows. `cameraViewProj`
/// culls lights whose sphere of influence is outside the view (identity: no
/// culling); `cameraClip` says whether it maps depth to -1..1 (true) or 0..1.
LocalShadows assignLocalShadows(const std::vector<LocalShadowCandidate>& candidates,
                                const glm::vec3& cameraPosition, const glm::mat4& cameraViewProj,
                                bool cameraMinusOneToOne, const LocalShadowSettings& settings);

} // namespace Shoonyakasha
