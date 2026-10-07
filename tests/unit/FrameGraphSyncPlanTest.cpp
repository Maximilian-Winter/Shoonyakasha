//
// FrameGraphSyncPlanTest.cpp - buffer barriers and attachment load/store ops
//
// Covers what the scheduler decides beyond image barriers: which buffers each
// pass touches, the one memory barrier placed before a pass, and when an
// attachment's load or store can be dropped.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context. The
// shipped pipeline tests also reflect committed SPIR-V.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"
#include "FrameGraph/ShaderInterfaceValidator.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

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

json present() {
    return {{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}, {"clear", {0, 0, 0, 1}}};
}

json computePass(const std::string& name, json sets, json outputs = json::array()) {
    return {{"name", name}, {"type", "compute"}, {"hasSideEffects", true},
            {"execution", {{"type", "compute_dispatch"}, {"dispatch", {{"x", 1}, {"y", 1}, {"z", 1}}}}},
            {"descriptorSets", std::move(sets)}, {"outputs", std::move(outputs)}};
}

json drawPass(const std::string& name, json sets, json outputs, json inputs = json::array(),
              const std::string& execution = "fullscreen") {
    return {{"name", name}, {"type", "graphics"}, {"execution", {{"type", execution}}},
            {"descriptorSets", std::move(sets)}, {"inputs", std::move(inputs)},
            {"outputs", std::move(outputs)}};
}

json storage(uint32_t binding, const std::string& buffer, json stages) {
    json b = {{"binding", binding}, {"type", "storage_buffer"}, {"stages", std::move(stages)}};
    if (!buffer.empty()) b["autoBindBuffer"] = buffer;
    return b;
}

json graph(json resources, json layouts, json passes) {
    return {{"version", 1}, {"name", "sync_plan_test"}, {"resources", std::move(resources)},
            {"descriptorSetLayouts", std::move(layouts)}, {"passes", std::move(passes)}};
}

struct Planned {
    FrameGraphBuilder builder;
    std::vector<ScheduledResource> resources;
    std::vector<uint32_t> order;
    BufferAccessTable buffers;
    std::vector<MemoryBarrierInfo> memory;
    std::vector<std::vector<AttachmentOps>> ops;
};

Planned plan(const json& j, const ShaderBufferQuery& query = {}) {
    Planned p;
    loadGraphFromJson(p.builder, j);
    const auto& passes = p.builder.getPassDeclarations();
    const auto& decls = p.builder.getResourceDeclarations();
    p.resources = describeResources(decls);
    const auto deps = passDependencies(passes, p.resources);
    std::string err;
    EXPECT_TRUE(sortPasses(passes, deps, p.order, err)) << err;  // nothing culled
    p.buffers = describeBufferAccesses(passes, decls, p.builder.getDescriptorSetLayouts(), query);
    p.memory = planBufferBarriers(p.order, p.buffers);
    p.ops = planAttachmentOps(passes, decls, p.builder.getDescriptorSetLayouts(), p.order, p.resources);
    return p;
}

uint32_t passIndex(const FrameGraphBuilder& b, const std::string& name) {
    const auto& passes = b.getPassDeclarations();
    for (uint32_t i = 0; i < passes.size(); ++i)
        if (passes[i].name == name) return i;
    ADD_FAILURE() << "no pass " << name;
    return 0;
}

const MemoryBarrierInfo& before(const Planned& p, const std::string& name) {
    return p.memory[passIndex(p.builder, name)];
}

/// The particle example's shape: a compute pass writes particles, a draw
/// reads them in its vertex shader, and presents.
json particleGraph() {
    return graph(
        json::array({swapchain()}),
        {{"simSet", {{"bindings", {storage(0, "particles", {"compute"})}}}},
         {"drawSet", {{"bindings", {storage(0, "particles", {"vertex"})}}}}},
        json::array({computePass("Simulate", {"simSet"}),
                     drawPass("Draw", {"drawSet"}, json::array({present()}), json::array(), "draw")}));
}

} // namespace

