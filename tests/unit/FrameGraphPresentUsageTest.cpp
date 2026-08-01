//
// FrameGraphPresentUsageTest.cpp - "blend onto the swapchain, then present it"
//
// Tier 1: pure JSON → declaration parsing, no GPU context.
//
// ResourceUsage used to fold presentation in as a value of its own, so an output
// could say `color_blend` or `present` but never both — and the two carry
// different semantics (LOAD and a dependency on the previous writer, versus the
// final layout). A pipeline that composites several blended passes into the
// swapchain had no way to spell its last pass. These tests pin the split.
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
    // Folding present back into "usage" on the way out would silently downgrade a
    // blend-and-present to a plain present, losing the LOAD op.
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
