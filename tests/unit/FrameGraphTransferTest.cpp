//
// FrameGraphTransferTest.cpp - copy and blit passes
//
// Covers the JSON checks on copy and blit passes, their barriers, and the
// regions the compiler builds from the images as created.
//
// Tier 1: pure JSON -> declarations, scheduling and regions, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"
#include "GPU/VkFormatUtils.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json image(const std::string& name, json desc) {
    return {{"name", name}, {"kind", "image"}, {"image", std::move(desc)}};
}

json rgba(uint32_t width, uint32_t height, json more = json::object()) {
    json desc = {{"format", "R16G16B16A16_SFLOAT"}, {"width", width}, {"height", height}};
    desc.update(more);
    return desc;
}

json transferPass(const std::string& type, json src, json dst, json exec = json::object()) {
    src["usage"] = "transfer_src";
    dst["usage"] = "transfer_dst";
    exec["type"] = type;
    return {{"name", "Copy"}, {"type", "transfer"}, {"inputs", json::array({src})},
            {"outputs", json::array({dst})}, {"execution", exec}};
}

json graph(json resources, json passes) {
    // A braced list of one json value is that value, not an array of it.
    if (!resources.is_array()) resources = json::array({resources});
    if (!passes.is_array()) passes = json::array({passes});
    return {{"version", 1}, {"name", "transfer_test"}, {"resources", resources}, {"passes", passes}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

/// The graph's images as the compiler would create them, without a device:
/// shape, extent, depth and format from the declarations.
std::vector<PhysicalResource> physical(const FrameGraphBuilder& b) {
    std::vector<PhysicalResource> out;
    for (const auto& decl : b.getResourceDeclarations()) {
        PhysicalImage img;
        const auto& d = decl.imageDesc;
        const uint32_t depth = d.viewType == ImageViewKind::Tex3D ? d.depth : 1u;
        img.format = d.format;
        img.extent = {d.width, d.height};
        img.depth = depth;
        img.shape = {d.mipLevels == 0 ? fullMipChainLength(d.width, d.height, depth) : d.mipLevels, d.arrayLayers};
        img.aspect = formatToAspectMask(d.format);
        out.emplace_back(std::move(img));
    }
    return out;
}

/// Regions of the graph's only pass, or the compiler's error.
struct Regions {
    CompiledPass pass;
    std::string error;
    bool ok = false;
};

Regions regions(const json& j) {
    auto b = load(j);
    const auto resources = physical(b);
    Regions r;
    const auto& decl = b.getPassDeclarations()[0];
    r.pass.executionKind = executionKindOf(decl.execution.type);
    r.ok = compileTransferRegions(decl, resources, b.getResourceDeclarations(), r.pass, r.error);
    return r;
}

} // namespace

// ── JSON ────────────────────────────────────────────────────────

TEST(TransferJson, CopyAndBlitParse) {
    auto b = load(graph({image("a", rgba(64, 64)), image("b", rgba(32, 32))},
                        {transferPass("blit", {{"resource", "a"}}, {{"resource", "b"}}, {{"filter", "nearest"}})}));
    const auto& pass = b.getPassDeclarations()[0];
    EXPECT_EQ(pass.type, PassType::Transfer);
    EXPECT_EQ(pass.execution.filter, "nearest");
    EXPECT_EQ(executionKindOf("copy"), ExecutionKind::Copy);
    EXPECT_EQ(executionKindOf("blit"), ExecutionKind::Blit);
}

TEST(TransferJson, BadShapesFailAtLoad) {
    const json res = json::array({image("a", rgba(64, 64)), image("b", rgba(64, 64))});
    json computeCopy = transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}});
    computeCopy["type"] = "compute";
    json wrongUsage = transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}});
    wrongUsage["inputs"][0]["usage"] = "shader_read";
    json twoOutputs = transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}});
    twoOutputs["outputs"].push_back({{"resource", "a"}, {"usage", "transfer_dst"}});
    const json bad[] = {
        computeCopy,
        wrongUsage,
        twoOutputs,
        transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}}, {{"filter", "linear"}}),
        transferPass("blit", {{"resource", "a"}}, {{"resource", "b"}}, {{"filter", "cubic"}}),
    };
    for (const auto& pass : bad) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph(res, {pass})), std::runtime_error) << pass.dump();
    }
}

// ── Barriers ────────────────────────────────────────────────────

