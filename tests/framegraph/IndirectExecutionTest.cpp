//
// IndirectExecutionTest.cpp - the "dispatch_indirect" and "draw_indirect"
// execution types
//
// Their arguments are in a storage buffer an earlier pass writes. Covers the
// JSON they are declared with and its errors, the device capabilities they
// need, the barrier and the order that make the earlier pass's writes
// visible, and the check that the buffer holds the arguments.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <nlohmann/json.hpp>

#include <algorithm>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json storage(uint32_t binding, const std::string& buffer) {
    return {{"binding", binding}, {"type", "storage_buffer"}, {"stages", {"compute"}}, {"autoBindBuffer", buffer}};
}

/// A compute pass writing `buffer` through set "writeSet".
json writer(const std::string& name, bool sideEffects = true) {
    return {{"name", name}, {"type", "compute"}, {"hasSideEffects", sideEffects},
            {"execution", {{"type", "compute_dispatch"}, {"dispatch", {{"x", 1}, {"y", 1}, {"z", 1}}}}},
            {"descriptorSets", {"writeSet"}}};
}

json indirectDispatch(const std::string& name, json arguments, bool sideEffects = true) {
    return {{"name", name}, {"type", "compute"}, {"hasSideEffects", sideEffects},
            {"execution", {{"type", "dispatch_indirect"}, {"arguments", std::move(arguments)}}}};
}

json indirectDraw(const std::string& name, json execution) {
    execution["type"] = "draw_indirect";
    return {{"name", name}, {"type", "graphics"}, {"execution", std::move(execution)},
            {"outputs", {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}}}};
}

json graph(json passes) {
    // {pass} is the pass itself to nlohmann, not an array holding it.
    if (passes.is_object()) passes = json::array({passes});
    return {{"version", 1}, {"name", "indirect_test"},
            {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
            {"descriptorSetLayouts", {{"writeSet", {{"bindings", {storage(0, "args")}}}}}},
            {"passes", std::move(passes)}};
}

const PassDeclaration& pass(const FrameGraphBuilder& b, const std::string& name) {
    for (const auto& p : b.getPassDeclarations())
        if (p.name == name) return p;
    throw std::runtime_error("no pass " + name);
}

uint32_t passIndex(const FrameGraphBuilder& b, const std::string& name) {
    const auto& passes = b.getPassDeclarations();
    for (uint32_t i = 0; i < passes.size(); ++i)
        if (passes[i].name == name) return i;
    ADD_FAILURE() << "no pass " << name;
    return 0;
}

void load(FrameGraphBuilder& builder, const json& j, PipelineCapabilities capabilities = {}) {
    loadGraphFromJson(builder, j, capabilities);
}

} // namespace

// ── Execution kinds ─────────────────────────────────────────────

TEST(IndirectExecution, TypesHaveTheirOwnKinds) {
    EXPECT_EQ(executionKindOf("dispatch_indirect"), ExecutionKind::DispatchIndirect);
    EXPECT_EQ(executionKindOf("draw_indirect"), ExecutionKind::DrawIndirect);
}

// ── Parsing ─────────────────────────────────────────────────────

TEST(IndirectExecution, DispatchReadsItsArguments) {
    FrameGraphBuilder b;
    load(b, graph({writer("Write"), indirectDispatch("Use", {{"buffer", "args"}, {"offset", 16}})}));
    const auto& exec = pass(b, "Use").execution;
    EXPECT_EQ(exec.argumentBuffer, "args");
    EXPECT_EQ(exec.argumentOffset, 16u);
    EXPECT_TRUE(readsIndirectArguments(pass(b, "Use")));
    EXPECT_FALSE(readsIndirectArguments(pass(b, "Write")));
}

