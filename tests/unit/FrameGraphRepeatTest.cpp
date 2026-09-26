//
// FrameGraphRepeatTest.cpp - "repeat" passes and layouts, and pass.* dot-paths
//
// Tier 1: pure JSON -> declarations and dot-path resolution, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "FrameGraph/DotPathResolver.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json graph(json passes, json layouts = json::object()) {
    json j = {
        {"version", 1},
        {"name", "repeat_test"},
        {"resources", json::array({
            {{"name", "shadow"}, {"kind", "image"},
             {"image", {{"format", "D32_SFLOAT"}, {"arrayLayers", 4}}}},
            {{"name", "chain"}, {"kind", "image"},
             {"image", {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}, {"mipLevels", 4}}}}
        })},
        {"passes", std::move(passes)}
    };
    if (!layouts.empty()) j["descriptorSetLayouts"] = std::move(layouts);
    return j;
}

json cascadePass() {
    return {
        {"name", "SunShadow"},
        {"type", "graphics"},
        {"repeat", {{"count", 4}, {"index", "cascade"}}},
        {"outputs", json::array({{{"resource", "shadow"}, {"usage", "depth_write"},
                                  {"layer", "{cascade}"}, {"clear", {{"depth", 1.0}}}}})}
    };
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder b;
    loadGraphFromJson(b, j);
    return b;
}

} // namespace

// ── Passes ──────────────────────────────────────────────────────

TEST(FrameGraphRepeat, PassExpandsIntoOneInstancePerIndex) {
    auto b = load(graph(json::array({cascadePass()})));
    const auto& passes = b.getPassDeclarations();
    ASSERT_EQ(passes.size(), 4u);
    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_EQ(passes[i].name, "SunShadow[" + std::to_string(i) + "]");
        EXPECT_EQ(passes[i].repeatGroup, "SunShadow");
        EXPECT_EQ(passes[i].repeatIndex, i);
        EXPECT_EQ(passes[i].repeatCount, 4u);
        // "{cascade}" alone became a number
        EXPECT_EQ(passes[i].outputs[0].subresource.baseLayer, i);
        EXPECT_EQ(passes[i].outputs[0].subresource.layerCount, 1u);
    }
}

TEST(FrameGraphRepeat, PassesWithoutRepeatAreUnchanged) {
    json p = cascadePass();
    p.erase("repeat");
    p["outputs"][0]["layer"] = 2;
    auto b = load(graph(json::array({p})));
    const auto& passes = b.getPassDeclarations();
    ASSERT_EQ(passes.size(), 1u);
    EXPECT_EQ(passes[0].name, "SunShadow");
    EXPECT_TRUE(passes[0].repeatGroup.empty());
    EXPECT_EQ(passes[0].repeatCount, 1u);
}

TEST(FrameGraphRepeat, NameWithPlaceholderNamesEachInstance) {
    json p = {
        {"name", "Down{m}"},
        {"type", "graphics"},
        {"repeat", {{"count", 3}, {"index", "m"}, {"first", 1}}},
        {"inputs",  json::array({{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", "{m-1}"}}})},
        {"outputs", json::array({{{"resource", "chain"}, {"usage", "color_write"}, {"mip", "{m}"}}})},
        {"descriptorSets", json::array({"downSet{m}"})}
    };
    auto b = load(graph(json::array({p})));
    const auto& passes = b.getPassDeclarations();
    ASSERT_EQ(passes.size(), 3u);
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t m = k + 1;
        EXPECT_EQ(passes[k].name, "Down" + std::to_string(m));
        EXPECT_EQ(passes[k].repeatGroup, "Down{m}");
        EXPECT_EQ(passes[k].repeatIndex, m);
        EXPECT_EQ(passes[k].inputs[0].subresource.baseMip, m - 1);
        EXPECT_EQ(passes[k].outputs[0].subresource.baseMip, m);
        EXPECT_EQ(passes[k].descriptorSetRefs[0], "downSet" + std::to_string(m));
    }
}

TEST(FrameGraphRepeat, PlaceholdersInsideLongerStringsAreSubstitutedAsText) {
    json p = cascadePass();
    p["pipeline"] = {{"vertexShader", "shaders/cascade_{cascade}.vert.spv"}};
    auto b = load(graph(json::array({p})));
    EXPECT_EQ(b.getPassDeclarations()[2].pipelineDesc.vertexShader, "shaders/cascade_2.vert.spv");
}

TEST(FrameGraphRepeat, OtherBracedTextIsLeftAlone) {
    json p = cascadePass();
    p["pipeline"] = {{"vertexShader", "shaders/{other}_{cascadeX}.spv"}};
    auto b = load(graph(json::array({p})));
    EXPECT_EQ(b.getPassDeclarations()[0].pipelineDesc.vertexShader, "shaders/{other}_{cascadeX}.spv");
}

TEST(FrameGraphRepeat, MalformedRepeatFailsAtLoad) {
    const json bad[] = {
        {{"count", 0}},
        {{"index", "i"}},
        {{"count", 2}, {"index", "not a name"}},
        {{"count", -1}},
        json::array({4}),
    };
    for (const auto& repeat : bad) {
        json p = cascadePass();
        p["repeat"] = repeat;
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph(json::array({p}))), std::runtime_error) << repeat.dump();
    }
}

TEST(FrameGraphRepeat, StepCountsDownForUpsampleChains) {
    // Up{m} blends mip m+1 into mip m, smallest first.
    json p = {
        {"name", "Up{m}"},
        {"type", "graphics"},
        {"repeat", {{"count", 3}, {"index", "m"}, {"first", 2}, {"step", -1}}},
        {"inputs",  json::array({{{"resource", "chain"}, {"usage", "shader_read"}, {"mip", "{m+1}"}}})},
        {"outputs", json::array({{{"resource", "chain"}, {"usage", "color_blend"}, {"mip", "{m}"}}})}
    };
    auto b = load(graph(json::array({p})));
    const auto& passes = b.getPassDeclarations();
    ASSERT_EQ(passes.size(), 3u);
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t m = 2 - k;
        EXPECT_EQ(passes[k].name, "Up" + std::to_string(m));
        EXPECT_EQ(passes[k].repeatIndex, m);
        EXPECT_EQ(passes[k].inputs[0].subresource.baseMip, m + 1);
        EXPECT_EQ(passes[k].outputs[0].subresource.baseMip, m);
    }
}

