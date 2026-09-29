//
// LocalShadows.cpp
//

#include "FrameGraph/LocalShadows.h"
#include "FrameGraph/ViewCulling.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

namespace {

/// A spot map's field of view: the cone's full angle, a little wider so the
/// cone's edge is not the map's edge.
float spotFov(float cosOuterCone) {
    const float halfAngle = std::acos(std::clamp(cosOuterCone, 0.05f, 1.0f));
    return std::min(2.0f * halfAngle + glm::radians(4.0f), glm::radians(170.0f));
}

} // namespace

glm::mat4 spotShadowViewProj(const glm::vec3& position, const glm::vec3& direction,
                             float cosOuterCone, float nearPlane, float farPlane) {
    const glm::vec3 forward = glm::normalize(direction);
    const glm::vec3 up = std::abs(forward.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 view = glm::lookAtRH(position, position + forward, up);
    return glm::perspectiveRH_ZO(spotFov(cosOuterCone), 1.0f, nearPlane, farPlane) * view;
}

std::array<glm::mat4, 6> pointShadowFaceViewProj(const glm::vec3& position, float nearPlane, float farPlane) {
    // Vulkan's cube-map face selection (spec, "Cube Map Face Selection"):
    // for direction r and face F, the texel is at
    //   s = (sc / |ma| + 1) / 2,   t = (tc / |ma| + 1) / 2
    // with sc, tc, ma below. Rendering puts clip x / w and y / w at the
    // framebuffer's (2s - 1, 2t - 1), so clip = (sc, tc, z, |ma|) renders the
    // texel the lookup reads.
    struct Face { glm::vec3 sc, tc, ma; };
    const Face faces[6] = {
        {{0, 0, -1}, {0, -1, 0}, { 1, 0, 0}},   // +X
        {{0, 0,  1}, {0, -1, 0}, {-1, 0, 0}},   // -X
        {{1, 0,  0}, {0, 0,  1}, { 0, 1, 0}},   // +Y
        {{1, 0,  0}, {0, 0, -1}, { 0,-1, 0}},   // -Y
        {{1, 0,  0}, {0, -1, 0}, { 0, 0, 1}},   // +Z
        {{-1, 0, 0}, {0, -1, 0}, { 0, 0,-1}},   // -Z
    };
    // Depth 0 at |ma| = near, 1 at far: z / w = A + B / |ma|.
    const float A = farPlane / (farPlane - nearPlane);
    const float B = -farPlane * nearPlane / (farPlane - nearPlane);

    std::array<glm::mat4, 6> out{};
    for (int f = 0; f < 6; ++f) {
        glm::mat4 m(0.0f);
        for (int c = 0; c < 3; ++c) {
            m[c][0] = faces[f].sc[c];
            m[c][1] = faces[f].tc[c];
            m[c][2] = A * faces[f].ma[c];
            m[c][3] = faces[f].ma[c];
        }
        m[3][2] = B;
        // Relative to the light.
        out[f] = m * glm::translate(glm::mat4(1.0f), -position);
    }
    return out;
}

namespace {

uint32_t floorPowerOfTwo(uint32_t v) {
    if (v == 0) return 0;
    uint32_t p = 1;
    while (p <= v / 2) p *= 2;
    return p;
}

/// Inverse of interleaving x and y bits: x from the even bits, y from the odd.
glm::uvec2 mortonDecode(uint32_t index) {
    glm::uvec2 out(0);
    for (uint32_t bit = 0; bit < 16; ++bit) {
        out.x |= ((index >> (2 * bit)) & 1u) << bit;
        out.y |= ((index >> (2 * bit + 1)) & 1u) << bit;
    }
    return out;
}

bool sphereInFrustum(const Frustum& f, const glm::vec3& center, float radius) {
    for (uint32_t i = 0; i < f.count; ++i) {
        const glm::vec4& p = f.planes[i];
        const float length = glm::length(glm::vec3(p));
        if (length <= 0.0f) continue;
        if ((glm::dot(glm::vec3(p), center) + p.w) / length < -radius) return false;
    }
    return true;
}

} // namespace

std::vector<glm::uvec2> packAtlasTiles(const std::vector<uint32_t>& sizes, uint32_t atlas) {
    if (sizes.empty()) return {};
    std::vector<uint32_t> order(sizes.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return sizes[a] > sizes[b]; });
    const uint32_t unit = sizes[order.back()];
    if (unit == 0) return {};

    // `cursor` counts unit-sized squares along the curve. A tile of side s
    // covers (s / unit)^2 of them, and since every earlier tile was at least
    // as large, the cursor is a multiple of that: the tile starts at a
    // corner of an aligned s-sized square.
    const uint64_t capacity = uint64_t(atlas / unit) * (atlas / unit);
    uint64_t cursor = 0;
    std::vector<glm::uvec2> out(sizes.size());
    for (uint32_t i : order) {
        const uint32_t side = sizes[i] / unit;
        const uint64_t cells = uint64_t(side) * side;
        if (sizes[i] > atlas || cursor + cells > capacity) return {};
        out[i] = mortonDecode(static_cast<uint32_t>(cursor / cells)) * sizes[i];
        cursor += cells;
    }
    return out;
}

