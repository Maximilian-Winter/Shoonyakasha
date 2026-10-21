//
// BufferDrawTest.cpp - draws from vertex and index buffers, and counts
//
// Covers "vertexBuffer" and "indexBuffer" on the draw types, "draw_indexed"
// and "draw_indexed_indirect", the order and barrier that make a writer's
// vertices visible, counts from images (vertexCount, dispatch, "layers"),
// and unknown execution types failing at load.
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

/// A compute pass writing the storage buffer "verts".
json writer(const std::string& name) {
    return {{"name", name}, {"type", "compute"}, {"hasSideEffects", true},
            {"execution", {{"type", "compute_dispatch"}, {"dispatch", {{"x", 1}}}}},
            {"descriptorSets", {"writeSet"}}};
}

json draw(const std::string& name, json execution) {
    return {{"name", name}, {"type", "graphics"}, {"execution", std::move(execution)},
            {"outputs", {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}}}};
}

json graph(json passes) {
    if (passes.is_object()) passes = json::array({passes});
    return {{"version", 1}, {"name", "draw_test"},
            {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}},
                           {{"name", "grid"}, {"kind", "image"},
                            {"image", {{"format", "R8G8B8A8_UNORM"}, {"width", 16}, {"height", 8},
                                       {"arrayLayers", 6}}}}}},
            {"descriptorSetLayouts", {{"writeSet", {{"bindings", {{{"binding", 0}, {"type", "storage_buffer"},
                                                                   {"stages", {"compute"}},
                                                                   {"autoBindBuffer", "verts"}}}}}}}},
            {"passes", std::move(passes)}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder b;
    loadGraphFromJson(b, j);
    return b;
}

void expectThrows(const json& j) {
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, j), std::runtime_error) << j["passes"].dump();
}

} // namespace

// ── Execution types ─────────────────────────────────────────────

TEST(BufferDraws, TypesHaveTheirOwnKinds) {
    EXPECT_EQ(executionKindOf("draw_indexed"), ExecutionKind::DrawIndexed);
    EXPECT_EQ(executionKindOf("draw_indexed_indirect"), ExecutionKind::DrawIndexedIndirect);
}

TEST(BufferDraws, UnknownExecutionTypesFailAtLoad) {
    expectThrows(graph(draw("P", {{"type", "draw_sometimes"}})));
}

TEST(BufferDraws, IndexedDrawParses) {
    auto b = load(graph(draw("P", {{"type", "draw_indexed"}, {"indexCount", 36}, {"firstIndex", 6},
                                   {"vertexOffset", -4}, {"instanceCount", 3},
                                   {"vertexBuffer", {{"buffer", "verts"}, {"offset", 16}}},
                                   {"indexBuffer", {{"buffer", "indices"}, {"type", "uint16"}}}})));
    const auto& e = b.getPassDeclarations()[0].execution;
    EXPECT_EQ(e.indexCount.value, 36u);
    EXPECT_EQ(e.firstIndex, 6u);
    EXPECT_EQ(e.vertexOffset, -4);
    EXPECT_EQ(e.instanceCount, 3u);
    EXPECT_EQ(e.vertexBuffer, "verts");
    EXPECT_EQ(e.vertexBufferOffset, 16u);
    EXPECT_EQ(e.indexBuffer, "indices");
    EXPECT_EQ(e.indexType, VK_INDEX_TYPE_UINT16);
}

TEST(BufferDraws, BadBuffersFailAtLoad) {
    const json index = {{"buffer", "indices"}};
    expectThrows(graph(draw("P", {{"type", "draw_indexed"}})));                          // no index buffer
    expectThrows(graph(draw("P", {{"type", "draw"}, {"indexBuffer", index}})));          // index buffer on draw
    expectThrows(graph(draw("P", {{"type", "draw_indexed"}, {"indexBuffer", {{"buffer", "indices"},
                                                                            {"type", "uint8"}}}})));
    expectThrows(graph(draw("P", {{"type", "draw"}, {"vertexBuffer", {{"buffer", "verts"}, {"offset", 2}}}})));
    expectThrows(graph(draw("P", {{"type", "fullscreen"}, {"vertexBuffer", {{"buffer", "verts"}}}})));
    json compute = writer("C");
    compute["execution"]["vertexBuffer"] = {{"buffer", "verts"}};
    expectThrows(graph(compute));
}

