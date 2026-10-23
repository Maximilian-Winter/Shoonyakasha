//
// MeshLodsTest.cpp - generated levels of detail (appendMeshLods)
//
// Tier 1: pure CPU, on height-field grids.
//

#include <gtest/gtest.h>

#include "Resources/MeshLods.h"

#include <cmath>
#include <vector>

using namespace Shoonyakasha;

namespace {

/// An n x n grid of quads over [0, 1]^2, two triangles each, with height
/// `bump` * sin waves (0 for a flat grid).
void grid(uint32_t n, float bump, std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices) {
    positions.clear();
    indices.clear();
    for (uint32_t y = 0; y <= n; ++y) {
        for (uint32_t x = 0; x <= n; ++x) {
            const float u = static_cast<float>(x) / n, v = static_cast<float>(y) / n;
            positions.emplace_back(u, bump * std::sin(u * 12.0f) * std::sin(v * 9.0f), v);
        }
    }
    for (uint32_t y = 0; y < n; ++y) {
        for (uint32_t x = 0; x < n; ++x) {
            const uint32_t a = y * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

} // namespace

TEST(MeshLods, LevelsFollowTheFullMeshWithFewerTriangles) {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    grid(48, 0.05f, positions, indices);
    const std::vector<uint32_t> full = indices;

    const std::vector<MeshLod> lods = appendMeshLods(positions, indices);
    ASSERT_GE(lods.size(), 2u);
    // The full mesh is untouched, at the start.
    EXPECT_TRUE(std::equal(full.begin(), full.end(), indices.begin()));

    uint32_t expectedFirst = static_cast<uint32_t>(full.size());
    uint32_t previousCount = static_cast<uint32_t>(full.size());
    float previousError = 0.0f;
    for (const MeshLod& lod : lods) {
        EXPECT_EQ(lod.firstIndex, expectedFirst);
        EXPECT_EQ(lod.indexCount % 3, 0u);
        EXPECT_LE(lod.indexCount * 4, previousCount * 3);   // at most three quarters of the level before
        EXPECT_GE(lod.error, previousError);
        for (uint32_t i = lod.firstIndex; i < lod.firstIndex + lod.indexCount; ++i) {
            ASSERT_LT(indices[i], positions.size());
        }
        expectedFirst += lod.indexCount;
        previousCount = lod.indexCount;
        previousError = lod.error;
    }
    EXPECT_EQ(indices.size(), expectedFirst);
}

TEST(MeshLods, ABumpierMeshHasLargerErrors) {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> flatIndices, bumpyIndices;
    grid(48, 0.0f, positions, flatIndices);
    const std::vector<MeshLod> flat = appendMeshLods(positions, flatIndices);
    grid(48, 0.1f, positions, bumpyIndices);
    const std::vector<MeshLod> bumpy = appendMeshLods(positions, bumpyIndices);
    ASSERT_FALSE(flat.empty());
    ASSERT_FALSE(bumpy.empty());
    EXPECT_LT(flat.front().error, 1e-4f);
    EXPECT_GT(bumpy.front().error, flat.front().error);
}

TEST(MeshLods, SmallMeshesGetNone) {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    grid(5, 0.1f, positions, indices);   // 50 triangles
    const size_t before = indices.size();
    EXPECT_TRUE(appendMeshLods(positions, indices).empty());
    EXPECT_EQ(indices.size(), before);
}

TEST(MeshLods, MaxLevelsLimitsTheCount) {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    grid(48, 0.05f, positions, indices);
    EXPECT_LE(appendMeshLods(positions, indices, 1).size(), 1u);
}