LocalShadows assignLocalShadows(const std::vector<LocalShadowCandidate>& candidates,
                                const glm::vec3& cameraPosition, const glm::mat4& cameraViewProj,
                                bool cameraMinusOneToOne, const LocalShadowSettings& settings) {
    LocalShadows out;
    const bool cull = cameraViewProj != glm::mat4(1.0f);
    const Frustum frustum = frustumFromViewProj(
        cameraViewProj, cameraMinusOneToOne ? ClipDepth::MinusOneToOne : ClipDepth::ZeroToOne);

    struct Ranked { const LocalShadowCandidate* light; float range; float score; };
    std::vector<Ranked> spots, points;
    for (const auto& c : candidates) {
        const float range = c.range > 0.0f ? c.range : settings.defaultRange;
        if (cull && !sphereInFrustum(frustum, c.position, range)) continue;
        // Brighter and nearer first; a light around the camera ranks as if at
        // the edge of its range.
        const float distance = std::max(glm::length(c.position - cameraPosition) - range, 0.0f);
        const float score = c.intensity * range / (1.0f + distance * distance);
        (c.spot ? spots : points).push_back({&c, range, score});
    }
    auto byScore = [](const Ranked& a, const Ranked& b) {
        return a.score != b.score ? a.score > b.score : a.light->lightIndex < b.light->lightIndex;
    };
    std::sort(spots.begin(), spots.end(), byScore);
    std::sort(points.begin(), points.end(), byScore);

    // How large each chosen light's range looks from the camera, 1 from
    // inside it, and its rank, for sizing atlas tiles.
    auto coverage = [&](const Ranked& r) {
        const float distance = glm::length(r.light->position - cameraPosition);
        return distance <= r.range ? 1.0f : r.range / distance;
    };
    std::array<Ranked, MAX_SPOT_SHADOWS> chosenSpots{};
    std::array<Ranked, MAX_POINT_SHADOWS> chosenPoints{};

    const float nearPlane = settings.nearPlane;
    const uint32_t spotBudget = std::min(settings.spotCount, MAX_SPOT_SHADOWS);
    for (const auto& r : spots) {
        if (out.spotCount >= spotBudget) break;
        chosenSpots[out.spotCount] = r;
        auto& s = out.spot[out.spotCount++];
        const float farPlane = std::max(r.range, nearPlane * 2.0f);
        s.lightIndex = static_cast<int32_t>(r.light->lightIndex);
        s.viewProj = spotShadowViewProj(r.light->position, r.light->direction, r.light->cosOuterCone,
                                        nearPlane, farPlane);
        // The map spans 2 tan(fov / 2) world units one unit from the light.
        const float spanAtOne = 2.0f * std::tan(spotFov(r.light->cosOuterCone) * 0.5f);
        s.params = glm::vec4(spanAtOne / static_cast<float>(std::max(settings.spotResolution, 1u)),
                             nearPlane, farPlane, 0.0f);
    }

    const uint32_t pointBudget = std::min(settings.pointCount, MAX_POINT_SHADOWS);
    for (const auto& r : points) {
        if (out.pointCount >= pointBudget) break;
        chosenPoints[out.pointCount] = r;
        auto& p = out.point[out.pointCount++];
        const float farPlane = std::max(r.range, nearPlane * 2.0f);
        p.lightIndex = static_cast<int32_t>(r.light->lightIndex);
        p.faceViewProj = pointShadowFaceViewProj(r.light->position, nearPlane, farPlane);
        p.positionFar = glm::vec4(r.light->position, farPlane);
        p.depthParams = glm::vec4(farPlane / (farPlane - nearPlane),
                                  -farPlane * nearPlane / (farPlane - nearPlane),
                                  2.0f / static_cast<float>(std::max(settings.pointResolution, 1u)),
                                  nearPlane);
    }

    if (settings.atlasResolution == 0) {
        for (uint32_t i = 0; i < out.spotCount; ++i) out.spot[i].rect = glm::vec4(0, 0, 1, 1);
        for (uint32_t i = 0; i < out.pointCount; ++i) out.point[i].faceRect.fill(glm::vec4(0, 0, 1, 1));
        return out;
    }

    // Tile sizes: powers of two, by how large the light looks.
    const uint32_t atlas = floorPowerOfTwo(settings.atlasResolution);
    const uint32_t minTile = std::min(floorPowerOfTwo(std::max(settings.minTileResolution, 1u)), atlas);
    const uint32_t maxSpot = std::clamp(floorPowerOfTwo(settings.spotResolution), minTile, atlas);
    const uint32_t maxPoint = std::clamp(floorPowerOfTwo(settings.pointResolution), minTile, atlas);
    auto tileFor = [&](const Ranked& r, uint32_t largest) {
        const float wanted = static_cast<float>(largest) * coverage(r);
        return std::clamp(floorPowerOfTwo(static_cast<uint32_t>(wanted)), minTile, largest);
    };
    struct Tiled { bool point; uint32_t slot; float score; uint32_t size; };
    std::vector<Tiled> lights;
    for (uint32_t i = 0; i < out.spotCount; ++i)
        lights.push_back({false, i, chosenSpots[i].score, tileFor(chosenSpots[i], maxSpot)});
    for (uint32_t i = 0; i < out.pointCount; ++i)
        lights.push_back({true, i, chosenPoints[i].score, tileFor(chosenPoints[i], maxPoint)});

    // Halve the least important lights' tiles until everything fits. A
    // point light has six tiles.
    std::stable_sort(lights.begin(), lights.end(), [](const Tiled& a, const Tiled& b) { return a.score < b.score; });
    auto area = [&] {
        uint64_t total = 0;
        for (const auto& l : lights) total += uint64_t(l.size) * l.size * (l.point ? 6u : 1u);
        return total;
    };
    const uint64_t capacity = uint64_t(atlas) * atlas;
    while (area() > capacity) {
        auto it = std::find_if(lights.begin(), lights.end(), [&](const Tiled& l) { return l.size > minTile; });
        if (it != lights.end()) {
            it->size /= 2;
        } else {
            lights.erase(lights.begin());   // even the smallest tiles do not fit: drop the least important
        }
    }

    std::vector<uint32_t> sizes;
    for (const auto& l : lights)
        for (uint32_t f = 0; f < (l.point ? 6u : 1u); ++f) sizes.push_back(l.size);
    const std::vector<glm::uvec2> corners = packAtlasTiles(sizes, atlas);

    // Lights dropped above lose their slots; the rest keep their order.
    LocalShadows packed;
    std::array<int32_t, MAX_SPOT_SHADOWS> spotTile{};
    std::array<int32_t, MAX_POINT_SHADOWS> pointTile{};
    spotTile.fill(-1);
    pointTile.fill(-1);
    uint32_t next = 0;
    for (uint32_t i = 0; i < lights.size(); ++i) {
        (lights[i].point ? pointTile[lights[i].slot] : spotTile[lights[i].slot]) = static_cast<int32_t>(next);
        next += lights[i].point ? 6u : 1u;
    }
    auto rectOf = [&](uint32_t tile) {
        const float scale = 1.0f / static_cast<float>(atlas);
        return glm::vec4(glm::vec2(corners[tile]) * scale, glm::vec2(static_cast<float>(sizes[tile]) * scale));
    };
    for (uint32_t i = 0; i < out.spotCount; ++i) {
        if (spotTile[i] < 0 || corners.empty()) continue;
        auto& s = packed.spot[packed.spotCount++];
        s = out.spot[i];
        const uint32_t tile = static_cast<uint32_t>(spotTile[i]);
        s.params.x *= static_cast<float>(std::max(settings.spotResolution, 1u)) / static_cast<float>(sizes[tile]);
        s.rect = rectOf(tile);
    }
    for (uint32_t i = 0; i < out.pointCount; ++i) {
        if (pointTile[i] < 0 || corners.empty()) continue;
        auto& p = packed.point[packed.pointCount++];
        p = out.point[i];
        const uint32_t tile = static_cast<uint32_t>(pointTile[i]);
        p.depthParams.z = 2.0f / static_cast<float>(sizes[tile]);
        for (uint32_t f = 0; f < 6; ++f) p.faceRect[f] = rectOf(tile + f);
    }
    return packed;
}

} // namespace Shoonyakasha
