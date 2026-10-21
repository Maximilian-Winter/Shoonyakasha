//
// FrameGraphStencilTest.cpp - the stencil test in pipeline JSON
//
// Covers parsing and saving the "stencil" block, and the compiler's check
// that a pass with a stencil test has an attachment it can run on.
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

json graphWith(json stencil) {
    json pass = {{"name", "P"}, {"type", "graphics"},
                 {"pipeline", {{"vertexShader", "shaders/p.vert.spv"}, {"stencil", std::move(stencil)}}}};
    return {{"version", 1}, {"name", "stencil_test"}, {"resources", json::array()},
            {"passes", json::array({pass})}};
}

PipelineDesc load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder.getPassDeclarations()[0].pipelineDesc;
}

CompiledPass withDepth(VkFormat format, VkImageLayout layout) {
    CompiledPass compiled;
    compiled.hasDepthAttachment = true;
    compiled.depthAttachment.format = format;
    compiled.depthAttachment.layout = layout;
    return compiled;
}

} // namespace

TEST(StencilJson, SharedKeysApplyToBothFaces) {
    const auto pd = load(graphWith({{"compareOp", "equal"}, {"passOp", "replace"}, {"reference", 1},
                                    {"writeMask", 15}}));
    EXPECT_TRUE(pd.stencilTest);
    for (const auto* face : {&pd.stencilFront, &pd.stencilBack}) {
        EXPECT_EQ(face->compareOp, VK_COMPARE_OP_EQUAL);
        EXPECT_EQ(face->passOp, VK_STENCIL_OP_REPLACE);
        EXPECT_EQ(face->failOp, VK_STENCIL_OP_KEEP);
        EXPECT_EQ(face->depthFailOp, VK_STENCIL_OP_KEEP);
        EXPECT_EQ(face->reference, 1u);
        EXPECT_EQ(face->compareMask, 255u);
        EXPECT_EQ(face->writeMask, 15u);
    }
}

TEST(StencilJson, BackOverridesOnlyWhatItNames) {
    const auto pd = load(graphWith({{"passOp", "increment_and_wrap"}, {"reference", 3},
                                    {"back", {{"passOp", "decrement_and_wrap"}}}}));
    EXPECT_EQ(pd.stencilFront.passOp, VK_STENCIL_OP_INCREMENT_AND_WRAP);
    EXPECT_EQ(pd.stencilBack.passOp, VK_STENCIL_OP_DECREMENT_AND_WRAP);
    EXPECT_EQ(pd.stencilBack.reference, 3u);
}

TEST(StencilJson, AbsentMeansOff) {
    json j = graphWith(json::object());
    j["passes"][0]["pipeline"].erase("stencil");
    EXPECT_FALSE(load(j).stencilTest);
}

TEST(StencilJson, BadValuesFailAtLoad) {
    const json bad[] = {
        {{"passOp", "replace_all"}},
        {{"compareOp", "sometimes"}},
        {{"reference", 256}},
        {{"writeMask", -1}},
        {{"compareMask", 1.5}},
        json::array(),
    };
    for (const auto& stencil : bad) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graphWith(stencil)), std::runtime_error) << stencil.dump();
    }
}

TEST(StencilJson, RoundTrips) {
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWith({{"compareOp", "not_equal"}, {"failOp", "invert"}, {"reference", 7},
                                           {"back", {{"depthFailOp", "zero"}}}}));
    const auto pd = load(saveGraphToJson(original));
    EXPECT_TRUE(pd.stencilTest);
    EXPECT_EQ(pd.stencilFront.compareOp, VK_COMPARE_OP_NOT_EQUAL);
    EXPECT_EQ(pd.stencilFront.failOp, VK_STENCIL_OP_INVERT);
    EXPECT_EQ(pd.stencilFront.depthFailOp, VK_STENCIL_OP_KEEP);
    EXPECT_EQ(pd.stencilBack.depthFailOp, VK_STENCIL_OP_ZERO);
    EXPECT_EQ(pd.stencilBack.reference, 7u);
}

TEST(StencilAttachment, NeedsAStencilFormat) {
    PassDeclaration pass;
    pass.name = "P";
    pass.pipelineDesc.stencilTest = true;

    CompiledPass none;
    EXPECT_NE(stencilAttachmentError(pass, none).find("stencil format"), std::string::npos);
    EXPECT_NE(stencilAttachmentError(pass, withDepth(VK_FORMAT_D32_SFLOAT,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)).find("stencil format"), std::string::npos);
    EXPECT_EQ(stencilAttachmentError(pass, withDepth(VK_FORMAT_D32_SFLOAT_S8_UINT,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)), "");

    // No stencil test, no requirement.
    pass.pipelineDesc.stencilTest = false;
    EXPECT_EQ(stencilAttachmentError(pass, none), "");
}

TEST(StencilAttachment, WritingNeedsAWritableAttachment) {
    PassDeclaration pass;
    pass.name = "P";
    pass.pipelineDesc.stencilTest = true;
    const auto readOnly = withDepth(VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    // Testing only is fine on depth_read.
    pass.pipelineDesc.stencilFront.compareOp = VK_COMPARE_OP_EQUAL;
    pass.pipelineDesc.stencilBack = pass.pipelineDesc.stencilFront;
    EXPECT_EQ(stencilAttachmentError(pass, readOnly), "");

    // So is an operation whose write mask is 0.
    pass.pipelineDesc.stencilBack.passOp = VK_STENCIL_OP_REPLACE;
    pass.pipelineDesc.stencilBack.writeMask = 0;
    EXPECT_EQ(stencilAttachmentError(pass, readOnly), "");

    pass.pipelineDesc.stencilBack.writeMask = 0xFF;
    EXPECT_NE(stencilAttachmentError(pass, readOnly).find("depth_write"), std::string::npos);
}

TEST(StencilJson, StencilOpNamesRoundTrip) {
    for (const char* name : {"keep", "zero", "replace", "increment_and_clamp", "decrement_and_clamp",
                             "invert", "increment_and_wrap", "decrement_and_wrap"}) {
        EXPECT_EQ(JsonUtils::stencilOpToString(JsonUtils::stringToStencilOp(name)), name);
    }
}
