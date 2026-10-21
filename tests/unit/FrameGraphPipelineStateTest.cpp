//
// FrameGraphPipelineStateTest.cpp - pipeline state in pipeline JSON
//
// Covers strict state names, blending and write masks per colour
// attachment, blend constants, and the rasterization keys frontFace,
// lineWidth, primitiveRestart and depthBounds.
//
// Tier 1: pure JSON -> declarations, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

constexpr VkColorComponentFlags kRGBA = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

json image(const std::string& name) {
    return {{"name", name}, {"kind", "image"},
            {"image", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}}}};
}

/// One graphics pass drawing into "hdr" (blended) and "mask", with the given
/// pipeline keys and outputs.
json graphWith(json pipeline, json outputs = nullptr) {
    pipeline["vertexShader"] = "shaders/p.vert.spv";
    if (outputs.is_null()) {
        outputs = json::array({{{"resource", "hdr"}, {"usage", "color_blend"}},
                               {{"resource", "mask"}, {"usage", "color_write"}}});
    }
    json pass = {{"name", "P"}, {"type", "graphics"}, {"outputs", outputs}, {"pipeline", pipeline}};
    return {{"version", 1}, {"name", "state_test"},
            {"resources", json::array({image("hdr"), image("mask"),
                                       {{"name", "depth"}, {"kind", "image"},
                                        {"image", {{"format", "D32_SFLOAT"}, {"width", 64}, {"height", 64}}}}})},
            {"passes", json::array({pass})}};
}

PassDeclaration load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder.getPassDeclarations()[0];
}

void expectThrows(const json& j) {
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, j), std::runtime_error) << j["passes"][0].dump();
}

} // namespace

// ── Strict names ────────────────────────────────────────────────

TEST(PipelineStateJson, UnknownNamesFailAtLoad) {
    for (const json& pipeline : {json{{"cullMode", "backside"}}, json{{"topology", "triangles"}},
                                 json{{"blending", "alpha_blend"}}, json{{"frontFace", "cw"}},
                                 json{{"blending", "custom"}, {"srcColorFactor", "source_alpha"}},
                                 json{{"blending", "custom"}, {"alphaBlendOp", "plus"}}}) {
        expectThrows(graphWith(pipeline));
    }
}

TEST(PipelineStateJson, ConversionsThrowOnUnknownNames) {
    EXPECT_THROW(JsonUtils::stringToCullMode("sideways"), std::runtime_error);
    EXPECT_THROW(JsonUtils::stringToTopology("quads"), std::runtime_error);
    EXPECT_THROW(JsonUtils::stringToBlendFactor("half"), std::runtime_error);
    EXPECT_THROW(JsonUtils::stringToBlendOp("multiply"), std::runtime_error);
    EXPECT_EQ(JsonUtils::stringToBlendFactor("one_minus_constant_alpha"), VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA);
}

// ── Blending per attachment ─────────────────────────────────────