TEST(IndirectExecution, DrawReadsCountAndStride) {
    FrameGraphBuilder b;
    load(b, graph({writer("Write"),
                   indirectDraw("Draw", {{"arguments", {{"buffer", "args"}, {"drawCount", 4}, {"stride", 32}}}})}));
    const auto& exec = pass(b, "Draw").execution;
    EXPECT_EQ(exec.argumentBuffer, "args");
    EXPECT_EQ(exec.argumentOffset, 0u);
    EXPECT_EQ(exec.drawCount, 4u);
    EXPECT_EQ(exec.drawStride, 32u);
    EXPECT_TRUE(exec.countBuffer.empty());
}

TEST(IndirectExecution, DrawWithCountBuffer) {
    FrameGraphBuilder b;
    load(b, graph({writer("Write"),
                   indirectDraw("Draw", {{"arguments", {{"buffer", "args"}, {"offset", 16}}},
                                         {"count", {{"buffer", "args"}, {"maxDrawCount", 64}}}})}));
    const auto& exec = pass(b, "Draw").execution;
    EXPECT_EQ(exec.countBuffer, "args");
    EXPECT_EQ(exec.countOffset, 0u);
    EXPECT_EQ(exec.maxDrawCount, 64u);
    EXPECT_EQ(exec.drawStride, 16u);
}

TEST(IndirectExecution, ErrorsAreCaughtAtLoad) {
    const json bad[] = {
        // no arguments
        graph({{{"name", "P"}, {"type", "compute"}, {"execution", {{"type", "dispatch_indirect"}}}}}),
        // no buffer named
        graph({indirectDispatch("P", {{"offset", 0}})}),
        // offset not a multiple of 4
        graph({indirectDispatch("P", {{"buffer", "args"}, {"offset", 6}})}),
        // dispatch on a graphics pass
        graph({{{"name", "P"}, {"type", "graphics"},
                {"execution", {{"type", "dispatch_indirect"}, {"arguments", {{"buffer", "args"}}}}}}}),
        // draw on a compute pass
        graph({{{"name", "P"}, {"type", "compute"},
                {"execution", {{"type", "draw_indirect"}, {"arguments", {{"buffer", "args"}}}}}}}),
        // a dispatch takes no draw count
        graph({indirectDispatch("P", {{"buffer", "args"}, {"drawCount", 2}})}),
        // stride smaller than a VkDrawIndirectCommand
        graph({indirectDraw("P", {{"arguments", {{"buffer", "args"}, {"drawCount", 2}, {"stride", 12}}}})}),
        // no draws
        graph({indirectDraw("P", {{"arguments", {{"buffer", "args"}, {"drawCount", 0}}}})}),
        // a count buffer without maxDrawCount
        graph({indirectDraw("P", {{"arguments", {{"buffer", "args"}}}, {"count", {{"buffer", "args"}}}})}),
        // both a count buffer and drawCount
        graph({indirectDraw("P", {{"arguments", {{"buffer", "args"}, {"drawCount", 2}}},
                                  {"count", {{"buffer", "args"}, {"maxDrawCount", 2}}}})}),
        // arguments on another execution type
        graph({{{"name", "P"}, {"type", "compute"},
                {"execution", {{"type", "compute_dispatch"}, {"arguments", {{"buffer", "args"}}}}}}}),
    };
    for (const auto& j : bad) {
        FrameGraphBuilder b;
        EXPECT_THROW(load(b, j), std::runtime_error) << j["passes"].dump();
    }
}

// ── Device capabilities ─────────────────────────────────────────

TEST(IndirectExecution, CountBufferNeedsDrawIndirectCount) {
    PipelineCapabilities without;
    without.drawIndirectCount = false;
    const json j = graph({indirectDraw("Draw", {{"arguments", {{"buffer", "args"}}},
                                                {"count", {{"buffer", "args"}, {"maxDrawCount", 1}}}})});
    FrameGraphBuilder b;
    EXPECT_THROW(load(b, j, without), std::runtime_error);
}