// ── Buffer accesses ─────────────────────────────────────────────

TEST(BufferAccesses, DescriptorBindingsNameTheirBuffers) {
    auto p = plan(particleGraph());
    ASSERT_NE(std::find(p.buffers.buffers.begin(), p.buffers.buffers.end(), "particles"),
              p.buffers.buffers.end());
    const auto& sim = p.buffers.passes[passIndex(p.builder, "Simulate")];
    ASSERT_EQ(sim.size(), 1u);
    EXPECT_EQ(sim[0].stages, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_TRUE(sim[0].access & VK_ACCESS_SHADER_WRITE_BIT);  // not known to be readonly

    const auto& draw = p.buffers.passes[passIndex(p.builder, "Draw")];
    ASSERT_EQ(draw.size(), 1u);
    EXPECT_EQ(draw[0].stages, VkPipelineStageFlags{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT});
}

TEST(BufferAccesses, ReflectionNarrowsStagesAndWrites) {
    const auto draw = 1u;
    auto p = plan(particleGraph(), [&](uint32_t pass, uint32_t, uint32_t) -> std::optional<ShaderBufferUse> {
        if (pass == draw) return ShaderBufferUse{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, false};
        return std::nullopt;
    });
    const auto& access = p.buffers.passes[draw];
    ASSERT_EQ(access.size(), 1u);
    EXPECT_FALSE(access[0].access & VK_ACCESS_SHADER_WRITE_BIT);
}

TEST(BufferAccesses, UnnamedStorageBindingTouchesEveryBuffer) {
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"named", {{"bindings", {storage(0, "a", {"compute"})}}}},
         {"unnamed", {{"bindings", {storage(0, "", {"fragment"})}}}}},
        json::array({computePass("Write", {"named"}),
                     drawPass("Read", {"unnamed"}, json::array({present()}))})));
    // "a" and "*" are both read by the pass with the unnamed binding.
    EXPECT_EQ(p.buffers.passes[passIndex(p.builder, "Read")].size(), p.buffers.buffers.size());
    EXPECT_FALSE(before(p, "Read").empty());
}

// ── Buffer barriers ─────────────────────────────────────────────

TEST(BufferBarriers, ComputeWriteThenVertexReadGetsOneBarrier) {
    auto p = plan(particleGraph());
    const auto& b = before(p, "Draw");
    EXPECT_EQ(b.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(b.srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
    EXPECT_EQ(b.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT});
    EXPECT_TRUE(b.dstAccess & VK_ACCESS_SHADER_READ_BIT);
}

TEST(BufferBarriers, NextFrameWriterWaitsForThisFramesReader) {
    // Write after read across the frame boundary: execution only.
    auto p = plan(particleGraph(), [](uint32_t pass, uint32_t, uint32_t) -> std::optional<ShaderBufferUse> {
        if (pass == 0) return ShaderBufferUse{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true};
        return ShaderBufferUse{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, false};
    });
    const auto& b = before(p, "Simulate");
    EXPECT_EQ(b.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT});
    EXPECT_EQ(b.srcAccess, 0u);
    EXPECT_EQ(b.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
}

TEST(BufferBarriers, ComputeToComputeIsSynchronized) {
    // The executor's old blanket barrier only covered compute -> graphics.
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"s", {{"bindings", {storage(0, "data", {"compute"})}}}}},
        json::array({computePass("First", {"s"}), computePass("Second", {"s"}),
                     drawPass("Show", json::array(), json::array({present()}))})));
    const auto& b = before(p, "Second");
    EXPECT_EQ(b.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(b.srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
    EXPECT_EQ(b.dstStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
}

TEST(BufferBarriers, UnrelatedBuffersNeedNoBarrier) {
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"sim", {{"bindings", {storage(0, "a", {"compute"})}}}},
         {"draw", {{"bindings", {storage(0, "b", {"fragment"})}}}}},
        json::array({computePass("Simulate", {"sim"}),
                     drawPass("Show", {"draw"}, json::array({present()}))})),
        [](uint32_t pass, uint32_t, uint32_t) -> std::optional<ShaderBufferUse> {
            if (pass == 0) return ShaderBufferUse{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true};
            return ShaderBufferUse{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, false};
        });
    EXPECT_TRUE(before(p, "Show").empty());
}

