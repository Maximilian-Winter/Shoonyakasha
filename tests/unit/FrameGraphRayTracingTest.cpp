//
// FrameGraphRayTracingTest.cpp - ray tracing passes
//
// Covers the ray_tracing pass type's pipeline JSON, the rules that tie it to
// trace_rays, its capability, the barrier stages of its accesses, and the
// layout of a shader binding table.
//
// Tier 1: pure JSON -> declarations, scheduling and arithmetic, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"
#include "Vulkan/VulkanRayTracingPipeline.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

const json kPipeline = {
    {"rayGenShader", "s/t.rgen.spv"},
    {"missShaders", {"s/sky.rmiss.spv", "s/shadow.rmiss.spv"}},
    {"hitGroups", {{{"closestHit", "s/surface.rchit.spv"}},
                   {{"closestHit", "s/glass.rchit.spv"}, {"anyHit", "s/glass.rahit.spv"}}}},
    {"maxRecursionDepth", 2}};

const json kExecution = {{"type", "trace_rays"},
                         {"dispatch", {{"x", {{"resource", "out"}, {"dimension", "width"}}},
                                       {"y", {{"resource", "out"}, {"dimension", "height"}}}}}};

json graphWith(json pipeline, json execution = kExecution, std::string type = "ray_tracing") {
    json pass = {{"name", "Trace"}, {"type", type},
                 {"inputs", json::array({{{"resource", "src"}, {"usage", "shader_read"}}})},
                 {"outputs", json::array({{{"resource", "out"}, {"usage", "storage_image_write"}}})},
                 {"pipeline", pipeline}, {"execution", execution}};
    json image = {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}};
    return {{"version", 1}, {"name", "rt_test"},
            {"resources", json::array({{{"name", "src"}, {"kind", "image"}, {"image", image}},
                                       {{"name", "out"}, {"kind", "image"}, {"image", image}}})},
            {"passes", json::array({pass})}};
}

FrameGraphBuilder load(const json& j, const PipelineCapabilities& caps = {}) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j, caps);
    return builder;
}

void expectThrows(const json& j, const PipelineCapabilities& caps = {}) {
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, j, caps), std::runtime_error) << j["passes"][0].dump();
}

} // namespace

TEST(RayTracingJson, PassParsesAndRoundTrips) {
    auto b = load(graphWith(kPipeline));
    const auto& pass = b.getPassDeclarations()[0];
    EXPECT_EQ(pass.type, PassType::RayTracing);
    EXPECT_EQ(executionKindOf(pass.execution.type), ExecutionKind::TraceRays);
    const auto& pd = pass.pipelineDesc;
    EXPECT_EQ(pd.rayGenShader, "s/t.rgen.spv");
    ASSERT_EQ(pd.missShaders.size(), 2u);
    ASSERT_EQ(pd.hitGroups.size(), 2u);
    EXPECT_EQ(pd.hitGroups[1].anyHit, "s/glass.rahit.spv");
    EXPECT_EQ(pd.maxRecursionDepth, 2u);

    uint32_t shaders = 0;
    pd.forEachShader([&](const std::string&, VkShaderStageFlagBits) { ++shaders; });
    EXPECT_EQ(shaders, 6u);

    json saved = saveGraphToJson(b);
    saved["passes"][0]["execution"] = kExecution;  // execution is not saved
    const auto reloaded = load(saved);
    const auto& again = reloaded.getPassDeclarations()[0].pipelineDesc;
    EXPECT_EQ(again.missShaders[1], "s/shadow.rmiss.spv");
    EXPECT_EQ(again.hitGroups[1].closestHit, "s/glass.rchit.spv");
    EXPECT_EQ(again.maxRecursionDepth, 2u);
    EXPECT_EQ(JsonUtils::stringToShaderStage("closest_hit"),
              static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
}

TEST(RayTracingJson, BadPassesFailAtLoad) {
    json noRayGen = kPipeline;
    noRayGen.erase("rayGenShader");
    json withFragment = kPipeline;
    withFragment["fragmentShader"] = "s/f.frag.spv";
    json emptyGroup = kPipeline;
    emptyGroup["hitGroups"].push_back(json::object());
    json zeroDepth = kPipeline;
    zeroDepth["maxRecursionDepth"] = 0;
    json missNotArray = kPipeline;
    missNotArray["missShaders"] = "s/sky.rmiss.spv";
    for (const auto& pipeline : {noRayGen, withFragment, emptyGroup, zeroDepth, missNotArray}) {
        expectThrows(graphWith(pipeline));
    }
    // ray_tracing without trace_rays, trace_rays without ray_tracing, and
    // ray tracing keys on another pass type.
    expectThrows(graphWith(kPipeline, {{"type", "compute_dispatch"}}));
    expectThrows(graphWith({{"computeShader", "s/c.comp.spv"}}, kExecution, "compute"));
    expectThrows(graphWith(kPipeline, {{"type", "compute_dispatch"}}, "compute"));
    // A ray_tracing pass with no pipeline block.
    json bare = graphWith(kPipeline);
    bare["passes"][0].erase("pipeline");
    expectThrows(bare);
}

TEST(RayTracingJson, MissingFeatureFailsUnlessRequired) {
    PipelineCapabilities none;
    none.rayTracingPipeline = false;
    expectThrows(graphWith(kPipeline), none);
    json required = graphWith(kPipeline);
    required["passes"][0]["requires"] = {"rayTracingPipeline"};
    EXPECT_TRUE(load(required, none).isUnavailablePass("Trace"));
}

TEST(RayTracingBarriers, AccessesWaitAtTheRayTracingStage) {
    auto b = load(graphWith(kPipeline));
    const auto plan = planBarriers(b.getPassDeclarations(), {0}, describeResources(b.getResourceDeclarations()));
    ASSERT_EQ(plan.preBarriers[0].size(), 2u);
    for (const auto& barrier : plan.preBarriers[0]) {
        EXPECT_EQ(barrier.dstStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR));
    }
    EXPECT_EQ(usageToStageMask(ResourceUsage::StorageImageWrite, PassType::RayTracing),
              static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR));
}

TEST(RayTracingTable, RegionsAreAligned) {
    // 32-byte handles, 32-byte handle alignment, 64-byte base alignment.
    const auto t = layoutShaderBindingTable(32, 32, 64, 2, 3);
    EXPECT_EQ(t.recordSize, 32u);
    EXPECT_EQ(t.rayGenOffset, 0u);
    EXPECT_EQ(t.rayGenSize, 64u);
    EXPECT_EQ(t.missOffset, 64u);
    EXPECT_EQ(t.missSize, 64u);
    EXPECT_EQ(t.hitOffset, 128u);
    EXPECT_EQ(t.hitSize, 128u);  // 3 x 32 = 96, rounded up to 64
    EXPECT_EQ(t.totalSize, 256u);

    // Handles smaller than their alignment take a whole aligned record.
    const auto u = layoutShaderBindingTable(24, 32, 64, 1, 0);
    EXPECT_EQ(u.recordSize, 32u);
    EXPECT_EQ(u.hitSize, 0u);
    EXPECT_EQ(u.totalSize, 128u);
}
