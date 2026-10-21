//
// FrameGraphQueueTest.cpp - passes on the graphics and compute queues
//
// Covers which resources both queues use, the waits and barriers between
// passes on different queues, batching, and which passes may run there.
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

namespace {

/// A (graphics) writes a. B (compute) reads a and writes b. C (graphics)
/// writes c. D (graphics) reads b. E (graphics) reads a.
json crossQueueGraph() {
    return {{"version", 1}, {"name", "queue_test"},
            {"resources", json::array({image("a"), image("b"), image("c")})},
            {"passes", json::array({
                pass("A", json::array(), json::array({access("a", "storage_image_write")})),
                pass("B", json::array({access("a", "shader_read")}), json::array({access("b", "storage_image_write")}),
                     "compute"),
                pass("C", json::array(), json::array({access("c", "storage_image_write")})),
                pass("D", json::array({access("b", "shader_read")}), json::array()),
                pass("E", json::array({access("a", "shader_read")}), json::array()),
            })}};
}

std::vector<QueueType> queuesOf(const FrameGraphBuilder& b) {
    std::vector<QueueType> out;
    for (const auto& pass : b.getPassDeclarations()) out.push_back(pass.queueType);
    return out;
}

const BarrierInfo* barrierFor(const std::vector<BarrierInfo>& barriers, uint32_t resource) {
    for (const auto& barrier : barriers) {
        if (barrier.resource.index == resource) return &barrier;
    }
    return nullptr;
}

} // namespace

TEST(Queues, ImageHazardsAcrossQueuesWait) {
    const auto b = load(crossQueueGraph());
    const auto resources = describeResources(b.getResourceDeclarations());
    const std::vector<uint32_t> order = {0, 1, 2, 3, 4};

    const auto plan = planBarriers(b.getPassDeclarations(), order, resources, queuesOf(b));
    // E reads a after B read it on the other queue: a read after a read, but
    // across queues, so it waits too.
    EXPECT_EQ(plan.crossQueueWait, (std::vector<uint32_t>{kNoPass, 0, kNoPass, 1, 1}));

    const auto* bReadsA = barrierFor(plan.preBarriers[1], 0);
    ASSERT_NE(bReadsA, nullptr);
    EXPECT_EQ(bReadsA->srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT});
    EXPECT_EQ(bReadsA->srcAccess, 0u);
    EXPECT_EQ(bReadsA->dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    ASSERT_NE(barrierFor(plan.preBarriers[4], 0), nullptr);

    // On one queue: no waits, and the barrier waits for A's compute shader.
    const auto single = planBarriers(b.getPassDeclarations(), order, resources);
    EXPECT_TRUE(single.crossQueueWait.empty());
    const auto* sameQueue = barrierFor(single.preBarriers[1], 0);
    ASSERT_NE(sameQueue, nullptr);
    EXPECT_EQ(sameQueue->srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(barrierFor(single.preBarriers[4], 0), nullptr);
}

TEST(Queues, BatchesOverlapWhereNothingWaits) {
    const auto b = load(crossQueueGraph());
    const auto queues = queuesOf(b);
    const auto plan = planBarriers(b.getPassDeclarations(), {0, 1, 2, 3, 4},
                                   describeResources(b.getResourceDeclarations()), queues);
    const auto batches = planQueueBatches({0, 1, 2, 3, 4}, queues, plan.crossQueueWait);
    // [A] signals for B; C runs beside B; D and E wait for B.
    ASSERT_EQ(batches.size(), 4u);
    EXPECT_EQ(batches[0].queue, QueueType::Graphics);
    EXPECT_EQ(batches[0].passes, (std::vector<uint32_t>{0}));
    EXPECT_EQ(batches[0].wait, -1);
    EXPECT_EQ(batches[1].queue, QueueType::Compute);
    EXPECT_EQ(batches[1].passes, (std::vector<uint32_t>{1}));
    EXPECT_EQ(batches[1].wait, 0);
    EXPECT_EQ(batches[2].queue, QueueType::Graphics);
    EXPECT_EQ(batches[2].passes, (std::vector<uint32_t>{2}));
    EXPECT_EQ(batches[2].wait, -1);
    EXPECT_EQ(batches[3].queue, QueueType::Graphics);
    EXPECT_EQ(batches[3].passes, (std::vector<uint32_t>{3, 4}));
    EXPECT_EQ(batches[3].wait, 1);

    // Without waits each queue keeps one batch.
    const auto free = planQueueBatches({0, 1, 2, 3, 4}, queues, {});
    ASSERT_EQ(free.size(), 2u);
    EXPECT_EQ(free[0].queue, QueueType::Compute);
    EXPECT_EQ(free[1].passes, (std::vector<uint32_t>{0, 2, 3, 4}));
}

TEST(Queues, BufferHazardsAcrossQueuesWait) {
    // Buffer 0 is written by pass 0; buffer 1 is only ever read.
    BufferAccessTable table;
    table.buffers = {"written", "readOnly"};
    const BufferAccess write0{0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT};
    const BufferAccess read0{0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    const BufferAccess read1{1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
    table.passes = {{write0}, {read0}, {read1}, {read1}, {read0}};
    const std::vector<QueueType> queues = {QueueType::Graphics, QueueType::Compute, QueueType::Graphics,
                                           QueueType::Compute, QueueType::Graphics};
    std::vector<uint32_t> waits;
    const auto barriers = planBufferBarriers({0, 1, 2, 3, 4}, table, queues, &waits);
    EXPECT_EQ(waits, (std::vector<uint32_t>{kNoPass, 0, kNoPass, kNoPass, 1}));
    EXPECT_EQ(barriers[1].srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_ALL_COMMANDS_BIT});
    EXPECT_EQ(barriers[1].srcAccess, 0u);
    EXPECT_TRUE(barriers[3].empty());
    EXPECT_FALSE(barriers[4].empty());
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
