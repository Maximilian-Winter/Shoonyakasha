//
// InstanceBatchingTest.cpp - grouping an entity pass's draws into instanced
// draws, and the material hash that decides which ones match
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>

#include "ECS/RenderComponents.h"
#include "FrameGraph/InstanceBatching.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;

TEST(InstanceBatching, EqualKeysJoinTheirFirstDrawsBatch) {
    // Draws 0, 2 and 4 share a mesh; 1 and 3 another; 5 draws alone.
    std::vector<uint32_t> order;
    const auto batches = batchDraws({7, 9, 7, 9, 7, 0}, /*keepOrder=*/false, order);
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(order, (std::vector<uint32_t>{0, 2, 4, 1, 3, 5}));
    EXPECT_EQ(batches[0].first, 0u);
    EXPECT_EQ(batches[0].count, 3u);
    EXPECT_EQ(batches[1].first, 3u);
    EXPECT_EQ(batches[1].count, 2u);
    EXPECT_EQ(batches[2].count, 1u);
}

TEST(InstanceBatching, KeyZeroNeverShares) {
    std::vector<uint32_t> order;
    EXPECT_EQ(batchDraws({0, 0, 0}, false, order).size(), 3u);
    EXPECT_EQ(batchDraws({0, 0, 0}, true, order).size(), 3u);
}

TEST(InstanceBatching, KeepingOrderMergesNeighboursOnly) {
    std::vector<uint32_t> order;
    const auto batches = batchDraws({7, 7, 9, 7}, /*keepOrder=*/true, order);
    EXPECT_EQ(order, (std::vector<uint32_t>{0, 1, 2, 3}));
    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].count, 2u);
    EXPECT_EQ(batches[2].first, 3u);
}

TEST(InstanceBatching, MaterialHashIgnoresParameterOrder) {
    MaterialComponentV5 a, b;
    a.setParam("metallicFactor", 0.5f);
    a.setParam("roughnessFactor", 0.25f);
    b.setParam("roughnessFactor", 0.25f);
    b.setParam("metallicFactor", 0.5f);
    EXPECT_EQ(materialHash(a), materialHash(b));

    b.setParam("roughnessFactor", 0.3f);
    EXPECT_NE(materialHash(a), materialHash(b));
    b.setParam("roughnessFactor", 0.25f);
    b.alphaMode = AlphaMode::Mask;
    EXPECT_NE(materialHash(a), materialHash(b));
}

TEST(InstanceBatching, OnlyEntityPassesTakeInstancing) {
    auto graph = [](const std::string& type) {
        return nlohmann::json{
            {"resources", nlohmann::json::array({{{"name", "out"}, {"kind", "image"},
                                                  {"image", {{"format", "R8G8B8A8_UNORM"}}}}})},
            {"passes", nlohmann::json::array({{{"name", "P"}, {"type", "graphics"},
                                               {"execution", {{"type", type}, {"instancing", true}}},
                                               {"outputs", nlohmann::json::array({{{"resource", "out"},
                                                                                   {"usage", "color_write"}}})}}})}};
    };
    FrameGraph::FrameGraphBuilder ok;
    FrameGraph::loadGraphFromJson(ok, graph("opaque_geometry"));
    EXPECT_TRUE(ok.getPassDeclarations()[0].execution.instancing);

    FrameGraph::FrameGraphBuilder bad;
    EXPECT_THROW(FrameGraph::loadGraphFromJson(bad, graph("fullscreen")), std::runtime_error);
}
