//
// FrameGraphQueueTest.cpp - passes on the graphics and compute queues
//
// Covers which resources both queues use, the barriers of passes recorded
// on the compute queue, and which passes may run there.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json image(const std::string& name) {
    return {{"name", name}, {"kind", "image"},
            {"image", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}}}};
}

json access(const std::string& resource, const std::string& usage) {
    return {{"resource", resource}, {"usage", usage}};
}

json pass(const std::string& name, json inputs, json outputs, const std::string& queue = "graphics") {
    return {{"name", name}, {"type", "compute"}, {"queue", queue}, {"inputs", inputs}, {"outputs", outputs},
            {"execution", {{"type", "compute_dispatch"}}}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

} // namespace

TEST(Queues, SharedAcrossQueuesByAccessesAndDescriptorSets) {
    // A and B on the graphics queue, C on the compute queue. a: A and B.
    // b: B and C. c: C only, but D on the graphics queue binds it.
    json j = {{"version", 1}, {"name", "queue_test"},
              {"resources", json::array({image("a"), image("b"), image("c"), image("d")})},
              {"descriptorSetLayouts", {{"late", {{"bindings", {
                  {{"binding", 0}, {"type", "combined_image_sampler"}, {"name", "c"}, {"autoBindResource", "c"}}}}}}}},
              {"passes", json::array({
                  pass("A", json::array(), json::array({access("a", "storage_image_write")})),
                  pass("B", json::array({access("a", "shader_read")}), json::array({access("b", "storage_image_write")})),
                  pass("C", json::array({access("b", "shader_read")}), json::array({access("c", "storage_image_write")}),
                       "compute"),
                  pass("D", json::array(), json::array({access("d", "storage_image_write")})),
              })}};
    j["passes"][3]["descriptorSets"] = {"late"};
    const auto b = load(j);
    const auto shared = sharedAcrossQueues(b.getPassDeclarations(), b.getResourceDeclarations(),
                                           b.getDescriptorSetLayouts());
    EXPECT_EQ(shared, (std::vector<bool>{false, true, true, false}));
}

TEST(Queues, ComputeQueueBarriersDropGraphicsStages) {
    BarrierInfo afterRendering;
    afterRendering.srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    afterRendering.srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    afterRendering.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    afterRendering.dstAccess = VK_ACCESS_SHADER_READ_BIT;
    BarrierInfo afterCompute = afterRendering;
    afterCompute.srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    afterCompute.srcAccess = VK_ACCESS_SHADER_WRITE_BIT;
    BarrierInfo afterSampling = afterRendering;   // reads by both kinds of pass
    afterSampling.srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    afterSampling.srcAccess = 0;

    std::vector<BarrierInfo> barriers = {afterRendering, afterCompute, afterSampling};
    MemoryBarrierInfo memory;
    memory.srcStage = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    memory.srcAccess = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    memory.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    memory.dstAccess = VK_ACCESS_SHADER_WRITE_BIT;
    fitBarriersToComputeQueue(barriers, memory);

    EXPECT_EQ(barriers[0].srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT});
    EXPECT_EQ(barriers[0].srcAccess, 0u);
    EXPECT_EQ(barriers[0].dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(barriers[0].dstAccess, VkAccessFlags{VK_ACCESS_SHADER_READ_BIT});
    EXPECT_EQ(barriers[1].srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(barriers[1].srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
    EXPECT_EQ(barriers[2].srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT});
    EXPECT_EQ(memory.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT});
    EXPECT_EQ(memory.srcAccess, 0u);
    EXPECT_EQ(memory.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});

    MemoryBarrierInfo none;
    fitBarriersToComputeQueue(barriers, none);
    EXPECT_TRUE(none.empty());
}

TEST(Queues, OnlyNonGraphicsPassesOnTheComputeQueue) {
    json j = {{"version", 1}, {"name", "queue_test"},
              {"resources", json::array({image("a")})},
              {"passes", json::array({pass("A", json::array(), json::array({access("a", "storage_image_write")}),
                                          "compute")})}};
    EXPECT_NO_THROW(load(j));
    j["passes"][0]["type"] = "graphics";
    j["passes"][0]["execution"] = {{"type", "fullscreen"}};
    j["passes"][0]["outputs"] = json::array({access("a", "color_write")});
    try {
        load(j);
        FAIL() << "a graphics pass on the compute queue loaded";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("cannot run on the compute queue"), std::string::npos) << e.what();
    }
}