TEST(IndirectExecution, RequiresDrawIndirectCountLeavesThePassOut) {
    PipelineCapabilities without;
    without.drawIndirectCount = false;
    json draw = indirectDraw("Draw", {{"arguments", {{"buffer", "args"}}},
                                      {"count", {{"buffer", "args"}, {"maxDrawCount", 8}}}});
    draw["requires"] = {"drawIndirectCount"};
    FrameGraphBuilder b;
    ASSERT_NO_THROW(load(b, graph({writer("Write"), draw}), without));
    const auto& passes = b.getPassDeclarations();
    EXPECT_TRUE(std::none_of(passes.begin(), passes.end(), [](const auto& p) { return p.name == "Draw"; }));

    FrameGraphBuilder with;
    ASSERT_NO_THROW(load(with, graph({writer("Write"), draw})));
    EXPECT_EQ(pass(with, "Draw").execution.maxDrawCount, 8u);
}

TEST(IndirectExecution, SeveralDrawsNeedMultiDrawIndirect) {
    PipelineCapabilities without;
    without.multiDrawIndirect = false;
    FrameGraphBuilder one;
    EXPECT_NO_THROW(load(one, graph({indirectDraw("Draw", {{"arguments", {{"buffer", "args"}}}})}), without));
    FrameGraphBuilder several;
    EXPECT_THROW(load(several, graph({indirectDraw("Draw", {{"arguments", {{"buffer", "args"}, {"drawCount", 2}}}})}),
                      without),
                 std::runtime_error);
}

// ── Barriers and order ──────────────────────────────────────────

