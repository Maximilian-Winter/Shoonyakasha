//
// FrameGraphShaderStagesTest.cpp - geometry and tessellation shaders
//
// Covers their pipeline JSON keys, the rules that tie them together, the
// device capabilities they need, and the barrier stages of the images a
// pass with them reads.
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

json image(const std::string& name) {
    return {{"name", name}, {"kind", "image"},
            {"image", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}}}};
}

json graphWith(json pipeline, json extra = json::object()) {
    json pass = {{"name", "P"}, {"type", "graphics"},
                 {"inputs", json::array({{{"resource", "src"}, {"usage", "shader_read"}}})},
                 {"outputs", json::array({{{"resource", "dst"}, {"usage", "color_write"}}})},
                 {"pipeline", pipeline}};
    pass.update(extra);
    return {{"version", 1}, {"name", "stages_test"},
            {"resources", json::array({image("src"), image("dst")})}, {"passes", json::array({pass})}};
}

const json kTess = {{"vertexShader", "s/p.vert.spv"}, {"tessControlShader", "s/p.tesc.spv"},
                    {"tessEvaluationShader", "s/p.tese.spv"}, {"topology", "patch_list"},
                    {"patchControlPoints", 3}};
const json kGeom = {{"vertexShader", "s/p.vert.spv"}, {"geometryShader", "s/p.geom.spv"},
                    {"fragmentShader", "s/p.frag.spv"}};

FrameGraphBuilder load(const json& j, const PipelineCapabilities& caps = {}) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j, caps);
    return builder;
}

} // namespace

TEST(ShaderStagesJson, KeysParseAndRoundTrip) {
    auto b = load(graphWith(kTess));
    const auto& pd = b.getPassDeclarations()[0].pipelineDesc;
    EXPECT_EQ(pd.tessControlShader, "s/p.tesc.spv");
    EXPECT_EQ(pd.shader(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT), "s/p.tese.spv");
    EXPECT_EQ(pd.patchControlPoints, 3u);

    auto reloaded = load(saveGraphToJson(load(graphWith(kGeom))));
    EXPECT_EQ(reloaded.getPassDeclarations()[0].pipelineDesc.geometryShader, "s/p.geom.spv");
    reloaded = load(saveGraphToJson(b));
    EXPECT_EQ(reloaded.getPassDeclarations()[0].pipelineDesc.patchControlPoints, 3u);
    EXPECT_EQ(JsonUtils::stringToTopology("patch_list"), VK_PRIMITIVE_TOPOLOGY_PATCH_LIST);
}

TEST(ShaderStagesJson, BadCombinationsFailAtLoad) {
    json onlyControl = kTess;
    onlyControl.erase("tessEvaluationShader");
    json noPatches = kTess;
    noPatches.erase("topology");
    json patchesAlone = {{"vertexShader", "s/p.vert.spv"}, {"topology", "patch_list"}};
    json zeroPoints = kTess;
    zeroPoints["patchControlPoints"] = 0;
    json tooMany = kTess;
    tooMany["patchControlPoints"] = 33;
    json pointsAlone = {{"vertexShader", "s/p.vert.spv"}, {"patchControlPoints", 3}};
    json noVertex = kGeom;
    noVertex.erase("vertexShader");
    for (const auto& pipeline : {onlyControl, noPatches, patchesAlone, zeroPoints, tooMany, pointsAlone, noVertex}) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graphWith(pipeline)), std::runtime_error) << pipeline.dump();
    }
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graphWith(kGeom, {{"type", "compute"}})), std::runtime_error);
}

TEST(ShaderStagesJson, MissingFeaturesFailUnlessRequired) {
    PipelineCapabilities none;
    none.geometryShader = false;
    none.tessellationShader = false;
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graphWith(kGeom), none), std::runtime_error);
    EXPECT_THROW(loadGraphFromJson(b, graphWith(kTess), none), std::runtime_error);

    auto optional = load(graphWith(kGeom, {{"requires", {"geometryShader"}}}), none);
    EXPECT_TRUE(optional.getPassDeclarations().empty());
    EXPECT_TRUE(optional.isUnavailablePass("P"));
    EXPECT_EQ(load(graphWith(kTess, {{"requires", {"tessellationShader"}}})).getPassDeclarations().size(), 1u);
}

