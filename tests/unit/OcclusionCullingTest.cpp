//
// OcclusionCullingTest.cpp - DepthPyramid's levels and box occlusion test
//
// Tier 1: pure CPU. The view-projection is the identity, so a box's world x
// and y are its clip x and y (-1 left/top, 1 right/bottom) and z its depth.
//

#include <gtest/gtest.h>

#include "FrameGraph/OcclusionCulling.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <vector>

using namespace Shoonyakasha;

namespace {

/// A 16 x 16 depth image of far background (1) with a wall at depth 0.5
/// over the texels [4, 12) in x and y: clip x and y from -0.5 to 0.5.
DepthPyramid wall() {
    std::vector<float> depths(16 * 16, 1.0f);
    for (int y = 4; y < 12; ++y) {
        for (int x = 4; x < 12; ++x) depths[y * 16 + x] = 0.5f;
    }
    DepthPyramid pyramid;
    pyramid.build(depths.data(), 16, 16, glm::mat4(1.0f));
    return pyramid;
}

} // namespace

TEST(DepthPyramid, LevelsHalveDownToOneTexelKeepingTheFarthest) {
    const DepthPyramid pyramid = wall();
    ASSERT_EQ(pyramid.levelCount(), 5u);
    EXPECT_EQ(pyramid.levelSize(1), glm::uvec2(8, 8));
    EXPECT_EQ(pyramid.levelSize(4), glm::uvec2(1, 1));
    EXPECT_FLOAT_EQ(pyramid.at(1, 3, 3), 0.5f);   // inside the wall
    EXPECT_FLOAT_EQ(pyramid.at(4, 0, 0), 1.0f);   // the background is farthest
}

TEST(DepthPyramid, OddSizesCoverTheirLastRowAndColumn) {
    std::vector<float> depths(3 * 3, 0.2f);
    depths[2 * 3 + 2] = 0.9f;                      // the last texel
    DepthPyramid pyramid;
    pyramid.build(depths.data(), 3, 3, glm::mat4(1.0f));
    ASSERT_EQ(pyramid.levelSize(1), glm::uvec2(2, 2));
    EXPECT_FLOAT_EQ(pyramid.at(1, 1, 1), 0.9f);
    EXPECT_FLOAT_EQ(pyramid.at(2, 0, 0), 0.9f);
}

TEST(DepthPyramid, ABoxBehindTheWallIsHidden) {
    const DepthPyramid pyramid = wall();
    EXPECT_TRUE(pyramid.occluded(glm::vec3(-0.1f, -0.1f, 0.7f), glm::vec3(0.1f, 0.1f, 0.8f)));
}

TEST(DepthPyramid, ABoxInFrontOfTheWallIsNot) {
    const DepthPyramid pyramid = wall();
    EXPECT_FALSE(pyramid.occluded(glm::vec3(-0.1f, -0.1f, 0.3f), glm::vec3(0.1f, 0.1f, 0.4f)));
    // Straddling the wall's depth.
    EXPECT_FALSE(pyramid.occluded(glm::vec3(-0.1f, -0.1f, 0.4f), glm::vec3(0.1f, 0.1f, 0.6f)));
}

TEST(DepthPyramid, ABoxReachingPastTheWallsEdgeIsNot) {
    const DepthPyramid pyramid = wall();
    // Behind the wall, but its right side reaches the background.
    EXPECT_FALSE(pyramid.occluded(glm::vec3(0.2f, -0.1f, 0.7f), glm::vec3(0.7f, 0.1f, 0.8f)));
    // Within the wall's area, but the test widens it by a texel past the edge.
    EXPECT_FALSE(pyramid.occluded(glm::vec3(0.40f, -0.1f, 0.7f), glm::vec3(0.49f, 0.1f, 0.8f)));
}

TEST(DepthPyramid, ABoxCrossingTheNearPlaneOrOutsideIsNot) {
    const DepthPyramid pyramid = wall();
    EXPECT_FALSE(pyramid.occluded(glm::vec3(-0.1f, -0.1f, -0.2f), glm::vec3(0.1f, 0.1f, 0.8f)));
    EXPECT_FALSE(pyramid.occluded(glm::vec3(2.0f, 2.0f, 0.7f), glm::vec3(3.0f, 3.0f, 0.8f)));
}

namespace {

nlohmann::json passWithExecution(nlohmann::json execution) {
    return {{"version", 1}, {"name", "occlusion_test"}, {"resources", nlohmann::json::array()},
            {"passes", nlohmann::json::array({{{"name", "P"}, {"type", "graphics"},
                                               {"execution", std::move(execution)}}})}};
}

} // namespace

TEST(OcclusionExecution, GeometryPassesNameTheirOcclusionImage) {
    FrameGraph::FrameGraphBuilder builder;
    FrameGraph::loadGraphFromJson(builder, passWithExecution({{"type", "opaque_geometry"}, {"occlusion", "hiz"}}));
    EXPECT_EQ(builder.getPassDeclarations()[0].execution.occlusion, "hiz");
}

TEST(OcclusionExecution, OtherPassesTakeNoOcclusionImage) {
    FrameGraph::FrameGraphBuilder builder;
    EXPECT_THROW(FrameGraph::loadGraphFromJson(builder, passWithExecution({{"type", "fullscreen"}, {"occlusion", "hiz"}})),
                 std::runtime_error);
}

TEST(DepthPyramid, AnEmptyPyramidHidesNothing) {
    DepthPyramid pyramid;
    EXPECT_TRUE(pyramid.empty());
    EXPECT_FALSE(pyramid.occluded(glm::vec3(0.0f), glm::vec3(0.1f)));
}
