//
// FrameGraphMultisampleTest.cpp - multisampled images and resolves
//
// Covers "samples" on images, "resolve" on colour outputs, the scheduling of
// resolve targets, and the compiler's checks on resolves and copies.
//
// Tier 1: pure JSON -> declarations, scheduling and checks, no GPU context.
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

json rgba(json more = json::object()) {
    json desc = {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}};
    desc.update(more);
    return desc;
}

json graph(json resources, json passes) {
    if (!resources.is_array()) resources = json::array({resources});
    if (!passes.is_array()) passes = json::array({passes});
    return {{"version", 1}, {"name", "msaa_test"}, {"resources", resources}, {"passes", passes}};
}

/// Draw into colorMS (4 samples) resolving into color, then read color.
json resolveGraph() {
    json draw = {{"name", "Draw"}, {"type", "graphics"},
                 {"outputs", json::array({{{"resource", "colorMS"}, {"usage", "color_write"},
                                           {"clear", {0, 0, 0, 1}}, {"resolve", "color"}}})}};
    json read = {{"name", "Read"}, {"type", "compute"},
                 {"inputs", json::array({{{"resource", "color"}, {"usage", "shader_read"}}})},
                 {"outputs", json::array({{{"resource", "out"}, {"usage", "storage_image_write"}}})}};
    return graph({image("colorMS", rgba({{"samples", 4}})), image("color", rgba()), image("out", rgba())},
                 {draw, read});
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

PhysicalImage physical(VkFormat format, VkSampleCountFlagBits samples, uint32_t size = 64) {
    PhysicalImage img;
    img.format = format;
    img.extent = {size, size};
    img.samples = samples;
    img.shape = {1, 1};
    img.aspect = formatToAspectMask(format);
    return img;
}

} // namespace

// ── JSON ────────────────────────────────────────────────────────

TEST(MultisampleJson, SamplesParse) {
    auto b = load(graph(image("a", rgba({{"samples", 8}})), json::array()));
    EXPECT_EQ(b.getResourceDeclarations()[0].imageDesc.samples, VK_SAMPLE_COUNT_8_BIT);
}

TEST(MultisampleJson, BadImagesFailAtLoad) {
    const json bad[] = {
        rgba({{"samples", 3}}),
        rgba({{"samples", 0}}),
        rgba({{"samples", 128}}),
        rgba({{"samples", "4"}}),
        rgba({{"samples", 4}, {"mipLevels", 2}}),
        rgba({{"samples", 4}, {"viewType", "cube"}, {"arrayLayers", 6}}),
    };
    for (const auto& desc : bad) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph(image("a", desc), json::array())), std::runtime_error) << desc.dump();
    }
    json withSave = image("a", rgba({{"samples", 4}}));
    withSave["save"] = json::object();
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graph(withSave, json::array())), std::runtime_error);
}

TEST(MultisampleJson, ResolveAddsAWriteOfItsTarget) {
    auto b = load(resolveGraph());
    const auto& outputs = b.getPassDeclarations()[0].outputs;
    ASSERT_EQ(outputs.size(), 2u);
    EXPECT_EQ(outputs[0].resolveTarget.index, b.getResource("color").index);
    EXPECT_EQ(outputs[1].handle.index, b.getResource("color").index);
    EXPECT_EQ(outputs[1].usage, ResourceUsage::ColorResolve);
}

TEST(MultisampleJson, ResolveRoundTripsWithoutAnExtraOutput) {
    auto reloaded = load(saveGraphToJson(load(resolveGraph())));
    const auto& outputs = reloaded.getPassDeclarations()[0].outputs;
    ASSERT_EQ(outputs.size(), 2u);
    EXPECT_TRUE(outputs[0].resolveTarget.valid());
    EXPECT_EQ(outputs[1].usage, ResourceUsage::ColorResolve);
}

TEST(MultisampleJson, BadResolvesFailAtLoad) {
    json onDepth = resolveGraph();
    onDepth["resources"].push_back(image("depth", {{"format", "D32_SFLOAT"}, {"width", 64}, {"height", 64}}));
    onDepth["passes"][0]["outputs"][0] = {{"resource", "depth"}, {"usage", "depth_write"}, {"resolve", "color"}};
    json unknown = resolveGraph();
    unknown["passes"][0]["outputs"][0]["resolve"] = "nowhere";
    json direct = resolveGraph();
    direct["passes"][0]["outputs"][0].erase("resolve");
    direct["passes"][0]["outputs"].push_back({{"resource", "color"}, {"usage", "color_resolve"}});
    for (const auto& j : {onDepth, unknown, direct}) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, j), std::runtime_error);
    }
}

