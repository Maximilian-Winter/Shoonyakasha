//
// SceneLightsBufferTest.cpp - storage buffers filled from the scene
//
// A storage_buffer layout whose "source" is {"type": "scene", "data":
// "lights"} holds the scene's lights as SceneContext::PackedLight, refilled
// every frame. Covers the JSON that declares one, its errors, and the
// check that its fields are the packed light.
//
// Tier 1: pure JSON -> declarations and layout compilation, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json vec4(const char* name) {
    return {{"name", name}, {"type", "vec4"}};
}

json lightFields() {
    return json::array({vec4("positionType"), vec4("colorIntensity"), vec4("directionRange"),
                        vec4("attenuation"), vec4("source"), vec4("shape")});
}

json lightsLayout(json fields = lightFields(), json source = {{"type", "scene"}, {"data", "lights"}},
                  const char* usage = "storage_buffer", int elementCount = 1024) {
    json layout = {{"usage", usage}, {"packing", "std430"}, {"fields", std::move(fields)},
                   {"source", std::move(source)}};
    if (elementCount > 0) layout["elementCount"] = elementCount;
    return layout;
}

json graph(json layout) {
    return {{"version", 1}, {"name", "scene_lights_test"},
            {"resources", {{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}}}},
            {"bufferLayouts", {{"SceneLights", std::move(layout)}}},
            {"passes", json::array()}};
}

std::string compileError(const json& layout) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(layout));
    FrameGraphCompiler compiler;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> compiled;
    compiler.compileBufferLayouts(builder.getBufferLayouts(), compiled);
    return sceneBuffersError(builder.getBufferLayouts(), compiled);
}

} // namespace

TEST(SceneLightsBuffer, SourceIsParsed) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, graph(lightsLayout()));
    const auto& layouts = builder.getBufferLayouts();
    ASSERT_EQ(layouts.size(), 1u);
    EXPECT_EQ(layouts[0].initConfig.type, "scene");
    EXPECT_EQ(layouts[0].initConfig.sceneData, "lights");
    EXPECT_EQ(layouts[0].elementCount, 1024u);
}

TEST(SceneLightsBuffer, ErrorsAreCaughtAtLoad) {
    const json bad[] = {
        lightsLayout(lightFields(), {{"type", "scene"}, {"data", "cameras"}}),   // not scene data a buffer holds
        lightsLayout(lightFields(), {{"type", "scene"}}),                        // no data named
        lightsLayout(lightFields(), {{"type", "scene"}, {"data", "lights"}}, "uniform_buffer"),
        lightsLayout(lightFields(), {{"type", "scene"}, {"data", "lights"}}, "storage_buffer", 0),  // no capacity
    };
    for (const auto& layout : bad) {
        FrameGraphBuilder builder;
        EXPECT_THROW(loadGraphFromJson(builder, graph(layout)), std::runtime_error) << layout.dump();
    }
}

TEST(SceneLightsBuffer, FieldsMustBeThePackedLight) {
    EXPECT_EQ(compileError(lightsLayout()), "");

    json fewer = lightFields();
    fewer.erase(fewer.size() - 1);
    EXPECT_NE(compileError(lightsLayout(fewer)), "");

    json vec3 = lightFields();
    vec3[2]["type"] = "vec3";
    EXPECT_NE(compileError(lightsLayout(vec3)), "");

    json more = lightFields();
    more.push_back(vec4("extra"));
    EXPECT_NE(compileError(lightsLayout(more)), "");
}

TEST(SceneLightsBuffer, PackedLightIsSixVec4s) {
    EXPECT_EQ(sizeof(SceneContext::PackedLight), 6 * sizeof(glm::vec4));
}
