//
// PipelineDepthBiasTest.cpp - the pipeline JSON's "depthBias" block
//
// Tier 1: pure JSON -> declaration parsing, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;

namespace {

/// One depth-only pass whose "pipeline" block is `pipeline`.
nlohmann::json graphWithPipeline(const nlohmann::json& pipeline) {
    return nlohmann::json{
        {"version", 1},
        {"name", "depth_bias_test"},
        {"resources", {
            {
                {"name", "shadowMap"},
                {"kind", "image"},
                {"image", {{"format", "D32_SFLOAT"}, {"width", 1024}, {"height", 1024}}}
            }
        }},
        {"passes", {
            {
                {"name", "ShadowPass"},
                {"type", "graphics"},
                {"outputs", {{{"resource", "shadowMap"}, {"usage", "depth_write"}}}},
                {"pipeline", pipeline}
            }
        }}
    };
}

const PipelineDesc& onlyPipeline(const FrameGraphBuilder& builder) {
    return builder.getPassDeclarations().at(0).pipelineDesc;
}

} // namespace

TEST(PipelineDepthBias, OffWhenAbsent) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPipeline({{"vertexShader", "shadow.vert.spv"}}));

    const auto& pd = onlyPipeline(builder);
    EXPECT_FALSE(pd.depthBias);
    EXPECT_FLOAT_EQ(pd.depthBiasConstant, 0.0f);
    EXPECT_FLOAT_EQ(pd.depthBiasSlope, 0.0f);
    EXPECT_FLOAT_EQ(pd.depthBiasClamp, 0.0f);
}

TEST(PipelineDepthBias, ParsesAllFields) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPipeline({
        {"vertexShader", "shadow.vert.spv"},
        {"depthBias", {{"constant", 1.25}, {"slope", 1.75}, {"clamp", 0.01}}}
    }));

    const auto& pd = onlyPipeline(builder);
    EXPECT_TRUE(pd.depthBias);
    EXPECT_FLOAT_EQ(pd.depthBiasConstant, 1.25f);
    EXPECT_FLOAT_EQ(pd.depthBiasSlope, 1.75f);
    EXPECT_FLOAT_EQ(pd.depthBiasClamp, 0.01f);
}

TEST(PipelineDepthBias, MissingFieldsDefaultToZero) {
    // An empty block still switches bias on; clamp stays 0 so no device
    // feature is needed.
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPipeline({
        {"vertexShader", "shadow.vert.spv"},
        {"depthBias", {{"slope", 2.0}}}
    }));

    const auto& pd = onlyPipeline(builder);
    EXPECT_TRUE(pd.depthBias);
    EXPECT_FLOAT_EQ(pd.depthBiasConstant, 0.0f);
    EXPECT_FLOAT_EQ(pd.depthBiasSlope, 2.0f);
    EXPECT_FLOAT_EQ(pd.depthBiasClamp, 0.0f);
}

TEST(PipelineDepthBias, RoundTripsThroughSave) {
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWithPipeline({
        {"vertexShader", "shadow.vert.spv"},
        {"depthBias", {{"constant", 0.5}, {"slope", 3.0}}}
    }));

    FrameGraphBuilder reloaded;
    loadGraphFromJson(reloaded, saveGraphToJson(original));

    const auto& pd = onlyPipeline(reloaded);
    EXPECT_TRUE(pd.depthBias);
    EXPECT_FLOAT_EQ(pd.depthBiasConstant, 0.5f);
    EXPECT_FLOAT_EQ(pd.depthBiasSlope, 3.0f);
    EXPECT_FLOAT_EQ(pd.depthBiasClamp, 0.0f);
}

TEST(PipelineDepthBias, SaveOmitsBlockWhenOff) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPipeline({{"vertexShader", "shadow.vert.spv"}}));

    const auto saved = saveGraphToJson(builder);
    EXPECT_FALSE(saved["passes"][0]["pipeline"].contains("depthBias"));
}
