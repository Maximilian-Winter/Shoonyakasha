//
// MorphTargets.cpp - Blending a mesh's morph targets by weight
//

#include "ECS/MorphComponents.h"

#include <algorithm>
#include <cstring>

namespace Shoonyakasha {

void blendMorphTargets(const MorphTargetSet& set, const std::vector<float>& weights, uint8_t* out) {
    const size_t bytes = static_cast<size_t>(set.vertexCount) * set.vertexStride;
    if (set.baseVertices.size() < bytes) return;
    std::memcpy(out, set.baseVertices.data(), bytes);

    const uint32_t targets = std::min<uint32_t>(set.targetCount, static_cast<uint32_t>(weights.size()));
    const size_t perTarget = set.vertexCount;
    if (set.positionDeltas.size() < perTarget * set.targetCount) return;
    const bool normals = set.normalDeltas.size() >= perTarget * set.targetCount;
    const bool tangents = set.tangentDeltas.size() >= perTarget * set.targetCount;
    auto vec3At = [&](uint32_t v, uint32_t offset) {
        glm::vec3 x;
        std::memcpy(&x, out + static_cast<size_t>(v) * set.vertexStride + offset, sizeof(x));
        return x;
    };
    auto storeVec3 = [&](uint32_t v, uint32_t offset, const glm::vec3& x) {
        std::memcpy(out + static_cast<size_t>(v) * set.vertexStride + offset, &x, sizeof(x));
    };

    for (uint32_t v = 0; v < set.vertexCount; ++v) {
        glm::vec3 position = vec3At(v, set.positionOffset);
        glm::vec3 normal = vec3At(v, set.normalOffset);
        glm::vec3 tangent = vec3At(v, set.tangentOffset);   // xyz of the vec4; w stays
        bool moved = false, turned = false, tilted = false;
        for (uint32_t t = 0; t < targets; ++t) {
            const float w = weights[t];
            if (w == 0.0f) continue;
            const size_t i = t * perTarget + v;
            position += w * set.positionDeltas[i];
            moved = true;
            if (normals) { normal += w * set.normalDeltas[i]; turned = true; }
            if (tangents) { tangent += w * set.tangentDeltas[i]; tilted = true; }
        }
        if (!moved) continue;
        storeVec3(v, set.positionOffset, position);
        if (turned) {
            const float length = glm::length(normal);
            if (length > 0.0f) storeVec3(v, set.normalOffset, normal / length);
        }
        if (tilted) {
            const float length = glm::length(tangent);
            if (length > 0.0f) storeVec3(v, set.tangentOffset, tangent / length);
        }
    }
}

} // namespace Shoonyakasha
