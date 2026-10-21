//
// TexelBufferTest.cpp - uniform and storage texel buffers
//
// Covers their descriptor binding JSON and its errors, and the buffer
// barriers between a pass writing a storage texel buffer and one reading it
// as a uniform texel buffer.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json binding(const std::string& type, json more = json::object()) {
    json b = {{"binding", 0}, {"type", type}, {"name", "texels"}, {"autoBindBuffer", "Heights"},
              {"stages", {"compute"}}, {"format", "R32_SFLOAT"}};
    b.update(more);
    return b;
}

json compute(const std::string& name, const std::string& set) {
    return {{"name", name}, {"type", "compute"}, {"hasSideEffects", true},
            {"execution", {{"type", "compute_dispatch"}, {"dispatch", {{"x", 1}}}}},
            {"descriptorSets", {set}}};
}

json graph(json layouts, json passes) {
    if (passes.is_object()) passes = json::array({passes});
    return {{"version", 1}, {"name", "texel_test"}, {"descriptorSetLayouts", std::move(layouts)},
            {"passes", std::move(passes)}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder b;
    loadGraphFromJson(b, j);
    return b;
}

} // namespace

TEST(TexelBuffers, BindingsParse) {
    auto b = load(graph({{"writeSet", {{"bindings", {binding("storage_texel_buffer")}}}},
                         {"readSet", {{"bindings", {binding("uniform_texel_buffer", {{"format", "R16G16_SFLOAT"}})}}}}},
                        compute("P", "writeSet")));
    const auto* write = b.getDescriptorSetLayout("writeSet");
    const auto* read = b.getDescriptorSetLayout("readSet");
    ASSERT_TRUE(write && read);
    EXPECT_EQ(write->bindings[0].texelFormat, VK_FORMAT_R32_SFLOAT);
    EXPECT_EQ(read->bindings[0].texelFormat, VK_FORMAT_R16G16_SFLOAT);
    EXPECT_EQ(JsonUtils::stringToDescriptorType("storage_texel_buffer"), VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);
    EXPECT_EQ(JsonUtils::descriptorTypeToString(VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER), "uniform_texel_buffer");
}

TEST(TexelBuffers, BadBindingsFailAtLoad) {
    json noFormat = binding("uniform_texel_buffer");
    noFormat.erase("format");
    json noBuffer = binding("storage_texel_buffer");
    noBuffer.erase("autoBindBuffer");
    const json bad[] = {
        noFormat,
        noBuffer,
        binding("storage_texel_buffer", {{"count", 2}}),
        binding("storage_buffer"),                                   // a format on another type
        binding("uniform_texel_buffer", {{"format", "R32_FLOAT"}}),  // not a format name
    };
    for (const auto& b : bad) {
        FrameGraphBuilder builder;
        EXPECT_THROW(loadGraphFromJson(builder, graph({{"s", {{"bindings", {b}}}}}, compute("P", "s"))),
                     std::runtime_error) << b.dump();
    }
}

TEST(TexelBuffers, WritesReachLaterReads) {
    auto b = load(graph({{"writeSet", {{"bindings", {binding("storage_texel_buffer")}}}},
                         {"readSet", {{"bindings", {binding("uniform_texel_buffer")}}}}},
                        {compute("Write", "writeSet"), compute("Read", "readSet")}));
    const auto& passes = b.getPassDeclarations();
    const auto table = describeBufferAccesses(passes, b.getResourceDeclarations(), b.getDescriptorSetLayouts());
    const auto barriers = planBufferBarriers({0, 1}, table);
    const auto& barrier = barriers[1];
    EXPECT_EQ(barrier.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_TRUE(barrier.srcAccess & VK_ACCESS_SHADER_WRITE_BIT);
    EXPECT_EQ(barrier.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_TRUE(barrier.dstAccess & VK_ACCESS_SHADER_READ_BIT);
}
