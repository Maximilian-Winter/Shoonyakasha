//
// ShadowPipelineOptionsTest.cpp - pipeline JSON options for shadow passes
//
// Covers "depthCompareOp", "depthClamp", pass "enabled", execution
// "alphaFilter", and the entity filters for static and skinned shadow
// casters.
//
// Tier 1: pure JSON -> declaration parsing and filter logic, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "FrameGraph/FrameGraphRenderer.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;

namespace {

/// One depth-only pass with the given "pipeline" block, plus extra keys
/// merged into the pass object.
nlohmann::json graphWithPass(const nlohmann::json& pipeline,
                             const nlohmann::json& passExtras = nlohmann::json::object()) {
    nlohmann::json pass = {
        {"name", "ShadowPass"},
        {"type", "graphics"},
        {"outputs", {{{"resource", "shadowMap"}, {"usage", "depth_write"}}}},
        {"pipeline", pipeline}
    };
    pass.update(passExtras);

    return nlohmann::json{
        {"version", 1},
        {"name", "shadow_options_test"},
        {"resources", {
            {
                {"name", "shadowMap"},
                {"kind", "image"},
                {"image", {{"format", "D32_SFLOAT"}, {"width", 1024}, {"height", 1024}}}
            }
        }},
        {"passes", {pass}}
    };
}

const nlohmann::json kShadowPipeline = {{"vertexShader", "shadow.vert.spv"}};

nlohmann::json execution(const std::string& type, const std::string& alphaFilter) {
    return {{"execution", {{"type", type}, {"alphaFilter", alphaFilter}}}};
}

const PassDeclaration& onlyPass(const FrameGraphBuilder& builder) {
    return builder.getPassDeclarations().at(0);
}

} // namespace

// ── depthCompareOp ──────────────────────────────────────────────

TEST(ShadowPipelineOptions, DepthCompareOpDefaultsToLess) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPass(kShadowPipeline));
    EXPECT_EQ(onlyPass(builder).pipelineDesc.depthCompareOp, "less");
}

TEST(ShadowPipelineOptions, DepthCompareOpParses) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPass({
        {"vertexShader", "shadow.vert.spv"},
        {"depthCompareOp", "greater_or_equal"}
    }));
    EXPECT_EQ(onlyPass(builder).pipelineDesc.depthCompareOp, "greater_or_equal");
}

TEST(ShadowPipelineOptions, UnknownDepthCompareOpFailsAtLoad) {
    FrameGraphBuilder builder;
    EXPECT_THROW(loadGraphFromJson(builder, graphWithPass({
        {"vertexShader", "shadow.vert.spv"},
        {"depthCompareOp", "lesser"}
    })), std::runtime_error);
}

TEST(ShadowPipelineOptions, DepthCompareOpRoundTripsAndDefaultIsOmitted) {
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWithPass({
        {"vertexShader", "shadow.vert.spv"},
        {"depthCompareOp", "greater"}
    }));
    FrameGraphBuilder reloaded;
    loadGraphFromJson(reloaded, saveGraphToJson(original));
    EXPECT_EQ(onlyPass(reloaded).pipelineDesc.depthCompareOp, "greater");

    FrameGraphBuilder plain;
    loadGraphFromJson(plain, graphWithPass(kShadowPipeline));
    EXPECT_FALSE(saveGraphToJson(plain)["passes"][0]["pipeline"].contains("depthCompareOp"));
}

// ── depthClamp ──────────────────────────────────────────────────

TEST(ShadowPipelineOptions, DepthClampOffByDefault) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPass(kShadowPipeline));
    EXPECT_FALSE(onlyPass(builder).pipelineDesc.depthClamp);
    EXPECT_FALSE(saveGraphToJson(builder)["passes"][0]["pipeline"].contains("depthClamp"));
}

TEST(ShadowPipelineOptions, DepthClampParsesAndRoundTrips) {
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWithPass({
        {"vertexShader", "shadow.vert.spv"},
        {"depthClamp", true}
    }));
    EXPECT_TRUE(onlyPass(original).pipelineDesc.depthClamp);

    FrameGraphBuilder reloaded;
    loadGraphFromJson(reloaded, saveGraphToJson(original));
    EXPECT_TRUE(onlyPass(reloaded).pipelineDesc.depthClamp);
}

// ── Pass "enabled" ──────────────────────────────────────────────

TEST(ShadowPipelineOptions, PassEnabledByDefault) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPass(kShadowPipeline));
    EXPECT_TRUE(onlyPass(builder).enabled);
    EXPECT_FALSE(saveGraphToJson(builder)["passes"][0].contains("enabled"));
}

TEST(ShadowPipelineOptions, DisabledPassRoundTrips) {
    FrameGraphBuilder original;
    loadGraphFromJson(original, graphWithPass(kShadowPipeline, {{"enabled", false}}));
    EXPECT_FALSE(onlyPass(original).enabled);

    FrameGraphBuilder reloaded;
    loadGraphFromJson(reloaded, saveGraphToJson(original));
    EXPECT_FALSE(onlyPass(reloaded).enabled);
}

// ── execution "alphaFilter" ─────────────────────────────────────

TEST(ShadowPipelineOptions, AlphaFilterDefaultsToAny) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graphWithPass(kShadowPipeline,
        {{"execution", {{"type", "shadow_casters"}}}}));
    EXPECT_EQ(onlyPass(builder).execution.alphaFilter, AlphaFilter::Any);
}