TEST(TransferBarriers, SourceAndDestinationGetTransferLayouts) {
    auto b = load(graph({image("a", rgba(64, 64)), image("b", rgba(64, 64))},
                        {transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}})}));
    const auto resources = describeResources(b.getResourceDeclarations());
    const auto p = planBarriers(b.getPassDeclarations(), {0}, resources);
    ASSERT_EQ(p.preBarriers[0].size(), 2u);
    for (const auto& barrier : p.preBarriers[0]) {
        const bool src = barrier.resource.index == 0;
        EXPECT_EQ(barrier.newLayout, src ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        EXPECT_EQ(barrier.dstStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TRANSFER_BIT));
        EXPECT_EQ(barrier.dstAccess, static_cast<VkAccessFlags>(src ? VK_ACCESS_TRANSFER_READ_BIT
                                                                   : VK_ACCESS_TRANSFER_WRITE_BIT));
    }
}

// ── Regions ─────────────────────────────────────────────────────

TEST(TransferRegions, CopyCoversEveryMipAndLayer) {
    auto r = regions(graph({image("a", rgba(64, 32, {{"mipLevels", 3}, {"arrayLayers", 2}})),
                            image("b", rgba(64, 32, {{"mipLevels", 3}, {"arrayLayers", 2}}))},
                           {transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}})}));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pass.blitRegions.empty());
    ASSERT_EQ(r.pass.copyRegions.size(), 3u);
    const auto& mip2 = r.pass.copyRegions[2];
    EXPECT_EQ(mip2.srcSubresource.mipLevel, 2u);
    EXPECT_EQ(mip2.srcSubresource.layerCount, 2u);
    EXPECT_EQ(mip2.extent.width, 16u);
    EXPECT_EQ(mip2.extent.height, 8u);
    EXPECT_EQ(mip2.extent.depth, 1u);
}

TEST(TransferRegions, CopyOfAVolumeCoversItsDepth) {
    const json vol = {{"viewType", "3d"}, {"depth", 96}};
    auto r = regions(graph({image("a", rgba(32, 18, vol)), image("b", rgba(32, 18, vol))},
                           {transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}})}));
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.pass.copyRegions.size(), 1u);
    EXPECT_EQ(r.pass.copyRegions[0].extent.depth, 96u);
}

TEST(TransferRegions, BlitFromOneMipToTheNext) {
    auto r = regions(graph({image("a", rgba(64, 64, {{"mipLevels", "full"}}))},
                           {transferPass("blit", {{"resource", "a"}, {"mip", 0}}, {{"resource", "a"}, {"mip", 1}})}));
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.pass.blitRegions.size(), 1u);
    const auto& region = r.pass.blitRegions[0];
    EXPECT_EQ(region.srcSubresource.mipLevel, 0u);
    EXPECT_EQ(region.dstSubresource.mipLevel, 1u);
    EXPECT_EQ(region.srcOffsets[1].x, 64);
    EXPECT_EQ(region.dstOffsets[1].x, 32);
    EXPECT_EQ(region.dstOffsets[1].z, 1);
    EXPECT_EQ(r.pass.blitFilter, VK_FILTER_LINEAR);
}

TEST(TransferRegions, MismatchesAreRefused) {
    // A copy between different sizes or formats.
    auto r = regions(graph({image("a", rgba(64, 64)), image("b", rgba(32, 32))},
                           {transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}})}));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("use blit"), std::string::npos) << r.error;

    r = regions(graph({image("a", rgba(64, 64)),
                       image("b", {{"format", "R8G8B8A8_UNORM"}, {"width", 64}, {"height", 64}})},
                      {transferPass("copy", {{"resource", "a"}}, {{"resource", "b"}})}));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("one format"), std::string::npos) << r.error;

    // Different mip counts, even for a blit.
    r = regions(graph({image("a", rgba(64, 64, {{"mipLevels", 2}})), image("b", rgba(64, 64))},
                      {transferPass("blit", {{"resource", "a"}}, {{"resource", "b"}})}));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("mip(s)"), std::string::npos) << r.error;

    // Colour into depth.
    r = regions(graph({image("a", rgba(64, 64)),
                       image("b", {{"format", "D32_SFLOAT"}, {"width", 64}, {"height", 64}})},
                      {transferPass("blit", {{"resource", "a"}}, {{"resource", "b"}})}));
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("aspects"), std::string::npos) << r.error;
}

TEST(TransferRegions, ImportedImagesAreRefused) {
    json j = graph({image("a", rgba(64, 64)),
                    {{"name", "swapchain"}, {"kind", "image"}, {"imported", true},
                     {"image", {{"format", "B8G8R8A8_UNORM"}}}}},
                   {transferPass("blit", {{"resource", "a"}}, {{"resource", "swapchain"}})});
    auto r = regions(j);
    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("imported"), std::string::npos) << r.error;
}
