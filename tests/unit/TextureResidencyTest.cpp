#include "Resources/TextureResidency.h"

#include <gtest/gtest.h>

using namespace Shoonyakasha::TextureResidency;

TEST(TextureResidency, UvPerUnitIsTheSquareRootOfUvAreaOverArea) {
    // A 2 x 2 quad mapped to the whole unit square: uv area 1, area 4.
    const std::vector<glm::vec3> positions{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}};
    const std::vector<glm::vec2> uvs{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const std::vector<uint32_t> indices{0, 1, 2, 0, 2, 3};
    EXPECT_FLOAT_EQ(uvPerUnit(positions, uvs, indices), 0.5f);
    // Tiled four times each way: uv area 16.
    std::vector<glm::vec2> tiled;
    for (const auto& uv : uvs) tiled.push_back(uv * 4.0f);
    EXPECT_FLOAT_EQ(uvPerUnit(positions, tiled, indices), 2.0f);
    // Unindexed triangles count the same.
    const std::vector<glm::vec3> flat{positions[0], positions[1], positions[2], positions[0], positions[2], positions[3]};
    const std::vector<glm::vec2> flatUvs{uvs[0], uvs[1], uvs[2], uvs[0], uvs[2], uvs[3]};
    EXPECT_FLOAT_EQ(uvPerUnit(flat, flatUvs, {}), 0.5f);
}

TEST(TextureResidency, UvPerUnitIsZeroWithoutUvsOrArea) {
    const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_EQ(uvPerUnit(positions, {}, {}), 0.0f);
    const std::vector<glm::vec2> collapsed{{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};
    EXPECT_EQ(uvPerUnit(positions, collapsed, {}), 0.0f);
}

TEST(TextureResidency, MipCountGoesDownToOneTexel) {
    EXPECT_EQ(mipCount(1, 1), 1u);
    EXPECT_EQ(mipCount(2048, 2048), 12u);
    EXPECT_EQ(mipCount(1024, 256), 11u);
}

TEST(TextureResidency, NeededMipKeepsAtLeastATexelPerPixel) {
    // 2048 texels across the uv range: 2048 pixels per uv unit needs mip 0,
    // 1024 mip 1, 1000 still mip 1 (1024 >= 1000 texels), 512 mip 2.
    EXPECT_EQ(neededMip(2048, 2048, 2048.0f), 0u);
    EXPECT_EQ(neededMip(2048, 2048, 4096.0f), 0u);
    EXPECT_EQ(neededMip(2048, 2048, 1024.0f), 1u);
    EXPECT_EQ(neededMip(2048, 2048, 1000.0f), 1u);
    EXPECT_EQ(neededMip(2048, 2048, 512.0f), 2u);
    // The larger side decides.
    EXPECT_EQ(neededMip(2048, 512, 1024.0f), 1u);
    // No demand, or far less than a texel: the last mip.
    EXPECT_EQ(neededMip(2048, 2048, 0.0f), 11u);
    EXPECT_EQ(neededMip(2048, 2048, 0.001f), 11u);
}

TEST(TextureResidency, ChainBytesCountsTexelsOrBlocks) {
    // RGBA8 4 x 4: 64 + 16 + 4 bytes.
    EXPECT_EQ(chainBytes(VK_FORMAT_R8G8B8A8_SRGB, 4, 4, 0), 84u);
    EXPECT_EQ(chainBytes(VK_FORMAT_R8G8B8A8_SRGB, 4, 4, 1), 20u);
    // BC1 8 x 8: 4 blocks, then 1, 1, 1 (partial blocks round up), 8 bytes each.
    EXPECT_EQ(chainBytes(VK_FORMAT_BC1_RGB_UNORM_BLOCK, 8, 8, 0), 56u);
    // BC7: 16 bytes a block.
    EXPECT_EQ(chainBytes(VK_FORMAT_BC7_SRGB_BLOCK, 8, 8, 0), 112u);
}

TEST(TextureResidency, FitBudgetKeepsNeedsThatFit) {
    const std::vector<Candidate> candidates{{1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, 0, 3},
                                            {512, 512, VK_FORMAT_R8G8B8A8_UNORM, 1, 2}};
    EXPECT_EQ(fitBudget(candidates, 0), (std::vector<uint32_t>{0, 1}));
    EXPECT_EQ(fitBudget(candidates, 1u << 30), (std::vector<uint32_t>{0, 1}));
}

TEST(TextureResidency, FitBudgetGivesUpEvenlyFromTheLeastDegraded) {
    // Two equal textures needing mip 0; a budget for about one full chain.
    const Candidate c{1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, 0, 4};
    const uint64_t full = chainBytes(c.format, c.width, c.height, 0);
    const auto target = fitBudget({c, c}, full);
    // Each gives up one mip rather than one giving up two.
    EXPECT_EQ(target, (std::vector<uint32_t>{1, 1}));
    uint64_t total = 0;
    for (uint32_t t : target) total += chainBytes(c.format, c.width, c.height, t);
    EXPECT_LE(total, full);
}

TEST(TextureResidency, FitBudgetStopsAtTheCoarsestAllowed) {
    const Candidate c{1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, 0, 3};
    EXPECT_EQ(fitBudget({c}, 1), (std::vector<uint32_t>{3}));
    // A need coarser than the coarsest allowed is held at the coarsest.
    const Candidate far{1024, 1024, VK_FORMAT_R8G8B8A8_UNORM, 9, 3};
    EXPECT_EQ(fitBudget({far}, 0), (std::vector<uint32_t>{3}));
}
