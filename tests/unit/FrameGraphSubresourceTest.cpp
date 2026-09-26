//
// FrameGraphSubresourceTest.cpp - mips, layers and view types in the frame graph
//
// Covers the JSON for image shapes and subresource ranges, and the
// scheduler's per-subresource dependencies, culling, validation and barriers.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json image(const std::string& name, json desc) {
    return {{"name", name}, {"kind", "image"}, {"image", std::move(desc)}};
}

json swapchain() {
    return {{"name", "swapchain"}, {"kind", "image"}, {"imported", true},
            {"image", {{"format", "B8G8R8A8_UNORM"}}}};
}

json pass(const std::string& name, json inputs, json outputs, const std::string& type = "graphics") {
    if (!inputs.is_array()) inputs = json::array({inputs});
    if (!outputs.is_array()) outputs = json::array({outputs});
    return {{"name", name}, {"type", type}, {"inputs", std::move(inputs)}, {"outputs", std::move(outputs)}};
}

json graph(json resources, json passes) {
    // A braced list of one json value is that value, not an array of it.
    if (!resources.is_array()) resources = json::array({resources});
    if (!passes.is_array()) passes = json::array({passes});
    return {{"version", 1}, {"name", "subresource_test"},
            {"resources", std::move(resources)}, {"passes", std::move(passes)}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

/// A shadow map with four cascades, each written by its own pass, then read
/// whole by a lighting pass that presents.
json cascadeGraph() {
    json passes = json::array();
    for (int i = 0; i < 4; ++i) {
        passes.push_back(pass("Cascade" + std::to_string(i), json::array(),
            {{{"resource", "shadow"}, {"usage", "depth_write"}, {"layer", i},
              {"clear", {{"depth", 1.0}}}}}));
    }
    passes.push_back(pass("Lighting",
        {{{"resource", "shadow"}, {"usage", "shader_read"}}},
        {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true},
          {"clear", {0, 0, 0, 1}}}}));
    return graph({image("shadow", {{"format", "D32_SFLOAT"}, {"width", 1024}, {"height", 1024},
                                  {"arrayLayers", 4}}),
                  swapchain()},
                 passes);
}

struct Scheduled {
    FrameGraphBuilder builder;
    std::vector<ScheduledResource> resources;
    std::vector<std::vector<uint32_t>> deps;
    std::vector<uint32_t> order;
};

Scheduled schedule(const json& j) {
    Scheduled s;
    loadGraphFromJson(s.builder, j);
    s.resources = describeResources(s.builder.getResourceDeclarations());
    s.deps = passDependencies(s.builder.getPassDeclarations(), s.resources);
    std::string err;
    EXPECT_TRUE(sortPasses(s.builder.getPassDeclarations(), s.deps, s.order, err)) << err;
    return s;
}

uint32_t passIndex(const FrameGraphBuilder& b, const std::string& name) {
    const auto& passes = b.getPassDeclarations();
    for (uint32_t i = 0; i < passes.size(); ++i)
        if (passes[i].name == name) return i;
    ADD_FAILURE() << "no pass " << name;
    return 0;
}

} // namespace

// ── Shapes ──────────────────────────────────────────────────────

TEST(ImageShape, ResolvesOpenCountsToTheRestOfTheImage) {
    ImageShape shape{5, 6};
    SubresourceRange r;
    r.baseMip = 2;
    r.baseLayer = 4;
    const auto resolved = shape.resolve(r);
    EXPECT_EQ(resolved.mipCount, 3u);
    EXPECT_EQ(resolved.layerCount, 2u);
    EXPECT_TRUE(shape.contains(resolved));
}

TEST(ImageShape, RejectsRangesOutsideTheImage) {
    ImageShape shape{1, 4};
    SubresourceRange r;
    r.baseLayer = 4;
    r.layerCount = 1;
    EXPECT_FALSE(shape.contains(shape.resolve(r)));
    r.baseLayer = 3;
    r.layerCount = 2;
    EXPECT_FALSE(shape.contains(shape.resolve(r)));
}

