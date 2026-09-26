//
// ViewCulling.cpp
//

#include "FrameGraph/ViewCulling.h"

#include <cmath>

namespace Shoonyakasha {

Frustum frustumFromViewProj(const glm::mat4& m, ClipDepth depth, bool withNearPlane) {
    // Row i of a column-major glm matrix.
    auto row = [&](int i) { return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]); };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    Frustum f;
    f.planes[f.count++] = r3 + r0;  // left:   -w <= x
    f.planes[f.count++] = r3 - r0;  // right:   x <= w
    f.planes[f.count++] = r3 + r1;  // bottom: -w <= y
    f.planes[f.count++] = r3 - r1;  // top:     y <= w
    f.planes[f.count++] = r3 - r2;  // far:     z <= w
    if (withNearPlane) {
        // near: 0 <= z, or -w <= z
        f.planes[f.count++] = depth == ClipDepth::ZeroToOne ? r2 : r3 + r2;
    }
    return f;
}

void transformBounds(const glm::mat4& transform,
                     const glm::vec3& localMin, const glm::vec3& localMax,
                     glm::vec3& outMin, glm::vec3& outMax) {
    // Arvo: each output axis gathers the smaller and larger product of every
    // matrix entry with the input extent on that axis.
    const glm::vec3 translation = glm::vec3(transform[3]);
    outMin = translation;
    outMax = translation;
    for (int col = 0; col < 3; ++col) {
        for (int r = 0; r < 3; ++r) {
            const float a = transform[col][r] * localMin[col];
            const float b = transform[col][r] * localMax[col];
            outMin[r] += std::fmin(a, b);
            outMax[r] += std::fmax(a, b);
        }
    }
}

bool boundsInFrustum(const Frustum& frustum, const glm::vec3& min, const glm::vec3& max) {
    for (uint32_t i = 0; i < frustum.count; ++i) {
        const glm::vec4& p = frustum.planes[i];
        // The corner furthest along the plane's normal.
        const glm::vec3 corner(p.x >= 0.0f ? max.x : min.x,
                               p.y >= 0.0f ? max.y : min.y,
                               p.z >= 0.0f ? max.z : min.z);
        if (glm::dot(glm::vec3(p), corner) + p.w < 0.0f) return false;
    }
    return true;
}

} // namespace Shoonyakasha