// ── Scheduling ──────────────────────────────────────────────────

TEST(MultisampleSchedule, ReaderOfTheResolveTargetRunsAfterTheResolve) {
    auto b = load(resolveGraph());
    const auto resources = describeResources(b.getResourceDeclarations());
    const auto deps = passDependencies(b.getPassDeclarations(), resources);
    EXPECT_EQ(deps[1], (std::vector<uint32_t>{0}));

    const auto plan = planBarriers(b.getPassDeclarations(), {0, 1}, resources);
    const uint32_t color = b.getResource("color").index;
    bool found = false;
    for (const auto& barrier : plan.preBarriers[0]) {
        if (barrier.resource.index != color) continue;
        found = true;
        EXPECT_EQ(barrier.newLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        EXPECT_EQ(barrier.dstStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT));
        EXPECT_EQ(barrier.dstAccess, static_cast<VkAccessFlags>(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT));
    }
    EXPECT_TRUE(found);
}

// ── Compiler checks ─────────────────────────────────────────────

TEST(MultisampleResolve, TargetMustMatch) {
    const auto ms = physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_4_BIT);
    SubresourceRange range;
    range.baseMip = 0; range.mipCount = 1; range.baseLayer = 0; range.layerCount = 1;

    EXPECT_EQ(resolveTargetError(ms, range, "ms", physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT), "t"),
              "");
    EXPECT_NE(resolveTargetError(physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT), range, "ms",
                                 physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT), "t")
                  .find("not multisampled"), std::string::npos);
    EXPECT_NE(resolveTargetError(ms, range, "ms", physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_4_BIT), "t")
                  .find("is multisampled"), std::string::npos);
    EXPECT_NE(resolveTargetError(ms, range, "ms", physical(VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT), "t")
                  .find("format"), std::string::npos);
    EXPECT_NE(resolveTargetError(ms, range, "ms",
                                 physical(VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, 32), "t")
                  .find("64x64"), std::string::npos);
}

TEST(MultisampleTransfer, CopyNeedsEqualSamplesAndBlitNone) {
    auto passFor = [](const std::string& type) {
        PassDeclaration pass;
        pass.name = "T";
        pass.type = PassType::Transfer;
        pass.execution.type = type;
        ResourceAccess src, dst;
        src.handle.index = 0; src.usage = ResourceUsage::TransferSrc;
        dst.handle.index = 1; dst.usage = ResourceUsage::TransferDst;
        pass.inputs = {src};
        pass.outputs = {dst};
        return pass;
    };
    std::vector<ResourceDeclaration> decls(2);
    decls[0].name = "a";
    decls[1].name = "b";

    auto run = [&](const std::string& type, VkSampleCountFlagBits a, VkSampleCountFlagBits b) {
        std::vector<PhysicalResource> resources;
        resources.emplace_back(physical(VK_FORMAT_R16G16B16A16_SFLOAT, a));
        resources.emplace_back(physical(VK_FORMAT_R16G16B16A16_SFLOAT, b));
        CompiledPass compiled;
        compiled.executionKind = executionKindOf(type);
        std::string error;
        compileTransferRegions(passFor(type), resources, decls, compiled, error);
        return error;
    };
    EXPECT_EQ(run("copy", VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_4_BIT), "");
    EXPECT_NE(run("copy", VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_1_BIT).find("sample count"), std::string::npos);
    EXPECT_NE(run("blit", VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_4_BIT).find("single-sampled"), std::string::npos);
}

TEST(MultisampleFormats, IntegerFormats) {
    EXPECT_TRUE(isIntegerFormat(VK_FORMAT_R32_UINT));
    EXPECT_TRUE(isIntegerFormat(VK_FORMAT_R16G16B16A16_SINT));
    EXPECT_FALSE(isIntegerFormat(VK_FORMAT_R16G16B16A16_SFLOAT));
    EXPECT_FALSE(isIntegerFormat(VK_FORMAT_R8G8B8A8_UNORM));
}