TEST(ImageShape, FullMipChainLength) {
    EXPECT_EQ(fullMipChainLength(1, 1), 1u);
    EXPECT_EQ(fullMipChainLength(2048, 2048), 12u);
    EXPECT_EQ(fullMipChainLength(1920, 1080), 11u);
}

// ── JSON: images ────────────────────────────────────────────────

TEST(SubresourceJson, ImageShapeKeysParse) {
    auto b = load(graph({image("hiz", {{"format", "R32_SFLOAT"}, {"width", 512}, {"height", 256},
                                      {"mipLevels", "full"}}),
                         image("arr", {{"format", "D32_SFLOAT"}, {"arrayLayers", 4},
                                       {"viewType", "2d_array"}})},
                        {pass("P", json::array(), json::array())}));
    const auto& res = b.getResourceDeclarations();
    EXPECT_EQ(res[0].imageDesc.mipLevels, 0u);  // full chain
    EXPECT_EQ(res[1].imageDesc.arrayLayers, 4u);
    EXPECT_EQ(res[1].imageDesc.viewType, ImageViewKind::Tex2DArray);
}

TEST(SubresourceJson, CubeDefaultsToSixLayers) {
    auto b = load(graph({image("env", {{"format", "D32_SFLOAT"}, {"width", 256}, {"height", 256},
                                      {"viewType", "cube"}})},
                        {pass("P", json::array(), json::array())}));
    EXPECT_EQ(b.getResourceDeclarations()[0].imageDesc.arrayLayers, 6u);
}

TEST(SubresourceJson, InvalidShapesFailAtLoad) {
    const json bad[] = {
        {{"format", "D32_SFLOAT"}, {"viewType", "cube"}, {"arrayLayers", 4}},
        {{"format", "D32_SFLOAT"}, {"viewType", "cube_array"}, {"arrayLayers", 8}},
        {{"format", "D32_SFLOAT"}, {"viewType", "2d"}, {"arrayLayers", 2}},
        {{"format", "D32_SFLOAT"}, {"viewType", "3d"}},
        {{"format", "D32_SFLOAT"}, {"mipLevels", "all"}},
        {{"format", "D32_SFLOAT"}, {"mipLevels", 0}},
        {{"format", "D32_SFLOAT"}, {"arrayLayers", 0}},
    };
    for (const auto& desc : bad) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph({image("x", desc)}, {pass("P", json::array(), json::array())})),
                     std::runtime_error) << desc.dump();
    }
}

TEST(SubresourceJson, ImageShapeRoundTrips) {
    auto original = load(graph({image("hiz", {{"format", "R32_SFLOAT"}, {"mipLevels", "full"}}),
                                image("cube", {{"format", "D32_SFLOAT"}, {"width", 64}, {"height", 64},
                                               {"viewType", "cube_array"}, {"arrayLayers", 12}})},
                               {pass("P", json::array(), json::array())}));
    auto reloaded = load(saveGraphToJson(original));
    const auto& res = reloaded.getResourceDeclarations();
    EXPECT_EQ(res[0].imageDesc.mipLevels, 0u);
    EXPECT_EQ(res[1].imageDesc.viewType, ImageViewKind::CubeArray);
    EXPECT_EQ(res[1].imageDesc.arrayLayers, 12u);
}

// ── JSON: accesses and bindings ─────────────────────────────────

TEST(SubresourceJson, AccessRangesParse) {
    auto b = load(graph({image("img", {{"format", "R16G16B16A16_SFLOAT"}, {"mipLevels", 4}, {"arrayLayers", 6}})},
        {pass("P",
              {{{"resource", "img"}, {"usage", "shader_read"}, {"mips", {1, 2}}, {"layers", {2, 3}}}},
              {{{"resource", "img"}, {"usage", "color_write"}, {"mip", 3}, {"layer", 5}}})}));
    const auto& p = b.getPassDeclarations()[0];
    EXPECT_EQ(p.inputs[0].subresource.baseMip, 1u);
    EXPECT_EQ(p.inputs[0].subresource.mipCount, 2u);
    EXPECT_EQ(p.inputs[0].subresource.baseLayer, 2u);
    EXPECT_EQ(p.inputs[0].subresource.layerCount, 3u);
    EXPECT_EQ(p.outputs[0].subresource.baseMip, 3u);
    EXPECT_EQ(p.outputs[0].subresource.mipCount, 1u);
    EXPECT_EQ(p.outputs[0].subresource.baseLayer, 5u);
    EXPECT_EQ(p.outputs[0].subresource.layerCount, 1u);
}

