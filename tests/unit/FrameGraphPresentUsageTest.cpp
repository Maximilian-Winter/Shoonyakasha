//
// FrameGraphPresentUsageTest.cpp - "blend onto the swapchain, then present it"
//
// Tier 1: pure JSON → declaration parsing, no GPU context.
//
// Presentation is a separate boolean on ResourceAccess rather than a
// ResourceUsage value, so an output can be both `color_blend` and presenting.
// The two describe different things: the usage selects the load op and the
// dependency on the previous writer, while `present` selects the final layout.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;

namespace {

/// Minimal graph: one imported swapchain, one pass writing it with `output`.
nlohmann::json graphWithSwapchainOutput(const nlohmann::json& output) {
    return nlohmann::json{
        {"version", 1},
        {"name", "present_test"},
        {"resources", {
            {
                {"name", "swapchain"},
                {"kind", "image"},
                {"imported", true},
                {"image", {{"format", "B8G8R8A8_UNORM"}}}
            }
        }},
        {"passes", {
            {
                {"name", "OnlyPass"},
                {"type", "graphics"},
                {"outputs", {output}}
            }
        }}
    };
}

const ResourceAccess& onlyOutput(const FrameGraphBuilder& builder) {
    return builder.getPassDeclarations().at(0).outputs.at(0);
}

} // namespace

TEST(FrameGraphPresentUsage, BlendAndPresentTogether) {
    // The combination that had no spelling at all.
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "color_blend"}, {"present", true}
    }));

    const auto& output = onlyOutput(builder);
    EXPECT_EQ(ResourceUsage::ColorAttachmentBlend, output.usage)
        << "the blend semantics must survive being marked presentable";
    EXPECT_TRUE(output.present);
    EXPECT_TRUE(leavesPresentable(output));
}

TEST(FrameGraphPresentUsage, PresentIsOptionalAndDefaultsOff) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "color_blend"}
    }));

    const auto& output = onlyOutput(builder);
    EXPECT_EQ(ResourceUsage::ColorAttachmentBlend, output.usage);
    EXPECT_FALSE(output.present);
    EXPECT_FALSE(leavesPresentable(output));
}

TEST(FrameGraphPresentUsage, ExplicitFalseIsHonoured) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "color_write"}, {"present", false}
    }));

    EXPECT_FALSE(leavesPresentable(onlyOutput(builder)));
}

TEST(FrameGraphPresentUsage, LegacyUsagePresentNormalisesToColorWritePlusFlag) {
    // 14 shipped pipelines are written this way and must keep working.
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "present"}
    }));

    const auto& output = onlyOutput(builder);
    EXPECT_EQ(ResourceUsage::ColorAttachmentWrite, output.usage)
        << "a presenting pass still renders into a colour attachment";
    EXPECT_TRUE(output.present);
}

TEST(FrameGraphPresentUsage, LegacyUsagePresentKeepsItsClearValue) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "present"}, {"clear", {0.1f, 0.2f, 0.3f, 1.0f}}
    }));

    const auto& output = onlyOutput(builder);
    EXPECT_TRUE(output.hasClearValue);
    EXPECT_FLOAT_EQ(0.2f, output.clearValue.color.float32[1]);
    EXPECT_TRUE(output.present);
}

TEST(FrameGraphPresentUsage, SurvivesASerializationRoundTrip) {
    // Serialising `present` back into "usage" would turn a blend-and-present
    // into a plain present and lose the LOAD op.
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithSwapchainOutput({
        {"resource", "swapchain"}, {"usage", "color_blend"}, {"present", true}
    }));

    const nlohmann::json roundTripped = saveGraphToJson(builder);

    FrameGraphBuilder reloaded;
    loadGraphFromJson(reloaded, roundTripped);

    const auto& output = onlyOutput(reloaded);
    EXPECT_EQ(ResourceUsage::ColorAttachmentBlend, output.usage);
    EXPECT_TRUE(output.present);
}

TEST(FrameGraphPresentUsage, LeavesPresentableIsTrueForEitherSpelling) {
    ResourceAccess viaFlag;
    viaFlag.usage = ResourceUsage::ColorAttachmentBlend;
    viaFlag.present = true;
    EXPECT_TRUE(leavesPresentable(viaFlag));

    ResourceAccess viaLegacyEnum;
    viaLegacyEnum.usage = ResourceUsage::Present;
    EXPECT_TRUE(leavesPresentable(viaLegacyEnum))
        << "programmatic graphs still use the enum and must not be missed";

    ResourceAccess neither;
    neither.usage = ResourceUsage::ColorAttachmentWrite;
    EXPECT_FALSE(leavesPresentable(neither));
}
