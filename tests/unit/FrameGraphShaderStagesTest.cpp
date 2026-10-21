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