TEST(SubresourceJson, AccessDefaultsToTheWholeImage) {
    auto b = load(graph({image("img", {{"format", "R8G8B8A8_UNORM"}})},
                        {pass("P", json::array(), {{{"resource", "img"}, {"usage", "color_write"}}})}));
    EXPECT_TRUE(b.getPassDeclarations()[0].outputs[0].subresource.isWholeImage());
}

TEST(SubresourceJson, MalformedRangesFailAtLoad) {
    const json bad[] = {
        {{"resource", "img"}, {"usage", "color_write"}, {"mip", 0}, {"mips", {0, 1}}},
        {{"resource", "img"}, {"usage", "color_write"}, {"layers", {0, 0}}},
        {{"resource", "img"}, {"usage", "color_write"}, {"layers", 2}},
    };
    for (const auto& access : bad) {
        FrameGraphBuilder b;
        // runtime_error is the loader's own diagnosis; nlohmann's type errors
        // are not runtime_errors, so a malformed test graph cannot pass here.
        EXPECT_THROW(loadGraphFromJson(b, graph({image("img", {{"format", "R8G8B8A8_UNORM"}})},
                                                {pass("P", json::array(), {access})})),
                     std::runtime_error)
            << access.dump();
    }
}

TEST(SubresourceJson, AccessRangesRoundTrip) {
    auto original = load(graph({image("img", {{"format", "R8G8B8A8_UNORM"}, {"mipLevels", 4}, {"arrayLayers", 4}})},
        {pass("P",
              {{{"resource", "img"}, {"usage", "shader_read"}, {"mips", {1, 3}}}},
              {{{"resource", "img"}, {"usage", "color_write"}, {"mip", 0}, {"layer", 2}}})}));
    auto reloaded = load(saveGraphToJson(original));
    const auto& p = reloaded.getPassDeclarations()[0];
    EXPECT_EQ(p.inputs[0].subresource, original.getPassDeclarations()[0].inputs[0].subresource);
    EXPECT_EQ(p.outputs[0].subresource, original.getPassDeclarations()[0].outputs[0].subresource);
}

TEST(SubresourceJson, BindingRangesParse) {
    json j = graph({image("hiz", {{"format", "R32_SFLOAT"}, {"mipLevels", 4}})},
                   {pass("P", json::array(), json::array())});
    j["descriptorSetLayouts"] = {{"downsample", {{"bindings", {
        {{"binding", 0}, {"type", "storage_image"}, {"name", "dst"}, {"autoBindResource", "hiz"}, {"mip", 2}}
    }}}}};
    auto b = load(j);
    const auto& binding = b.getDescriptorSetLayouts()[0].bindings[0];
    EXPECT_EQ(binding.autoBindSubresource.baseMip, 2u);
    EXPECT_EQ(binding.autoBindSubresource.mipCount, 1u);
}

// ── Validation ──────────────────────────────────────────────────

namespace {

std::string validate(const json& j) {
    auto b = load(j);
    const auto resources = describeResources(b.getResourceDeclarations());
    std::string err;
    validateSubresources(b.getPassDeclarations(), b.getResourceDeclarations(), resources,
                         b.getDescriptorSetLayouts(), err);
    return err;
}

} // namespace

TEST(SubresourceValidation, CascadeGraphIsValid) {
    EXPECT_EQ(validate(cascadeGraph()), "");
}

