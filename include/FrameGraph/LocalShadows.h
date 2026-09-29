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
// The maps are either layers of arrays, each at a fixed resolution, or tiles
// of one atlas (atlasResolution > 0). In an atlas each light's tiles are
// sized by how large its range looks from the camera, as a power of two up to
// spotResolution (pointResolution for each cube face), and halved, least
// important light first, until they all fit; then packed largest first, each
// at the next free position of a Z-order curve, which fills a power-of-two
// square without gaps when the sizes come in decreasing powers of two. Each
// slot and face publishes its tile as a rectangle in fractions of the atlas.
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
    uint32_t spotCount       = 8;     // slots for spot lights, 0..MAX_SPOT_SHADOWS; match the pipeline
    uint32_t pointCount      = 4;     // slots for point lights, 0..MAX_POINT_SHADOWS; match the pipeline
    uint32_t spotResolution  = 2048;  // texels per side of a spot map (in an atlas, the largest tile)
    uint32_t pointResolution = 1024;  // texels per side of a cube face (in an atlas, the largest tile)
    uint32_t atlasResolution = 4096;  // texels per side of the atlas holding every map; 0 for arrays
    uint32_t minTileResolution = 128; // the smallest tile an atlas gives a map
    float    nearPlane       = 0.05f; // closest caster distance from the light
    float    defaultRange    = 50.0f; // far plane for lights with no range (range <= 0)
};

struct SpotShadow {
    int32_t   lightIndex = -1;          // index in SceneContext::lights; -1 for an empty slot
    glm::mat4 viewProj{1.0f};           // world to light clip space, depth 0..1
    /// x = world size of a texel one unit from the light, y = near, z = far
    glm::vec4 params{0.0f};
    /// The map's tile in the atlas: x, y, width, height in fractions of it;
    /// (0, 0, 1, 1) with arrays, all 0 for an empty slot.
    glm::vec4 rect{0.0f};
};

struct PointShadow {
    int32_t   lightIndex = -1;
    std::array<glm::mat4, 6> faceViewProj{};   // +X, -X, +Y, -Y, +Z, -Z: the cube array's layer order
    glm::vec4 positionFar{0.0f};               // xyz = light position, w = far
    /// x, y = depth from the major-axis distance m: x + y / m; z = world size
    /// of a texel one unit from the light; w = near
    glm::vec4 depthParams{0.0f};
    std::array<glm::vec4, 6> faceRect{};       // each face's tile, as SpotShadow::rect
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
/// Pack square tiles, each a power of two no larger than `atlas` (also a
/// power of two), largest first along a Z-order curve. Returns each tile's
/// top-left corner in texels, in the order given, or an empty vector when
/// they do not fit.
std::vector<glm::uvec2> packAtlasTiles(const std::vector<uint32_t>& sizes, uint32_t atlas);

LocalShadows assignLocalShadows(const std::vector<LocalShadowCandidate>& candidates,
                                const glm::vec3& cameraPosition, const glm::mat4& cameraViewProj,
                                bool cameraMinusOneToOne, const LocalShadowSettings& settings);

} // namespace Shoonyakasha
