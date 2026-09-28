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

    const float nearPlane = settings.nearPlane;
    const uint32_t spotBudget = std::min(settings.spotCount, MAX_SPOT_SHADOWS);
    for (const auto& r : spots) {
        if (out.spotCount >= spotBudget) break;
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
    return out;
}

} // namespace Shoonyakasha