TEST(SubresourceValidation, LayerOutsideTheImage) {
    auto err = validate(graph({image("shadow", {{"format", "D32_SFLOAT"}, {"arrayLayers", 4}})},
        {pass("P", json::array(), {{{"resource", "shadow"}, {"usage", "depth_write"}, {"layer", 4}}})}));
    EXPECT_NE(err.find("outside"), std::string::npos) << err;
}

TEST(SubresourceValidation, AttachmentMustNameOneMip) {
    auto err = validate(graph({image("img", {{"format", "R8G8B8A8_UNORM"}, {"mipLevels", 3}})},
        {pass("P", json::array(), {{{"resource", "img"}, {"usage", "color_write"}}})}));
    EXPECT_NE(err.find("one mip level"), std::string::npos) << err;
}

TEST(SubresourceValidation, OneSubresourceInTwoLayoutsInOnePass) {
    auto err = validate(graph({image("img", {{"format", "R8G8B8A8_UNORM"}, {"arrayLayers", 2}})},
        {pass("P", {{{"resource", "img"}, {"usage", "shader_read"}}},
                   {{{"resource", "img"}, {"usage", "color_write"}, {"layer", 1}}})}));
    EXPECT_NE(err.find("two different layouts"), std::string::npos) << err;

    // Disjoint layers are fine: read layer 0, write layer 1.
    err = validate(graph({image("img", {{"format", "R8G8B8A8_UNORM"}, {"arrayLayers", 2}})},
        {pass("P", {{{"resource", "img"}, {"usage", "shader_read"}, {"layer", 0}}},
                   {{{"resource", "img"}, {"usage", "color_write"}, {"layer", 1}}})}));
    EXPECT_EQ(err, "");
}

TEST(SubresourceValidation, BindingRangeOutsideTheImage) {
    json j = graph({image("hiz", {{"format", "R32_SFLOAT"}, {"mipLevels", 2}})},
                   {pass("P", json::array(), json::array())});
    j["descriptorSetLayouts"] = {{"s", {{"bindings", {
        {{"binding", 0}, {"type", "storage_image"}, {"name", "dst"}, {"autoBindResource", "hiz"}, {"mip", 2}}
    }}}}};
    EXPECT_NE(validate(j).find("outside"), std::string::npos);
}

// ── Dependencies, order and culling ─────────────────────────────

TEST(SubresourceSchedule, ReaderOfAllLayersDependsOnEveryLayerWriter) {
    auto s = schedule(cascadeGraph());
    const uint32_t lighting = passIndex(s.builder, "Lighting");
    EXPECT_EQ(s.deps[lighting], (std::vector<uint32_t>{0, 1, 2, 3}));
    // The cascade writers do not depend on each other.
    for (uint32_t i = 0; i < 4; ++i) EXPECT_TRUE(s.deps[i].empty());
}