TEST(PipelineStateBlend, OutputsOverrideThePipeline) {
    json outputs = json::array({{{"resource", "hdr"}, {"usage", "color_blend"}},
                                {{"resource", "mask"}, {"usage", "color_write"}, {"blending", "none"},
                                 {"writeMask", "rg"}}});
    const auto pass = load(graphWith({{"blending", "additive"}}, outputs));
    const auto states = colorBlendStates(pass);
    ASSERT_EQ(states.size(), 2u);

    EXPECT_EQ(states[0].blendEnable, VK_TRUE);
    EXPECT_EQ(states[0].srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(states[0].dstColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(states[0].colorWriteMask, kRGBA);

    EXPECT_EQ(states[1].blendEnable, VK_FALSE);
    EXPECT_EQ(states[1].colorWriteMask,
              static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT));
}

TEST(PipelineStateBlend, PipelineWriteMaskAndCustomBlending) {
    json outputs = json::array({{{"resource", "hdr"}, {"usage", "color_blend"}, {"blending", "custom"},
                                 {"srcColorFactor", "constant_color"}, {"colorBlendOp", "max"}},
                                {{"resource", "depth"}, {"usage", "depth_write"}}});
    const auto pass = load(graphWith({{"writeMask", "rgb"}}, outputs));
    const auto states = colorBlendStates(pass);
    ASSERT_EQ(states.size(), 1u);  // the depth output has none
    EXPECT_EQ(states[0].srcColorBlendFactor, VK_BLEND_FACTOR_CONSTANT_COLOR);
    EXPECT_EQ(states[0].dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(states[0].colorBlendOp, VK_BLEND_OP_MAX);
    EXPECT_EQ(states[0].colorWriteMask, static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT |
                                                                           VK_COLOR_COMPONENT_G_BIT |
                                                                           VK_COLOR_COMPONENT_B_BIT));
}

TEST(PipelineStateBlend, BadOutputBlendingFailsAtLoad) {
    const json bad[] = {
        // On a depth output.
        json::array({{{"resource", "depth"}, {"usage", "depth_write"}, {"blending", "none"}}}),
        json::array({{{"resource", "depth"}, {"usage", "depth_write"}, {"writeMask", "r"}}}),
        // Custom keys without "blending".
        json::array({{{"resource", "hdr"}, {"usage", "color_write"}, {"srcColorFactor", "one"}}}),
        // Write masks.
        json::array({{{"resource", "hdr"}, {"usage", "color_write"}, {"writeMask", "rgbx"}}}),
        json::array({{{"resource", "hdr"}, {"usage", "color_write"}, {"writeMask", "rr"}}}),
        json::array({{{"resource", "hdr"}, {"usage", "color_write"}, {"writeMask", 15}}}),
    };
    for (const auto& outputs : bad) expectThrows(graphWith(json::object(), outputs));
}

// ── Constants and rasterization ─────────────────────────────────

TEST(PipelineStateJson, NewKeysParse) {
    auto pd = load(graphWith({{"blendConstants", {0.25, 0.5, 0.75, 1.0}}, {"frontFace", "clockwise"},
                              {"lineWidth", 2.0}, {"primitiveRestart", true}, {"depthBounds", {0.1, 0.9}}}))
                  .pipelineDesc;
    EXPECT_TRUE(pd.hasBlendConstants);
    EXPECT_TRUE(pd.blendConstantsPath.empty());
    EXPECT_FLOAT_EQ(pd.blendConstants[2], 0.75f);
    EXPECT_EQ(pd.frontFace, "clockwise");
    EXPECT_FLOAT_EQ(pd.lineWidth, 2.0f);
    EXPECT_TRUE(pd.primitiveRestart);
    EXPECT_TRUE(pd.depthBoundsTest);
    EXPECT_FLOAT_EQ(pd.minDepthBounds, 0.1f);
    EXPECT_FLOAT_EQ(pd.maxDepthBounds, 0.9f);

    pd = load(graphWith({{"blendConstants", "scene.custom.tint"}})).pipelineDesc;
    EXPECT_EQ(pd.blendConstantsPath, "scene.custom.tint");

    pd = load(graphWith(json::object())).pipelineDesc;
    EXPECT_FALSE(pd.hasBlendConstants);
    EXPECT_EQ(pd.frontFace, "counter_clockwise");
    EXPECT_FALSE(pd.depthBoundsTest);
    EXPECT_EQ(pd.writeMask, kRGBA);
}

TEST(PipelineStateJson, BadValuesFailAtLoad) {
    for (const json& pipeline : {json{{"blendConstants", {1, 2, 3}}}, json{{"blendConstants", ""}},
                                 json{{"lineWidth", 0}}, json{{"depthBounds", {0.9, 0.1}}},
                                 json{{"depthBounds", 0.5}}}) {
        expectThrows(graphWith(pipeline));
    }
}

TEST(PipelineStateJson, RoundTrips) {
    json outputs = json::array({{{"resource", "hdr"}, {"usage", "color_blend"}},
                                {{"resource", "mask"}, {"usage", "color_write"}, {"blending", "custom"},
                                 {"dstColorFactor", "one"}, {"writeMask", "a"}}});
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWith({{"blending", "alpha"}, {"writeMask", "rgb"},
                                           {"blendConstants", "scene.custom.tint"}, {"frontFace", "clockwise"},
                                           {"lineWidth", 3.0}, {"primitiveRestart", true},
                                           {"depthBounds", {0.0, 0.5}}}, outputs));
    const auto pass = load(saveGraphToJson(original));
    const auto& pd = pass.pipelineDesc;
    EXPECT_EQ(pd.blendConstantsPath, "scene.custom.tint");
    EXPECT_EQ(pd.frontFace, "clockwise");
    EXPECT_FLOAT_EQ(pd.lineWidth, 3.0f);
    EXPECT_TRUE(pd.primitiveRestart);
    EXPECT_FLOAT_EQ(pd.maxDepthBounds, 0.5f);
    EXPECT_EQ(pd.writeMask, static_cast<VkColorComponentFlags>(kRGBA & ~VK_COLOR_COMPONENT_A_BIT));
    ASSERT_TRUE(pass.outputs[1].blend.has_value());
    EXPECT_EQ(pass.outputs[1].blend->blending, "custom");
    EXPECT_EQ(pass.outputs[1].blend->dstColorFactor, "one");
    ASSERT_TRUE(pass.outputs[1].writeMask.has_value());
    EXPECT_EQ(*pass.outputs[1].writeMask, static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_A_BIT));
}