TEST(IndirectExecution, ArgumentsWrittenByComputeGetAnIndirectBarrier) {
    FrameGraphBuilder b;
    load(b, graph({writer("Write"), indirectDispatch("Use", {{"buffer", "args"}})}));
    const auto& passes = b.getPassDeclarations();
    const auto& decls = b.getResourceDeclarations();
    const auto resources = describeResources(decls);
    auto deps = passDependencies(passes, resources);
    const auto table = describeBufferAccesses(passes, decls, b.getDescriptorSetLayouts());
    addIndirectArgumentDependencies(passes, table, deps);
    std::vector<uint32_t> order;
    std::string err;
    ASSERT_TRUE(sortPasses(passes, deps, order, err)) << err;
    const auto memory = planBufferBarriers(order, table);

    const auto& barrier = memory[passIndex(b, "Use")];
    EXPECT_TRUE(barrier.srcStage & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    EXPECT_EQ(barrier.srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
    EXPECT_EQ(barrier.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT});
    EXPECT_EQ(barrier.dstAccess, VkAccessFlags{VK_ACCESS_INDIRECT_COMMAND_READ_BIT});
}

TEST(IndirectExecution, DependsOnTheLastEarlierWriterOfItsArguments) {
    FrameGraphBuilder b;
    load(b, graph({writer("First"), writer("Second"),
                   indirectDraw("Draw", {{"arguments", {{"buffer", "args"}}}}), writer("Later")}));
    const auto& passes = b.getPassDeclarations();
    const auto& decls = b.getResourceDeclarations();
    auto deps = passDependencies(passes, describeResources(decls));
    addIndirectArgumentDependencies(passes, describeBufferAccesses(passes, decls, b.getDescriptorSetLayouts()), deps);
    EXPECT_EQ(deps[passIndex(b, "Draw")], std::vector<uint32_t>{passIndex(b, "Second")});
}

TEST(IndirectExecution, WriterOfArgumentsIsNotCulled) {
    // Neither the writer nor the indirect dispatch has side effects of its
    // own; the draw that presents depends on the dispatch through an image.
    json use = indirectDispatch("Use", {{"buffer", "args"}}, false);
    use["outputs"] = {{{"resource", "result"}, {"usage", "storage_image_write"}}};
    json present = {{"name", "Present"}, {"type", "graphics"}, {"execution", {{"type", "fullscreen"}}},
                    {"inputs", {{{"resource", "result"}, {"usage", "shader_read"}}}},
                    {"outputs", {{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}}}};
    json j = graph({writer("Write", false), use, present});
    j["resources"].push_back({{"name", "result"}, {"kind", "image"}, {"image", {{"format", "R8G8B8A8_UNORM"}}}});

    FrameGraphBuilder b;
    load(b, j);
    const auto& passes = b.getPassDeclarations();
    const auto& decls = b.getResourceDeclarations();
    auto deps = passDependencies(passes, describeResources(decls));
    const auto before = livePasses(passes, decls, deps);
    EXPECT_EQ(std::count(before.begin(), before.end(), passIndex(b, "Write")), 0);

    addIndirectArgumentDependencies(passes, describeBufferAccesses(passes, decls, b.getDescriptorSetLayouts()), deps);
    const auto after = livePasses(passes, decls, deps);
    EXPECT_EQ(std::count(after.begin(), after.end(), passIndex(b, "Write")), 1);
}

// ── Argument buffer sizes ───────────────────────────────────────

namespace {

struct Layouts {
    std::vector<BufferLayoutDesc> descs;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> compiled;

    void add(const std::string& name, BufferUsageType usage, uint32_t elementSize, uint32_t elementCount) {
        BufferLayoutDesc d;
        d.name = name;
        d.elementCount = elementCount;
        descs.push_back(d);
        FrameGraph::CompiledBufferLayout c;
        c.name = name;
        c.usage = usage;
        c.totalSize = elementSize;
        compiled[name] = c;
    }
};

std::string sizeError(const json& passJson, const Layouts& layouts) {
    FrameGraphBuilder b;
    loadGraphFromJson(b, graph({passJson}));
    return indirectArgumentsError(b.getPassDeclarations(), layouts.descs, layouts.compiled);
}

} // namespace

TEST(IndirectExecution, ArgumentBufferMustHoldTheArguments) {
    Layouts layouts;
    layouts.add("args", BufferUsageType::StorageBuffer, 4, 4);       // 16 bytes
    layouts.add("uniform", BufferUsageType::UniformBuffer, 16, 1);

    EXPECT_EQ(sizeError(indirectDispatch("P", {{"buffer", "args"}, {"offset", 4}}), layouts), "");
    EXPECT_NE(sizeError(indirectDispatch("P", {{"buffer", "args"}, {"offset", 8}}), layouts), "");
    EXPECT_NE(sizeError(indirectDispatch("P", {{"buffer", "uniform"}}), layouts), "");
    // Not declared in the pipeline: registered by the application.
    EXPECT_EQ(sizeError(indirectDispatch("P", {{"buffer", "external"}}), layouts), "");

    EXPECT_EQ(sizeError(indirectDraw("P", {{"arguments", {{"buffer", "args"}}}}), layouts), "");
    EXPECT_NE(sizeError(indirectDraw("P", {{"arguments", {{"buffer", "args"}, {"drawCount", 2}}}}), layouts), "");
    // The last of maxDrawCount draws must fit, and so must the count.
    layouts.add("many", BufferUsageType::StorageBuffer, 16, 8);      // 128 bytes
    EXPECT_EQ(sizeError(indirectDraw("P", {{"arguments", {{"buffer", "many"}, {"stride", 16}}},
                                           {"count", {{"buffer", "args"}, {"offset", 12}, {"maxDrawCount", 8}}}}),
                        layouts), "");
    EXPECT_NE(sizeError(indirectDraw("P", {{"arguments", {{"buffer", "many"}, {"stride", 32}}},
                                           {"count", {{"buffer", "args"}, {"maxDrawCount", 8}}}}),
                        layouts), "");
    EXPECT_NE(sizeError(indirectDraw("P", {{"arguments", {{"buffer", "many"}}},
                                           {"count", {{"buffer", "args"}, {"offset", 16}, {"maxDrawCount", 1}}}}),
                        layouts), "");
}
