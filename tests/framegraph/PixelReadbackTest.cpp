//
// PixelReadbackTest.cpp - one texel of a graph image back to the CPU, and the
// pipeline JSON "ui" block that names the depth image the canvas UI reads
//
// Tier 1: no GPU. The formats, the barriers around the copy, the layout a
// pass leaves an image in and the queue's frame-in-flight latency.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/PixelReadback.h"

#include <glm/gtc/packing.hpp>
#include <nlohmann/json.hpp>

#include <cstring>

using namespace Shoonyakasha::FrameGraph;

// ─── Formats ────────────────────────────────────────────────────

TEST(PixelReadback, ReadsDepthAndSingleChannelFormats) {
    EXPECT_EQ(pixelReadbackTexelSize(VK_FORMAT_D32_SFLOAT), 4u);
    EXPECT_EQ(pixelReadbackTexelSize(VK_FORMAT_D24_UNORM_S8_UINT), 4u);
    EXPECT_EQ(pixelReadbackTexelSize(VK_FORMAT_D16_UNORM), 2u);
    EXPECT_EQ(pixelReadbackTexelSize(VK_FORMAT_R8_UNORM), 1u);
    EXPECT_EQ(pixelReadbackTexelSize(VK_FORMAT_R8G8B8A8_UNORM), 0u);

    EXPECT_EQ(pixelReadbackAspect(VK_FORMAT_D32_SFLOAT), static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(pixelReadbackAspect(VK_FORMAT_D32_SFLOAT_S8_UINT),
              static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    EXPECT_EQ(pixelReadbackAspect(VK_FORMAT_R32_SFLOAT), static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT));
}

TEST(PixelReadback, DecodesTheFirstComponent) {
    const float f = 0.625f;
    EXPECT_FLOAT_EQ(decodeReadbackTexel(VK_FORMAT_D32_SFLOAT, &f), 0.625f);
    EXPECT_FLOAT_EQ(decodeReadbackTexel(VK_FORMAT_R32_SFLOAT, &f), 0.625f);

    const uint32_t d24 = 0xAB000000u | 0x00FFFFFFu;   // the top byte is not depth
    EXPECT_FLOAT_EQ(decodeReadbackTexel(VK_FORMAT_D24_UNORM_S8_UINT, &d24), 1.0f);

    const uint16_t d16 = 65535 / 5;
    EXPECT_NEAR(decodeReadbackTexel(VK_FORMAT_D16_UNORM, &d16), 0.2f, 1e-4f);

    const uint16_t half = glm::packHalf1x16(-2.5f);
    EXPECT_FLOAT_EQ(decodeReadbackTexel(VK_FORMAT_R16_SFLOAT, &half), -2.5f);

    const uint8_t byte = 51;
    EXPECT_FLOAT_EQ(decodeReadbackTexel(VK_FORMAT_R8_UNORM, &byte), 0.2f);
}

// ─── Layouts and barriers ───────────────────────────────────────

TEST(PixelReadback, LayoutAfterAPassIsItsAttachmentsOrItsPostBarriers) {
    const ResourceHandle depth{3}, colour{4}, swapchain{5}, other{6};
    CompiledPass pass{};
    pass.hasDepthAttachment = true;
    pass.depthAttachment.resource = depth;
    pass.depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    CompiledAttachment c;
    c.resource = colour;
    c.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    pass.colorAttachments.push_back(c);
    c.resource = swapchain;
    pass.colorAttachments.push_back(c);
    BarrierInfo present;
    present.resource = swapchain;
    present.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    pass.postBarriers.push_back(present);

    EXPECT_EQ(layoutAfterPass(pass, depth), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(layoutAfterPass(pass, colour), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(layoutAfterPass(pass, swapchain), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    EXPECT_FALSE(layoutAfterPass(pass, other));
}

TEST(PixelReadback, BarriersTakeTheImageToTheCopyAndBack) {
    const auto image = reinterpret_cast<VkImage>(static_cast<uintptr_t>(0x1234));
    const auto barriers =
        pixelReadbackBarriers(image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

    const auto& toCopy = barriers[0];
    EXPECT_EQ(toCopy.image, image);
    EXPECT_EQ(toCopy.oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(toCopy.newLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_NE(toCopy.srcAccessMask & VK_ACCESS_2_MEMORY_WRITE_BIT, 0u);   // after the pass's writes
    EXPECT_EQ(toCopy.dstStageMask, VK_PIPELINE_STAGE_2_COPY_BIT);
    EXPECT_EQ(toCopy.dstAccessMask, VK_ACCESS_2_TRANSFER_READ_BIT);

    const auto& back = barriers[1];
    EXPECT_EQ(back.oldLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EXPECT_EQ(back.newLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(back.srcStageMask, VK_PIPELINE_STAGE_2_COPY_BIT);
    EXPECT_EQ(back.dstStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);   // before any later pass

    for (const auto& b : barriers) {
        EXPECT_EQ(b.subresourceRange.aspectMask, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
        EXPECT_EQ(b.subresourceRange.baseMipLevel, 0u);
        EXPECT_EQ(b.subresourceRange.levelCount, 1u);
        EXPECT_EQ(b.subresourceRange.baseArrayLayer, 0u);
        EXPECT_EQ(b.subresourceRange.layerCount, 1u);
        EXPECT_EQ(b.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    }
}

// ─── Requests and results ───────────────────────────────────────

TEST(PixelReadback, ResultArrivesWhenTheSameFrameInFlightComesRound) {
    PixelReadbackQueue queue;
    queue.setFramesInFlight(2);
    queue.request({"gDepth", "GBuffer", {10, 20}});
    ASSERT_TRUE(queue.next());

    // Frame 7 records it into frame in flight 1.
    queue.recorded(1, VK_FORMAT_D32_SFLOAT, 7);
    EXPECT_FALSE(queue.next());
    EXPECT_TRUE(queue.awaiting(1));
    EXPECT_FALSE(queue.awaiting(0));

    // Frame 8 uses frame in flight 0: nothing to read yet.
    const float value = 0.75f;
    queue.complete(0, &value);
    EXPECT_FALSE(queue.latest().valid);

    // Frame 9 is back on frame in flight 1, after its fence.
    queue.complete(1, &value);
    EXPECT_FALSE(queue.awaiting(1));
    ASSERT_TRUE(queue.latest().valid);
    EXPECT_FLOAT_EQ(queue.latest().value, 0.75f);
    EXPECT_EQ(queue.latest().frame, 7u);
    EXPECT_EQ(queue.latest().pixel.x, 10);
    EXPECT_EQ(queue.latest().pixel.y, 20);
}

TEST(PixelReadback, ALaterRequestReplacesOneNotYetRecorded) {
    PixelReadbackQueue queue;
    queue.setFramesInFlight(2);
    queue.request({"gDepth", "GBuffer", {1, 1}});
    queue.request({"gDepth", "GBuffer", {5, 6}});
    queue.recorded(0, VK_FORMAT_D32_SFLOAT, 1);
    const float value = 0.5f;
    queue.complete(0, &value);
    EXPECT_EQ(queue.latest().pixel.x, 5);
    EXPECT_EQ(queue.latest().pixel.y, 6);
}

TEST(PixelReadback, AnOlderCopyReadLateDoesNotReplaceANewerOne) {
    PixelReadbackQueue queue;
    queue.setFramesInFlight(2);
    queue.request({"gDepth", "GBuffer", {0, 0}});
    queue.recorded(0, VK_FORMAT_D32_SFLOAT, 1);
    queue.request({"gDepth", "GBuffer", {0, 0}});
    queue.recorded(1, VK_FORMAT_D32_SFLOAT, 2);

    const float newer = 0.2f, older = 0.9f;
    queue.complete(1, &newer);
    queue.complete(0, &older);
    EXPECT_FLOAT_EQ(queue.latest().value, 0.2f);
    EXPECT_EQ(queue.latest().frame, 2u);
}

// ─── The pipeline's "ui" block ──────────────────────────────────

namespace {

nlohmann::json graphWithUI(const nlohmann::json& ui) {
    nlohmann::json graph = {
        {"version", 1},
        {"name", "ui_depth_test"},
        {"resources", {
            {{"name", "swapchain"}, {"kind", "image"}, {"imported", true}},
            {{"name", "depth"}, {"kind", "image"}, {"image", {{"format", "D32_SFLOAT"}}}},
            {{"name", "params"}, {"kind", "buffer"}, {"buffer", {{"size", 64}}}}
        }},
        {"passes", {
            {
                {"name", "Geometry"},
                {"type", "graphics"},
                {"outputs", {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}},
                             {{"resource", "depth"}, {"usage", "depth_write"}}}}
            },
            {
                {"name", "Other"},
                {"type", "graphics"},
                {"outputs", {{{"resource", "swapchain"}, {"usage", "color_blend"}}}}
            }
        }}
    };
    if (!ui.is_null()) graph["ui"] = ui;
    return graph;
}

} // namespace

TEST(PipelineUISettings, LoadsTheDepthImageAndThePassAfterWhichItIsRead) {
    FrameGraphBuilder builder;
    ASSERT_NO_THROW(loadGraphFromJson(builder, graphWithUI({{"depth", "depth"}, {"afterPass", "Geometry"}})));
    EXPECT_EQ(builder.getUISettings().depth, "depth");
    EXPECT_EQ(builder.getUISettings().afterPass, "Geometry");

    // A texel is copied out of it, so it is created to allow that.
    const auto& depth = builder.getResourceDeclarations()[builder.getResource("depth").index];
    EXPECT_NE(depth.imageDesc.additionalUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 0u);
}

TEST(PipelineUISettings, AreEmptyWithoutTheBlock) {
    FrameGraphBuilder builder;
    ASSERT_NO_THROW(loadGraphFromJson(builder, graphWithUI(nullptr)));
    EXPECT_TRUE(builder.getUISettings().depth.empty());
    EXPECT_TRUE(builder.getUISettings().afterPass.empty());
}

TEST(PipelineUISettings, RejectsNamesThatDoNotFit) {
    const nlohmann::json bad[] = {
        {{"depth", "depth"}},                                     // no afterPass
        {{"depth", "missing"}, {"afterPass", "Geometry"}},        // no such resource
        {{"depth", "params"}, {"afterPass", "Geometry"}},         // a buffer
        {{"depth", "depth"}, {"afterPass", "Missing"}},           // no such pass
        {{"depth", "depth"}, {"afterPass", "Other"}},             // does not write it
        "gDepth",
    };
    for (const auto& ui : bad) {
        SCOPED_TRACE(ui.dump());
        FrameGraphBuilder builder;
        EXPECT_THROW(loadGraphFromJson(builder, graphWithUI(ui)), std::runtime_error);
    }
}