TEST(ShadowPipelineOptions, AlphaFilterParses) {
    FrameGraphBuilder opaque;
    loadGraphFromJson(opaque, graphWithPass(kShadowPipeline, execution("shadow_casters", "opaque")));
    EXPECT_EQ(onlyPass(opaque).execution.alphaFilter, AlphaFilter::Opaque);

    FrameGraphBuilder mask;
    loadGraphFromJson(mask, graphWithPass(kShadowPipeline, execution("skinned_shadow_casters", "mask")));
    EXPECT_EQ(onlyPass(mask).execution.alphaFilter, AlphaFilter::Mask);
}

TEST(ShadowPipelineOptions, UnknownAlphaFilterFailsAtLoad) {
    FrameGraphBuilder builder;
    EXPECT_THROW(loadGraphFromJson(builder,
        graphWithPass(kShadowPipeline, execution("shadow_casters", "cutout"))),
        std::runtime_error);
}

TEST(ShadowPipelineOptions, AlphaFilterRejectedWhereItCanMatchNothing) {
    for (const char* type : {"transparent_geometry", "skinned_transparent",
                             "sprite_geometry", "fullscreen"}) {
        FrameGraphBuilder builder;
        EXPECT_THROW(loadGraphFromJson(builder,
            graphWithPass(kShadowPipeline, execution(type, "opaque"))),
            std::runtime_error) << type;
    }
}

TEST(ShadowPipelineOptions, AlphaFilterAnyAcceptedOnEveryType) {
    FrameGraphBuilder builder;
    EXPECT_NO_THROW(loadGraphFromJson(builder,
        graphWithPass(kShadowPipeline, execution("transparent_geometry", "any"))));
}

// ── Execution type list ─────────────────────────────────────────

TEST(ShadowPipelineOptions, EntityGeometryTypesIncludeBothShadowCasterTypes) {
    EXPECT_TRUE(isEntityGeometryExecutionType("shadow_casters"));
    EXPECT_TRUE(isEntityGeometryExecutionType("skinned_shadow_casters"));
    EXPECT_TRUE(isEntityGeometryExecutionType("sprite_geometry"));
    // Hybrid type: the application draws, not the entity renderer.
    EXPECT_FALSE(isEntityGeometryExecutionType("scene_geometry"));
    EXPECT_FALSE(isEntityGeometryExecutionType("fullscreen"));
}

// ── Entity filters ──────────────────────────────────────────────

namespace {

MaterialComponentV5 materialWith(AlphaMode mode) {
    MaterialComponentV5 m;
    m.alphaMode = mode;
    return m;
}

bool drawn(EntityFilter filter, AlphaMode mode, bool hasSkeleton,
           bool castShadows = true, bool isSprite2D = false) {
    RenderableTagComponent tag;
    tag.castShadows = castShadows;
    return FrameGraphRenderer::passesFilter(materialWith(mode), tag, filter, hasSkeleton, isSprite2D);
}

} // namespace

TEST(ShadowCasterFilter, StaticCastersExcludeSkinnedEntities) {
    EXPECT_TRUE(drawn(EntityFilter::ShadowCasters, AlphaMode::Opaque, false));
    EXPECT_TRUE(drawn(EntityFilter::ShadowCasters, AlphaMode::Mask, false));
    EXPECT_FALSE(drawn(EntityFilter::ShadowCasters, AlphaMode::Opaque, true));
}

TEST(ShadowCasterFilter, SkinnedCastersOnlyTakeSkinnedEntities) {
    EXPECT_TRUE(drawn(EntityFilter::SkinnedShadowCasters, AlphaMode::Opaque, true));
    EXPECT_TRUE(drawn(EntityFilter::SkinnedShadowCasters, AlphaMode::Mask, true));
    EXPECT_FALSE(drawn(EntityFilter::SkinnedShadowCasters, AlphaMode::Opaque, false));
}

TEST(ShadowCasterFilter, BothRespectCastShadowsAndSkipBlendedMaterials) {
    for (auto filter : {EntityFilter::ShadowCasters, EntityFilter::SkinnedShadowCasters}) {
        const bool skinned = filter == EntityFilter::SkinnedShadowCasters;
        EXPECT_FALSE(drawn(filter, AlphaMode::Opaque, skinned, /*castShadows=*/false));
        EXPECT_FALSE(drawn(filter, AlphaMode::Blend, skinned));
    }
}

TEST(ShadowCasterFilter, SpritesNeverCastShadows) {
    EXPECT_FALSE(drawn(EntityFilter::ShadowCasters, AlphaMode::Opaque, false, true, /*isSprite2D=*/true));
}

TEST(ShadowCasterFilter, AlphaFilterNarrowsByAlphaMode) {
    const auto opaque = materialWith(AlphaMode::Opaque);
    const auto mask = materialWith(AlphaMode::Mask);
    const auto blend = materialWith(AlphaMode::Blend);

    EXPECT_TRUE(FrameGraphRenderer::passesAlphaFilter(opaque, AlphaFilter::Any));
    EXPECT_TRUE(FrameGraphRenderer::passesAlphaFilter(blend, AlphaFilter::Any));

    EXPECT_TRUE(FrameGraphRenderer::passesAlphaFilter(opaque, AlphaFilter::Opaque));
    EXPECT_FALSE(FrameGraphRenderer::passesAlphaFilter(mask, AlphaFilter::Opaque));

    EXPECT_TRUE(FrameGraphRenderer::passesAlphaFilter(mask, AlphaFilter::Mask));
    EXPECT_FALSE(FrameGraphRenderer::passesAlphaFilter(opaque, AlphaFilter::Mask));
}
