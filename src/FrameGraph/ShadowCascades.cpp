//
// ShadowCascades.cpp
//

#include "FrameGraph/ShadowCascades.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Shoonyakasha {

std::array<float, MAX_SUN_CASCADES + 1> cascadeSplitDepths(float nearPlane, float farDistance,
                                                            uint32_t count, float lambda) {
    std::array<float, MAX_SUN_CASCADES + 1> depths{};
    count = std::clamp(count, 1u, MAX_SUN_CASCADES);
    lambda = std::clamp(lambda, 0.0f, 1.0f);
    nearPlane = std::max(nearPlane, 1e-4f);
    farDistance = std::max(farDistance, nearPlane * 1.001f);

    for (uint32_t i = 0; i <= count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count);
        const float logSplit  = nearPlane * std::pow(farDistance / nearPlane, t);
        const float evenSplit = nearPlane + (farDistance - nearPlane) * t;
        depths[i] = lambda * logSplit + (1.0f - lambda) * evenSplit;
    }
    depths[0] = nearPlane;
    depths[count] = farDistance;
    for (uint32_t i = count + 1; i <= MAX_SUN_CASCADES; ++i) depths[i] = farDistance;
    return depths;
}

SunShadowCascades computeSunCascades(const glm::mat4& cameraView,
                                     float fovYDegrees, float aspect,
                                     float nearPlane, float farPlane,
                                     const glm::vec3& lightDirection,
                                     const SunShadowSettings& settings) {
    SunShadowCascades out;
    const float dirLength = glm::length(lightDirection);
    if (dirLength < 1e-6f || aspect <= 0.0f || fovYDegrees <= 0.0f) return out;

    out.count = std::clamp(settings.cascadeCount, 1u, MAX_SUN_CASCADES);
    const float farDistance = std::min(settings.maxDistance, farPlane);
    const auto depths = cascadeSplitDepths(nearPlane, farDistance, out.count, settings.splitLambda);

    const glm::mat4 invView = glm::inverse(cameraView);
    const glm::vec3 cameraPos = glm::vec3(invView[3]);
    const glm::vec3 forward = -glm::normalize(glm::vec3(invView[2]));

    const glm::vec3 dir = lightDirection / dirLength;
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

    // Distance from the view axis to a frustum corner, per unit of depth.
    const float tanHalf = std::tan(glm::radians(fovYDegrees) * 0.5f);
    const float k = tanHalf * std::sqrt(1.0f + aspect * aspect);
    const float resolution = static_cast<float>(std::max(settings.resolution, 1u));
    const float extension = std::max(settings.casterExtension, 0.0f);

    for (uint32_t i = 0; i < out.count; ++i) {
        const float n = depths[i];
        const float f = depths[i + 1];

        // Smallest sphere centred on the view axis that holds the slice's
        // corner rings: radius n*k at depth n and f*k at depth f.
        float centreDepth = 0.5f * (n + f) * (1.0f + k * k);
        float radius;
        if (centreDepth >= f) {
            centreDepth = f;
            radius = std::max(f * k, std::sqrt((f - n) * (f - n) + n * k * n * k));
        } else {
            radius = std::sqrt((centreDepth - n) * (centreDepth - n) + n * k * n * k);
        }
        // Quantised, so floating-point noise cannot change the texel size.
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const glm::vec3 centre = cameraPos + forward * centreDepth;
        const glm::vec3 eye = centre - dir * (radius + extension);
        const glm::mat4 view = glm::lookAtRH(eye, centre, up);
        glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + extension);

        // Move the window in whole texels: put the world origin on a texel
        // corner of this cascade's map.
        const glm::vec4 origin = proj * view * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec2 inTexels = glm::vec2(origin) * (resolution * 0.5f);
        const glm::vec2 offset = (glm::round(inTexels) - inTexels) / (resolution * 0.5f);
        proj[3][0] += offset.x;
        proj[3][1] += offset.y;

        out.viewProj[i] = proj * view;
        out.splits[static_cast<int>(i)] = f;
        out.texelWorldSize[static_cast<int>(i)] = 2.0f * radius / resolution;
    }
    for (uint32_t i = out.count; i < MAX_SUN_CASCADES; ++i) {
        out.viewProj[i] = out.viewProj[out.count - 1];
        out.splits[static_cast<int>(i)] = out.splits[static_cast<int>(out.count - 1)];
        out.texelWorldSize[static_cast<int>(i)] = out.texelWorldSize[static_cast<int>(out.count - 1)];
    }
    out.valid = true;
    return out;
}

} // namespace Shoonyakasha