TEST(SubresourceSchedule, CullingKeepsEveryCascade) {
    auto s = schedule(cascadeGraph());
    const auto live = livePasses(s.builder.getPassDeclarations(), s.builder.getResourceDeclarations(), s.deps);
    EXPECT_EQ(live, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
}

TEST(SubresourceSchedule, ReaderOfOneLayerDependsOnlyOnItsWriter) {
    json j = cascadeGraph();
    j["passes"][4]["inputs"][0]["layer"] = 2;
    auto s = schedule(j);
    EXPECT_EQ(s.deps[4], (std::vector<uint32_t>{2}));
    const auto live = livePasses(s.builder.getPassDeclarations(), s.builder.getResourceDeclarations(), s.deps);
    EXPECT_EQ(live, (std::vector<uint32_t>{2, 4}));
}

TEST(SubresourceSchedule, LaterWholeImageWriteHidesEarlierOnes) {
    json j = graph({image("img", {{"format", "R8G8B8A8_UNORM"}}), swapchain()},
        {pass("A", json::array(), {{{"resource", "img"}, {"usage", "color_write"}, {"clear", {0, 0, 0, 1}}}}),
         pass("B", json::array(), {{{"resource", "img"}, {"usage", "color_write"}, {"clear", {0, 0, 0, 1}}}}),
         pass("C", {{{"resource", "img"}, {"usage", "shader_read"}}},
                   {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})});
    auto s = schedule(j);
    EXPECT_EQ(s.deps[2], (std::vector<uint32_t>{1}));
    const auto live = livePasses(s.builder.getPassDeclarations(), s.builder.getResourceDeclarations(), s.deps);
    EXPECT_EQ(live, (std::vector<uint32_t>{1, 2}));
}

TEST(SubresourceSchedule, LoadingAnAttachmentDependsOnItsPreviousWriter) {
    json j = graph({image("depth", {{"format", "D32_SFLOAT"}}), swapchain()},
        {pass("Prepass", json::array(), {{{"resource", "depth"}, {"usage", "depth_write"}, {"clear", {{"depth", 1.0}}}}}),
         pass("Main", json::array(),
              {{{"resource", "depth"}, {"usage", "depth_write"}},   // no clear: loads the prepass
               {{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})});
    auto s = schedule(j);
    EXPECT_EQ(s.deps[1], (std::vector<uint32_t>{0}));
    const auto live = livePasses(s.builder.getPassDeclarations(), s.builder.getResourceDeclarations(), s.deps);
    EXPECT_EQ(live, (std::vector<uint32_t>{0, 1}));
}

TEST(SubresourceSchedule, MipChainPassesDependOnThePreviousLevel) {
    json passes = json::array();
    passes.push_back(pass("Mip0", json::array(),
        {{{"resource", "hiz"}, {"usage", "storage_image_write"}, {"mip", 0}}}, "compute"));
    for (int m = 1; m < 4; ++m) {
        passes.push_back(pass("Mip" + std::to_string(m),
            {{{"resource", "hiz"}, {"usage", "shader_read"}, {"mip", m - 1}}},
            {{{"resource", "hiz"}, {"usage", "storage_image_write"}, {"mip", m}}}, "compute"));
    }
    passes.push_back(pass("Use", {{{"resource", "hiz"}, {"usage", "shader_read"}}},
                          {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}}));
    auto s = schedule(graph({image("hiz", {{"format", "R32_SFLOAT"}, {"width", 8}, {"height", 8},
                                          {"mipLevels", 4}}), swapchain()}, passes));
    for (uint32_t m = 1; m < 4; ++m) EXPECT_EQ(s.deps[m], (std::vector<uint32_t>{m - 1}));
    EXPECT_EQ(s.deps[4], (std::vector<uint32_t>{0, 1, 2, 3}));
}

TEST(SubresourceSchedule, OrderIsDeclarationOrder) {
    auto s = schedule(cascadeGraph());
    EXPECT_EQ(s.order, (std::vector<uint32_t>{0, 1, 2, 3, 4}));
}

TEST(SubresourceSchedule, FullMipChainCountsFromTheDeclaredSize) {
    auto b = load(graph({image("hiz", {{"format", "R32_SFLOAT"}, {"width", 64}, {"height", 32},
                                      {"mipLevels", "full"}})},
                        {pass("P", json::array(), json::array())}));
    EXPECT_EQ(describeResources(b.getResourceDeclarations())[0].shape.mipLevels, 7u);
    // With the size unknown until compile time, the resolved extent decides.
    EXPECT_EQ(describeResources(b.getResourceDeclarations(), {VkExtent2D{256, 256}})[0].shape.mipLevels, 9u);
}

// ── Barriers ────────────────────────────────────────────────────

namespace {

BarrierPlan plan(const Scheduled& s) {
    return planBarriers(s.builder.getPassDeclarations(), s.order, s.resources);
}

} // namespace

TEST(SubresourceBarriers, EachCascadeTransitionsOnlyItsLayer) {
    auto s = schedule(cascadeGraph());
    auto p = plan(s);
    for (uint32_t i = 0; i < 4; ++i) {
        ASSERT_EQ(p.preBarriers[i].size(), 1u);
        const auto& b = p.preBarriers[i][0];
        EXPECT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
        EXPECT_EQ(b.newLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        EXPECT_EQ(b.range.baseArrayLayer, i);
        EXPECT_EQ(b.range.layerCount, 1u);
        EXPECT_EQ(b.range.aspectMask, static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_DEPTH_BIT));
    }
}

TEST(SubresourceBarriers, ReaderGetsOneMergedBarrierForAllLayers) {
    auto s = schedule(cascadeGraph());
    auto p = plan(s);
    const auto& pre = p.preBarriers[4];
    const auto shadowBarrier = std::find_if(pre.begin(), pre.end(),
        [](const BarrierInfo& b) { return b.resource.index == 0; });
    ASSERT_NE(shadowBarrier, pre.end());
    EXPECT_EQ(std::count_if(pre.begin(), pre.end(), [](const BarrierInfo& b) { return b.resource.index == 0; }), 1);
    EXPECT_EQ(shadowBarrier->oldLayout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(shadowBarrier->newLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(shadowBarrier->range.baseArrayLayer, 0u);
    EXPECT_EQ(shadowBarrier->range.layerCount, 4u);
    EXPECT_EQ(shadowBarrier->srcAccess, static_cast<VkAccessFlags>(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
}

TEST(SubresourceBarriers, PresentingOutputGetsAPostBarrier) {
    auto s = schedule(cascadeGraph());
    auto p = plan(s);
    ASSERT_EQ(p.postBarriers[4].size(), 1u);
    EXPECT_EQ(p.postBarriers[4][0].oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(p.postBarriers[4][0].newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    EXPECT_EQ(p.finalLayouts[1], (std::vector<VkImageLayout>{VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}));
    EXPECT_TRUE(p.warnings.empty());
}

TEST(SubresourceBarriers, UndeclaredSwapchainPresentsFromItsLastWriterWithAWarning) {
    json j = cascadeGraph();
    j["passes"][4]["outputs"][0].erase("present");
    auto s = schedule(j);
    s.resources[1].presentFallback = true;
    auto p = plan(s);
    ASSERT_EQ(p.postBarriers[4].size(), 1u);
    EXPECT_EQ(p.postBarriers[4][0].newLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    ASSERT_EQ(p.warnings.size(), 1u);
    EXPECT_NE(p.warnings[0].find("Lighting"), std::string::npos);
}

TEST(SubresourceBarriers, NeighbouringMipsMergeIntoOneBarrier) {
    json j = graph({image("img", {{"format", "R8G8B8A8_UNORM"}, {"width", 16}, {"height", 16}, {"mipLevels", 4}}),
                    swapchain()},
        {pass("Write", json::array(), {{{"resource", "img"}, {"usage", "storage_image_write"}}}, "compute"),
         pass("Read", {{{"resource", "img"}, {"usage", "shader_read"}}},
                      {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})});
    auto s = schedule(j);
    auto p = plan(s);
    ASSERT_EQ(p.preBarriers[0].size(), 1u);
    EXPECT_EQ(p.preBarriers[0][0].range.levelCount, 4u);
    const auto& read = p.preBarriers[1][0];
    EXPECT_EQ(read.oldLayout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(read.range.baseMipLevel, 0u);
    EXPECT_EQ(read.range.levelCount, 4u);
}

TEST(SubresourceBarriers, WriteAfterWriteInOneLayoutStillGetsABarrier) {
    json j = graph({image("depth", {{"format", "D32_SFLOAT"}}), swapchain()},
        {pass("Prepass", json::array(), {{{"resource", "depth"}, {"usage", "depth_write"}, {"clear", {{"depth", 1.0}}}}}),
         pass("Main", json::array(),
              {{{"resource", "depth"}, {"usage", "depth_write"}},
               {{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})});
    auto s = schedule(j);
    auto p = plan(s);
    const auto& pre = p.preBarriers[1];
    const auto depth = std::find_if(pre.begin(), pre.end(), [](const BarrierInfo& b) { return b.resource.index == 0; });
    ASSERT_NE(depth, pre.end());
    EXPECT_EQ(depth->oldLayout, depth->newLayout);
    EXPECT_EQ(depth->srcAccess, static_cast<VkAccessFlags>(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
}

TEST(SubresourceBarriers, ConsecutiveReadsShareOneBarrierAndTheNextWriterWaitsForBoth) {
    json j = graph({image("img", {{"format", "R8G8B8A8_UNORM"}}), swapchain()},
        {pass("Write", json::array(), {{{"resource", "img"}, {"usage", "color_write"}, {"clear", {0, 0, 0, 1}}}}),
         pass("ReadCompute", {{{"resource", "img"}, {"usage", "shader_read"}}}, json::array(), "compute"),
         pass("ReadFragment", {{{"resource", "img"}, {"usage", "shader_read"}}}, json::array()),
         pass("Rewrite", json::array(), {{{"resource", "img"}, {"usage", "color_write"}, {"clear", {0, 0, 0, 1}}},
                                         {{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})});
    auto s = schedule(j);
    auto p = planBarriers(s.builder.getPassDeclarations(), {0, 1, 2, 3}, s.resources);
    EXPECT_EQ(p.preBarriers[1].size(), 1u);
    EXPECT_TRUE(p.preBarriers[2].empty());

    const auto& pre = p.preBarriers[3];
    const auto img = std::find_if(pre.begin(), pre.end(), [](const BarrierInfo& b) { return b.resource.index == 0; });
    ASSERT_NE(img, pre.end());
    EXPECT_EQ(img->oldLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(img->srcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                                                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
    EXPECT_EQ(img->srcAccess, 0u);  // write-after-read needs only an execution dependency
}

TEST(SubresourceBarriers, FinalLayoutsArePerSubresource) {
    json j = cascadeGraph();
    j["passes"][4]["inputs"][0]["layers"] = {0, 2};   // lighting reads layers 0 and 1 only
    auto s = schedule(j);
    auto p = planBarriers(s.builder.getPassDeclarations(), {0, 1, 2, 3, 4}, s.resources);
    EXPECT_EQ(p.finalLayouts[0], (std::vector<VkImageLayout>{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}));
}

TEST(SubresourceBarriers, PersistentImagesKeepTheirContentsAcrossFrames) {
    // An exposure image: read and rewritten by a compute pass, then sampled.
    auto withPersistence = [](bool persistent) {
        json image = {{"name", "exposure"}, {"kind", "image"},
                      {"image", {{"format", "R32_SFLOAT"}, {"width", 1}, {"height", 1}}}};
        if (persistent) image["image"]["persistent"] = true;
        return graph(json::array({image, {{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}),
                     json::array({
                         pass("Adapt", json::array(), {{{"resource", "exposure"}, {"usage", "shader_read_write"}}}, "compute"),
                         pass("Tonemap", {{{"resource", "exposure"}, {"usage", "shader_read"}}},
                              {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})}));
    };
    auto firstBarrier = [](const BarrierPlan& p) {
        const auto& pre = p.preBarriers[0];
        const auto it = std::find_if(pre.begin(), pre.end(), [](const BarrierInfo& b) { return b.resource.index == 0; });
        EXPECT_NE(it, pre.end());
        return it == pre.end() ? BarrierInfo{} : *it;
    };

    auto transient = schedule(withPersistence(false));
    EXPECT_EQ(firstBarrier(plan(transient)).oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);

    auto persistent = schedule(withPersistence(true));
    EXPECT_TRUE(persistent.resources[0].persistent);
    const auto p = plan(persistent);
    const auto b = firstBarrier(p);
    // From where the previous frame left it, waiting for its reads.
    EXPECT_EQ(b.oldLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    EXPECT_EQ(b.newLayout, VK_IMAGE_LAYOUT_GENERAL);
    EXPECT_EQ(p.finalLayouts[0], (std::vector<VkImageLayout>{VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}));

    const auto& desc = persistent.builder.getResourceDeclarations()[0].imageDesc;
    EXPECT_TRUE(desc.persistent);
    EXPECT_TRUE(desc.additionalUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT);   // cleared at compile time
}

TEST(SubresourceBarriers, UpsamplePassesDeclaredSmallestFirstRunInThatOrder) {
    json up = {{"name", "Up{m}"}, {"type", "graphics"},
               {"repeat", {{"count", 3}, {"index", "m"}, {"first", 2}, {"step", -1}}},
               {"inputs", json::array({{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", "{m+1}"}}})},
               {"outputs", json::array({{{"resource", "chain"}, {"usage", "color_blend"}, {"mip", "{m}"}}})}};
    json down = {{"name", "Down{m}"}, {"type", "graphics"},
                 {"repeat", {{"count", 3}, {"index", "m"}, {"first", 1}}},
                 {"inputs", json::array({{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", "{m-1}"}}})},
                 {"outputs", json::array({{{"resource", "chain"}, {"usage", "color_write"}, {"mip", "{m}"}}})}};
    json j = graph(json::array({{{"name", "chain"}, {"kind", "image"},
                                 {"image", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64},
                                            {"mipLevels", 4}}}},
                                {{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}),
                   json::array({pass("Fill", json::array(), {{{"resource", "chain"}, {"usage", "color_write"}, {"mip", 0}}}),
                                down, up,
                                pass("Show", {{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", 0}}},
                                     {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})}));
    auto s = schedule(j);
    auto position = [&](const std::string& name) {
        return std::find(s.order.begin(), s.order.end(), passIndex(s.builder, name)) - s.order.begin();
    };
    EXPECT_LT(position("Down3"), position("Up2"));
    EXPECT_LT(position("Up2"), position("Up1"));
    EXPECT_LT(position("Up1"), position("Up0"));
    EXPECT_LT(position("Up0"), position("Show"));
}

// ── View types ──────────────────────────────────────────────────

TEST(SubresourceViews, SampledViewTypes) {
    const ImageShape one{1, 1}, four{1, 4}, cube{1, 6}, cubes{1, 12};
    auto whole = [](const ImageShape& s) { return s.resolve(SubresourceRange{}); };
    auto layers = [](uint32_t base, uint32_t count) {
        SubresourceRange r; r.baseMip = 0; r.mipCount = 1; r.baseLayer = base; r.layerCount = count; return r;
    };

    EXPECT_EQ(sampledViewType(ImageViewKind::Auto, one, whole(one)), VK_IMAGE_VIEW_TYPE_2D);
    EXPECT_EQ(sampledViewType(ImageViewKind::Auto, four, whole(four)), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    EXPECT_EQ(sampledViewType(ImageViewKind::Tex2DArray, one, whole(one)), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    EXPECT_EQ(sampledViewType(ImageViewKind::Cube, cube, whole(cube)), VK_IMAGE_VIEW_TYPE_CUBE);
    EXPECT_EQ(sampledViewType(ImageViewKind::CubeArray, cubes, whole(cubes)), VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);

    EXPECT_EQ(sampledViewType(ImageViewKind::Auto, four, layers(2, 1)), VK_IMAGE_VIEW_TYPE_2D);
    EXPECT_EQ(sampledViewType(ImageViewKind::Auto, four, layers(1, 2)), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    EXPECT_EQ(sampledViewType(ImageViewKind::Tex2DArray, four, layers(2, 1)), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
    EXPECT_EQ(sampledViewType(ImageViewKind::CubeArray, cubes, layers(6, 6)), VK_IMAGE_VIEW_TYPE_CUBE);
    EXPECT_EQ(sampledViewType(ImageViewKind::Cube, cube, layers(3, 1)), VK_IMAGE_VIEW_TYPE_2D);
}

TEST(SubresourceViews, AttachmentViewTypes) {
    SubresourceRange r;
    r.baseMip = 0; r.mipCount = 1; r.baseLayer = 3; r.layerCount = 1;
    EXPECT_EQ(attachmentViewType(r), VK_IMAGE_VIEW_TYPE_2D);
    r.layerCount = 6;
    EXPECT_EQ(attachmentViewType(r), VK_IMAGE_VIEW_TYPE_2D_ARRAY);
}