TEST(BufferBarriers, ReadsAfterReadsShareTheEarlierBarrier) {
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"sim", {{"bindings", {storage(0, "data", {"compute"})}}}},
         {"draw", {{"bindings", {storage(0, "data", {"fragment"})}}}}},
        json::array({computePass("Simulate", {"sim"}),
                     drawPass("ReadOne", {"draw"}, json::array({present()})),
                     drawPass("ReadTwo", {"draw"}, json::array({present()}))})),
        [](uint32_t pass, uint32_t, uint32_t) -> std::optional<ShaderBufferUse> {
            if (pass == 0) return ShaderBufferUse{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, true};
            return ShaderBufferUse{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, false};  // readonly
        });
    EXPECT_FALSE(before(p, "ReadOne").empty());
    EXPECT_TRUE(before(p, "ReadTwo").empty());
}

TEST(BufferBarriers, WritableBindingsWithoutReflectionAreWriters) {
    // No reflection: both draws may write "data", so the second waits.
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"draw", {{"bindings", {storage(0, "data", {"fragment"})}}}}},
        json::array({drawPass("One", {"draw"}, json::array({present()})),
                     drawPass("Two", {"draw"}, json::array({present()}))})));
    const auto& b = before(p, "Two");
    EXPECT_EQ(b.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
    EXPECT_EQ(b.srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
}

TEST(BufferBarriers, EntityGeometryPassesWaitForComputeWrites) {
    // Their renderers bind vertex and index buffers the graph cannot see.
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"sim", {{"bindings", {storage(0, "data", {"compute"})}}}}},
        json::array({computePass("Simulate", {"sim"}),
                     drawPass("Opaque", json::array(), json::array({present()}), json::array(),
                              "opaque_geometry")})));
    const auto& b = before(p, "Opaque");
    EXPECT_TRUE(b.dstStage & VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
    EXPECT_TRUE(b.dstAccess & VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
}

TEST(BufferBarriers, UniformReadsAloneNeedNoBarrier) {
    auto p = plan(graph(
        json::array({swapchain()}),
        {{"u", {{"bindings", {{{"binding", 0}, {"type", "uniform_buffer"}, {"stages", {"compute", "fragment"}},
                                {"autoBindBuffer", "camera"}}}}}}},
        json::array({computePass("Cull", {"u"}), drawPass("Show", {"u"}, json::array({present()}))})));
    for (const auto& b : p.memory) EXPECT_TRUE(b.empty());
}

// ── Attachment ops ──────────────────────────────────────────────

namespace {

json depthOut(const std::string& usage = "depth_write", bool clear = true) {
    json o = {{"resource", "depth"}, {"usage", usage}};
    if (clear) o["clear"] = {{"depth", 1.0}};
    return o;
}

const AttachmentOps& opsOf(const Planned& p, const std::string& pass, size_t output) {
    return p.ops[passIndex(p.builder, pass)][output];
}

} // namespace

TEST(AttachmentOps, DepthNobodyReadsIsNotStored) {
    auto p = plan(graph(
        json::array({swapchain(), image("depth", {{"format", "D32_SFLOAT"}})}),
        json::object(),
        json::array({drawPass("Forward", json::array(), json::array({present(), depthOut()}))})));
    EXPECT_FALSE(opsOf(p, "Forward", 0).discardStore);  // presents
    EXPECT_TRUE(opsOf(p, "Forward", 1).discardStore);
    EXPECT_FALSE(opsOf(p, "Forward", 1).discardLoad);   // cleared
}

TEST(AttachmentOps, DepthReadLaterIsStored) {
    auto p = plan(graph(
        json::array({swapchain(), image("depth", {{"format", "D32_SFLOAT"}})}),
        json::object(),
        json::array({drawPass("Prepass", json::array(), json::array({depthOut()})),
                     drawPass("Forward", json::array(), json::array({present(), depthOut("depth_read", false)}))})));
    EXPECT_FALSE(opsOf(p, "Prepass", 0).discardStore);
}

TEST(AttachmentOps, ImageSampledThroughADescriptorIsStored) {
    // Bound by a descriptor set the reader uses, but not declared as an input.
    auto p = plan(graph(
        json::array({swapchain(), image("color", {{"format", "R8G8B8A8_UNORM"}})}),
        {{"s", {{"bindings", {{{"binding", 0}, {"type", "combined_image_sampler"}, {"stages", {"fragment"}},
                                {"autoBindResource", "color"}}}}}}},
        json::array({drawPass("Render", json::array(),
                              json::array({{{"resource", "color"}, {"usage", "color_write"}, {"clear", {0, 0, 0, 1}}}})),
                     drawPass("Show", {"s"}, json::array({present()}))})));
    EXPECT_FALSE(opsOf(p, "Render", 0).discardStore);
}

TEST(AttachmentOps, FirstUnclearedWriteDiscardsItsLoad) {
    auto p = plan(graph(
        json::array({swapchain(), image("color", {{"format", "R8G8B8A8_UNORM"}})}),
        json::object(),
        json::array({drawPass("Render", json::array(),
                              json::array({{{"resource", "color"}, {"usage", "color_write"}}})),
                     drawPass("Overlay", json::array(),
                              json::array({{{"resource", "color"}, {"usage", "color_write"}}})),
                     drawPass("Show", json::array(), json::array({present()}),
                              json::array({{{"resource", "color"}, {"usage", "shader_read"}}}))})));
    EXPECT_TRUE(opsOf(p, "Render", 0).discardLoad);
    EXPECT_FALSE(opsOf(p, "Overlay", 0).discardLoad);   // keeps what Render drew
    EXPECT_FALSE(opsOf(p, "Render", 0).discardStore);    // Overlay loads it
    EXPECT_FALSE(opsOf(p, "Overlay", 0).discardStore);
}

TEST(AttachmentOps, PersistentAndPresentedImagesKeepTheirContents) {
    auto p = plan(graph(
        json::array({swapchain(), image("history", {{"format", "R8G8B8A8_UNORM"}, {"persistent", true}})}),
        json::object(),
        json::array({drawPass("Accumulate", json::array(),
                              json::array({{{"resource", "history"}, {"usage", "color_write"}}, present()}))})));
    EXPECT_FALSE(opsOf(p, "Accumulate", 0).discardLoad);
    EXPECT_FALSE(opsOf(p, "Accumulate", 0).discardStore);
    EXPECT_FALSE(opsOf(p, "Accumulate", 1).discardStore);
}

TEST(AttachmentOps, OnlyTheMipsALaterPassReadsAreStored) {
    // Mip 0 is read by the next pass; mip 1 by nobody.
    auto p = plan(graph(
        json::array({swapchain(), image("chain", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64},
                                                 {"height", 64}, {"mipLevels", 2}})}),
        json::object(),
        json::array({drawPass("Mip0", json::array(),
                              json::array({{{"resource", "chain"}, {"usage", "color_write"}, {"mip", 0},
                                            {"clear", {0, 0, 0, 1}}}})),
                     drawPass("Mip1", json::array(),
                              json::array({{{"resource", "chain"}, {"usage", "color_write"}, {"mip", 1},
                                            {"clear", {0, 0, 0, 1}}}})),
                     drawPass("Show", json::array(), json::array({present()}),
                              json::array({{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", 0}}}))})));
    EXPECT_FALSE(opsOf(p, "Mip0", 0).discardStore);
    EXPECT_TRUE(opsOf(p, "Mip1", 0).discardStore);
}

TEST(AttachmentOps, PassesWithCallbacksMayReadAnything) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(
        json::array({swapchain(), image("depth", {{"format", "D32_SFLOAT"}})}),
        json::object(),
        json::array({drawPass("Forward", json::array(), json::array({present(), depthOut()})),
                     drawPass("Debug", json::array(), json::array({{{"resource", "swapchain"},
                                                                    {"usage", "color_blend"},
                                                                    {"present", true}}}), json::array(),
                              "manual")})));
    auto& passes = builder.getPassDeclarations();
    const auto decls = builder.getResourceDeclarations();
    const auto resources = describeResources(decls);
    const std::vector<uint32_t> order{0, 1};
    const auto ops = planAttachmentOps(passes, decls, builder.getDescriptorSetLayouts(), order, resources);
    EXPECT_FALSE(ops[0][1].discardStore);
}

// ── Shipped pipelines, with their shaders reflected ─────────────

namespace {

/// A shipped pipeline scheduled as the compiler does: culled, with buffer
/// accesses narrowed by reflecting its committed SPIR-V.
Planned planShipped(const std::string& relativePath) {
    const auto file = std::filesystem::path(SHOONYAKASHA_SOURCE_DIR) / relativePath;
    Planned p;
    loadGraphFromFile(p.builder, file.string());
    const auto& passes = p.builder.getPassDeclarations();
    const auto& decls = p.builder.getResourceDeclarations();
    p.resources = describeResources(decls);
    const auto deps = passDependencies(passes, p.resources);
    std::string err;
    EXPECT_TRUE(sortPasses(passes, deps, p.order, err)) << err;
    const auto live = livePasses(passes, decls, deps);
    std::erase_if(p.order, [&](uint32_t pi) { return !std::binary_search(live.begin(), live.end(), pi); });

    // Shader paths in the C++ examples are relative to the example.
    const auto saved = std::filesystem::current_path();
    std::filesystem::current_path(file.parent_path());
    p.buffers = describeBufferAccesses(passes, decls, p.builder.getDescriptorSetLayouts(),
                                       reflectShaderBufferUse(passes));
    std::filesystem::current_path(saved);

    p.memory = planBufferBarriers(p.order, p.buffers);
    p.ops = planAttachmentOps(passes, decls, p.builder.getDescriptorSetLayouts(), p.order, p.resources);
    return p;
}

} // namespace

TEST(ShippedPipelines, DefaultPipelineNeedsNoBufferBarriers) {
    // Its compute passes write images, which image barriers cover, and every
    // storage buffer its shaders bind is readonly. The executor used to put a
    // compute -> graphics barrier after each of its two compute passes.
    auto p = planShipped("python/shoonyakasha/pipelines/default/pipeline.json");
    for (uint32_t pi : p.order) {
        EXPECT_TRUE(p.memory[pi].empty()) << p.builder.getPassDeclarations()[pi].name;
    }
}

TEST(ShippedPipelines, ParticlesWaitForTheSimulationAndTheSimulationForLastFramesDraw) {
    auto p = planShipped("examples/cpp/compute/particle_test/particle_pipeline.json");
    const auto& draw = before(p, "ParticleRender");
    EXPECT_EQ(draw.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT});
    EXPECT_EQ(draw.srcAccess, VkAccessFlags{VK_ACCESS_SHADER_WRITE_BIT});
    EXPECT_TRUE(draw.dstStage & VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    // The draw reads the particles in its vertex shader; the next frame's
    // simulation must not overwrite them before it is done.
    const auto& sim = before(p, "ParticleSimulate");
    EXPECT_EQ(sim.srcStage, VkPipelineStageFlags{VK_PIPELINE_STAGE_VERTEX_SHADER_BIT});
    EXPECT_EQ(sim.srcAccess, 0u);

    // Depth only serves the draw's own test.
    const auto& outputs = p.builder.getPassDeclarations()[passIndex(p.builder, "ParticleRender")].outputs;
    for (size_t oi = 0; oi < outputs.size(); ++oi) {
        const auto& name = p.builder.getResourceDeclarations()[outputs[oi].handle.index].name;
        EXPECT_EQ(opsOf(p, "ParticleRender", oi).discardStore, name == "sceneDepth") << name;
    }
}
