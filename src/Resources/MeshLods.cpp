//
// MeshLods.cpp - generated levels of detail for triangle meshes
//

#include "Resources/MeshLods.h"

#include "meshoptimizer.h"

namespace Shoonyakasha {

namespace {
// How much a change of vertex normal counts against a change of position,
// in the simplifier's error.
constexpr float kNormalWeight = 0.5f;
}

std::vector<MeshLod> appendMeshLods(const std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices,
                                    uint32_t maxLevels, const std::vector<glm::vec3>& normals) {
    std::vector<MeshLod> lods;
    const size_t full = indices.size();
    if (positions.empty() || full < 64 * 3 || full % 3 != 0) return lods;

    const float* xyz = &positions[0].x;
    const float scale = meshopt_simplifyScale(xyz, positions.size(), sizeof(glm::vec3));
    const std::vector<uint32_t> source(indices.begin(), indices.end());
    const bool withNormals = normals.size() == positions.size();
    static const float kNormalWeights[3] = {kNormalWeight, kNormalWeight, kNormalWeight};
    std::vector<uint32_t> simplified(full);
    size_t previous = full;
    for (uint32_t level = 0; level < maxLevels && previous >= 2 * 32 * 3; ++level) {
        const size_t target = previous / 2 / 3 * 3;
        float error = 0.0f;
        const size_t count = withNormals
            ? meshopt_simplifyWithAttributes(simplified.data(), source.data(), full, xyz, positions.size(),
                                             sizeof(glm::vec3), &normals[0].x, sizeof(glm::vec3), kNormalWeights, 3,
                                             nullptr, target, 1.0f, meshopt_SimplifyLockBorder, &error)
            : meshopt_simplify(simplified.data(), source.data(), full, xyz, positions.size(), sizeof(glm::vec3),
                               target, 1.0f, meshopt_SimplifyLockBorder, &error);
        if (count == 0 || count * 4 > previous * 3) break;

        MeshLod lod;
        lod.firstIndex = static_cast<uint32_t>(indices.size());
        lod.indexCount = static_cast<uint32_t>(count);
        lod.error = error * scale;
        indices.insert(indices.end(), simplified.begin(), simplified.begin() + static_cast<std::ptrdiff_t>(count));
        lods.push_back(lod);
        previous = count;
    }
    return lods;
}

} // namespace Shoonyakasha