TEST(FrameGraphRepeat, StepMustBeNonZeroAndStayAtOrAboveZero) {
    for (const json& repeat : {json{{"count", 2}, {"step", 0}},
                               json{{"count", 2}, {"step", "down"}},
                               json{{"count", 3}, {"first", 1}, {"step", -1}}}) {
        json p = cascadePass();
        p["repeat"] = repeat;
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph(json::array({p}))), std::runtime_error) << repeat.dump();
    }
}

TEST(FrameGraphRepeat, NegativeSubstitutionFailsAtLoad) {
    json p = cascadePass();
    p["outputs"][0]["layer"] = "{cascade-1}";
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graph(json::array({p}))), std::runtime_error);
}

// ── Descriptor set layouts ──────────────────────────────────────

TEST(FrameGraphRepeat, LayoutExpandsIntoOneLayoutPerIndex) {
    json layouts = {
        {"downSet{m}", {
            {"repeat", {{"count", 3}, {"index", "m"}, {"first", 1}}},
            {"bindings", json::array({{{"binding", 0}, {"type", "combined_image_sampler"}, {"name", "src"},
                                       {"autoBindResource", "chain"}, {"mip", "{m-1}"}}})}
        }}
    };
    auto b = load(graph(json::array({cascadePass()}), layouts));
    const auto& decl = b.getDescriptorSetLayouts();
    ASSERT_EQ(decl.size(), 3u);
    for (uint32_t m = 1; m <= 3; ++m) {
        const auto* layout = b.getDescriptorSetLayout("downSet" + std::to_string(m));
        ASSERT_NE(layout, nullptr) << m;
        EXPECT_EQ(layout->bindings[0].autoBindSubresource.baseMip, m - 1);
    }
}

TEST(FrameGraphRepeat, RepeatedLayoutNameMustContainThePlaceholder) {
    json layouts = {
        {"downSet", {{"repeat", {{"count", 2}, {"index", "m"}}},
                     {"bindings", json::array({{{"binding", 0}, {"type", "uniform_buffer"}}})}}}
    };
    FrameGraphBuilder b;
    EXPECT_THROW(loadGraphFromJson(b, graph(json::array({cascadePass()}), layouts)), std::runtime_error);
}

// ── pass.* dot-paths ────────────────────────────────────────────

TEST(PassDotPaths, RootIsRecognised) {
    EXPECT_EQ(DotPathResolver::getPathRoot("pass.repeatIndex"), DotPathResolver::PathRoot::Pass);
    EXPECT_TRUE(DotPathResolver::isPassPath("pass.extent"));
}

TEST(PassDotPaths, ResolveFromTheSceneContextsPassInfo) {
    DotPathResolver resolver;
    SceneContext scene;
    scene.pass.repeatIndex = 3;
    scene.pass.repeatCount = 4;
    scene.pass.extent = glm::vec2(2048.0f, 1024.0f);

    entt::registry registry;
    const auto none = entt::entity{entt::null};
    EXPECT_EQ(resolver.resolve("pass.repeatIndex", scene, none, registry).as<uint32_t>(), 3u);
    EXPECT_EQ(resolver.resolve("pass.repeatCount", scene, none, registry).as<uint32_t>(), 4u);
    EXPECT_EQ(resolver.resolve("pass.extent", scene, none, registry).as<glm::vec2>(), glm::vec2(2048.0f, 1024.0f));
    EXPECT_EQ(resolver.resolve("pass.texelSize", scene, none, registry).as<glm::vec2>(),
              glm::vec2(1.0f / 2048.0f, 1.0f / 1024.0f));
    EXPECT_FALSE(resolver.resolve("pass.nothing", scene, none, registry).isValid());
}

TEST(PassDotPaths, ValidationNamesTheKnownValues) {
    DotPathResolver resolver;
    EXPECT_EQ(resolver.validatePath("pass.repeatIndex"), "");
    EXPECT_EQ(resolver.validatePath("pass.texelSize"), "");
    EXPECT_NE(resolver.validatePath("pass.repeat"), "");
}

TEST(PassDotPaths, LayoutsUsingThemAreMarked) {
    BufferLayoutDesc desc;
    desc.name = "ShadowPerDraw";
    desc.usage = BufferUsageType::PushConstant;
    desc.packing = BufferPackingRule::Scalar;
    BufferFieldDesc model;
    model.name = "model"; model.type = BufferFieldType::Mat4; model.source = "entity.transform.worldMatrix";
    BufferFieldDesc cascade;
    cascade.name = "cascade"; cascade.type = BufferFieldType::UInt; cascade.source = "pass.repeatIndex";
    desc.fields = {model, cascade};

    FrameGraphCompiler compiler;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> out;
    compiler.compileBufferLayouts({desc}, out);
    const auto& layout = out.at("ShadowPerDraw");
    EXPECT_TRUE(layout.hasPassSources);
    EXPECT_TRUE(layout.usesDotPathSources());
    EXPECT_TRUE(layout.toResolverLayout().hasPassSources);
    EXPECT_EQ(layout.totalSize, 68u);
}