TEST(ShaderStagesBarriers, ReadsWaitAtTheExtraStages) {
    auto b = load(graphWith(kGeom));
    const auto plan = planBarriers(b.getPassDeclarations(), {0}, describeResources(b.getResourceDeclarations()));
    bool found = false;
    for (const auto& barrier : plan.preBarriers[0]) {
        if (barrier.resource.index != 0) continue;
        found = true;
        EXPECT_TRUE(barrier.dstStage & VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT);
        EXPECT_TRUE(barrier.dstStage & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    EXPECT_TRUE(found);

    b = load(graphWith(kTess));
    EXPECT_EQ(extraShaderStages(b.getPassDeclarations()[0], ResourceUsage::ShaderReadOnly),
              static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                                                VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT));
    EXPECT_EQ(extraShaderStages(b.getPassDeclarations()[0], ResourceUsage::ColorAttachmentWrite), 0u);
}

// ── Mesh and task shaders ───────────────────────────────────────

namespace {

const json kMesh = {{"taskShader", "s/p.task.spv"}, {"meshShader", "s/p.mesh.spv"},
                    {"fragmentShader", "s/p.frag.spv"}};
const json kMeshExec = {{"execution", {{"type", "draw_mesh_tasks"}, {"dispatch", {{"x", 4}, {"y", 2}}}}}};

} // namespace

TEST(MeshShadersJson, ParseRoundTripAndExecution) {
    auto b = load(graphWith(kMesh, kMeshExec));
    const auto& pass = b.getPassDeclarations()[0];
    EXPECT_EQ(pass.pipelineDesc.meshShader, "s/p.mesh.spv");
    EXPECT_EQ(pass.pipelineDesc.shader(VK_SHADER_STAGE_TASK_BIT_EXT), "s/p.task.spv");
    EXPECT_EQ(pass.execution.dispatch[0].value, 4u);
    EXPECT_EQ(executionKindOf("draw_mesh_tasks"), ExecutionKind::DrawMeshTasks);
    EXPECT_EQ(JsonUtils::stringToShaderStage("mesh"), static_cast<VkShaderStageFlags>(VK_SHADER_STAGE_MESH_BIT_EXT));

    // The execution block is not saved; the shaders are.
    json saved = saveGraphToJson(b);
    saved["passes"][0]["execution"] = kMeshExec["execution"];
    EXPECT_EQ(load(saved).getPassDeclarations()[0].pipelineDesc.taskShader, "s/p.task.spv");
}

TEST(MeshShadersJson, BadCombinationsFailAtLoad) {
    json taskAlone = kMesh;
    taskAlone.erase("meshShader");
    json withVertex = kMesh;
    withVertex["vertexShader"] = "s/p.vert.spv";
    json withGeometry = kMesh;
    withGeometry["geometryShader"] = "s/p.geom.spv";
    for (const auto& pipeline : {taskAlone, withVertex, withGeometry}) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graphWith(pipeline, kMeshExec)), std::runtime_error) << pipeline.dump();
    }
    // A mesh pipeline drawn another way, and draw_mesh_tasks without one.
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graphWith(kMesh, {{"execution", {{"type", "fullscreen"}}}})),
                 std::runtime_error);
    EXPECT_THROW(loadGraphFromJson(b, graphWith({{"vertexShader", "s/p.vert.spv"}}, kMeshExec)), std::runtime_error);
}

TEST(MeshShadersJson, MissingFeaturesFailUnlessRequired) {
    PipelineCapabilities noTask;
    noTask.taskShader = false;
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graphWith(kMesh, kMeshExec), noTask), std::runtime_error);
    json meshOnly = kMesh;
    meshOnly.erase("taskShader");
    EXPECT_EQ(load(graphWith(meshOnly, kMeshExec), noTask).getPassDeclarations().size(), 1u);

    PipelineCapabilities none;
    none.meshShader = false;
    none.taskShader = false;
    json required = kMeshExec;
    required["requires"] = {"meshShader"};
    EXPECT_TRUE(load(graphWith(meshOnly, required), none).isUnavailablePass("P"));
}

TEST(MeshShadersBarriers, ReadsWaitAtTaskAndMeshStages) {
    auto b = load(graphWith(kMesh, kMeshExec));
    EXPECT_EQ(extraShaderStages(b.getPassDeclarations()[0], ResourceUsage::ShaderReadOnly),
              static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_TASK_SHADER_BIT_EXT |
                                                VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT));
}