TEST(BufferDraws, IndexedIndirectArgumentsAre20Bytes) {
    const json index = {{"buffer", "indices"}};
    auto b = load(graph(draw("P", {{"type", "draw_indexed_indirect"}, {"indexBuffer", index},
                                   {"arguments", {{"buffer", "args"}}}})));
    EXPECT_EQ(b.getPassDeclarations()[0].execution.drawStride, 20u);
    expectThrows(graph(draw("P", {{"type", "draw_indexed_indirect"}, {"indexBuffer", index},
                                  {"arguments", {{"buffer", "args"}, {"stride", 16}}}})));
}

TEST(BufferDraws, WrittenVerticesGetAVertexInputBarrierAndOrder) {
    auto b = load(graph({writer("Write"), draw("Draw", {{"type", "draw"}, {"vertexCount", 3},
                                                        {"vertexBuffer", {{"buffer", "verts"}}}})}));
    const auto& passes = b.getPassDeclarations();
    const auto& decls = b.getResourceDeclarations();
    auto deps = passDependencies(passes, describeResources(decls));
    const auto table = describeBufferAccesses(passes, decls, b.getDescriptorSetLayouts());
    addIndirectArgumentDependencies(passes, table, deps);
    EXPECT_EQ(deps[1], std::vector<uint32_t>{0});

    std::vector<uint32_t> order;
    std::string err;
    ASSERT_TRUE(sortPasses(passes, deps, order, err)) << err;
    const auto& barrier = planBufferBarriers(order, table)[1];
    EXPECT_TRUE(barrier.srcStage & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_VERTEX_INPUT_BIT});
    EXPECT_EQ(barrier.dstAccess, VkAccessFlags{VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT});
}

TEST(BufferDraws, BuffersMustBeLargeStorageBuffers) {
    std::vector<BufferLayoutDesc> descs(2);
    descs[0].name = "args";
    descs[0].elementCount = 1;
    descs[1].name = "params";
    descs[1].elementCount = 1;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> compiled;
    compiled["args"].usage = BufferUsageType::StorageBuffer;
    compiled["args"].totalSize = 16;
    compiled["params"].usage = BufferUsageType::UniformBuffer;
    compiled["params"].totalSize = 16;
    auto error = [&](const json& execution) {
        auto b = load(graph(draw("P", execution)));
        return indirectArgumentsError(b.getPassDeclarations(), descs, compiled);
    };
    const json index = {{"buffer", "args"}};
    // One indexed command is 20 bytes; 16 do not hold it.
    EXPECT_NE(error({{"type", "draw_indexed_indirect"}, {"indexBuffer", index},
                     {"arguments", {{"buffer", "args"}}}}), "");
    EXPECT_NE(error({{"type", "draw"}, {"vertexBuffer", {{"buffer", "params"}}}}), "");
    EXPECT_EQ(error({{"type", "draw"}, {"vertexBuffer", {{"buffer", "args"}}}}), "");
}

// ── Counts from images ──────────────────────────────────────────

TEST(Counts, ResourceDimensionsParse) {
    auto b = load(graph(draw("P", {{"type", "draw"},
                                   {"vertexCount", {{"resource", "grid"}, {"dimension", "layers"}}}})));
    const auto& count = b.getPassDeclarations()[0].execution.vertexCount;
    EXPECT_EQ(count.resource, "grid");
    EXPECT_EQ(count.dimension, "layers");

    json dispatch = writer("C");
    dispatch["execution"]["dispatch"]["z"] = {{"resource", "grid"}, {"dimension", "layers"}, {"divisor", 2}};
    auto c = load(graph(dispatch));
    EXPECT_EQ(c.getPassDeclarations()[0].execution.dispatch[2].divisor, 2u);
}

TEST(Counts, BadCountsFailAtLoad) {
    expectThrows(graph(draw("P", {{"type", "draw"}, {"vertexCount", {{"resource", "grid"}, {"dimension", "area"}}}})));
    expectThrows(graph(draw("P", {{"type", "draw"}, {"vertexCount", {{"resource", "nowhere"},
                                                                     {"dimension", "width"}}}})));
    expectThrows(graph(draw("P", {{"type", "draw"}, {"vertexCount", {{"parameter", "n"}, {"divisor", 0}}}})));
    expectThrows(graph(draw("P", {{"type", "draw"}, {"vertexCount", "three"}})));
}
