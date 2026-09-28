//
// PipelinePresetTest.cpp - pipeline JSON "presets"
//
// Tier 1: no GPU. Parsing, and setting a preset's values with the types of
// the fields that read them.
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha;
using nlohmann::json;

namespace {

json pipelineWithPresets(json presets) {
    return {
        {"version", 1},
        {"bufferLayouts", {{"Settings", {{"usage", "uniform_buffer"}, {"fields", json::array({
            {{"name", "exposure"}, {"type", "float"}, {"source", "scene.custom.t.exposure"}, {"default", 1.0}},
            {{"name", "mode"}, {"type", "uint"}, {"source", "scene.custom.t.mode"}},
            {{"name", "tint"}, {"type", "vec3"}, {"source", "scene.custom.t.tint"}}})}}}}},
        {"resources", json::array({{{"name", "swapchain"}, {"kind", "image"}, {"imported", true}},
                                   {{"name", "shadow"}, {"kind", "image"},
                                    {"image", {{"format", "D32_SFLOAT"}, {"arrayLayers", 2}}}}})},
        {"passes", json::array({
            {{"name", "Shadow{c}"}, {"type", "graphics"}, {"repeat", {{"count", 2}, {"index", "c"}}},
             {"outputs", json::array({{{"resource", "shadow"}, {"usage", "depth_write"}, {"layer", "{c}"}}})}},
            {{"name", "Main"}, {"type", "graphics"},
             {"outputs", json::array({{{"resource", "swapchain"}, {"usage", "color_write"}, {"present", true}}})}}})},
        {"presets", std::move(presets)}
    };
}

FrameGraph::FrameGraphBuilder load(const json& j) {
    FrameGraph::FrameGraphBuilder b;
    FrameGraph::loadGraphFromJson(b, j);
    return b;
}

} // namespace

TEST(PipelinePresets, ParseInDeclarationOrder) {
    auto b = load(pipelineWithPresets({
        {"low", {{"passes", {{"Shadow{c}", false}}}, {"values", {{"t.exposure", 0.5}, {"t.tint", {1, 0, 0}}}}}},
        {"high", {{"passes", {{"Shadow{c}", true}, {"Main", true}}}, {"values", {{"t.exposure", nullptr}}}}}}));
    ASSERT_EQ(b.getPresets().size(), 2u);
    const auto* low = b.getPreset("low");
    ASSERT_NE(low, nullptr);
    ASSERT_EQ(low->passes.size(), 1u);
    EXPECT_EQ(low->passes[0], (std::pair<std::string, bool>{"Shadow{c}", false}));
    ASSERT_EQ(low->values.size(), 2u);
    const auto* high = b.getPreset("high");
    ASSERT_NE(high, nullptr);
    EXPECT_FALSE(high->values[0].second.has_value());   // null: back to the default
    EXPECT_EQ(b.getPreset("ultra"), nullptr);
}

TEST(PipelinePresets, ValuesTakeTheTypeOfTheFieldThatReadsThem) {
    auto b = load(pipelineWithPresets({
        {"p", {{"values", {{"t.exposure", 2}, {"t.mode", 3}, {"t.tint", {0.5, 0.25, 1}},
                           {"t.unread", 7}, {"t.bad", {1, 2, 3, 4, 5}}}}}}}));
    SceneContext scene;
    scene.setCustom("t.gone", 1.0f);
    const auto warnings = FrameGraph::applyPresetValues(b, *b.getPreset("p"), scene);

    ASSERT_TRUE(scene.customValues.count("t.exposure"));
    EXPECT_FLOAT_EQ(scene.customValues["t.exposure"].as<float>(), 2.0f);   // written as 2, read as float
    EXPECT_EQ(scene.customValues["t.mode"].as<uint32_t>(), 3u);
    EXPECT_FLOAT_EQ(scene.customValues["t.tint"].as<glm::vec3>().y, 0.25f);
    EXPECT_FLOAT_EQ(scene.customValues["t.unread"].as<float>(), 7.0f);   // no field: a float
    EXPECT_FALSE(scene.customValues.count("t.bad"));
    EXPECT_EQ(warnings.size(), 1u);
}

TEST(PipelinePresets, NullRemovesTheValueSoTheDefaultApplies) {
    auto b = load(pipelineWithPresets({{"reset", {{"values", {{"t.exposure", nullptr}}}}}}));
    SceneContext scene;
    scene.setCustom("t.exposure", 0.25f);
    FrameGraph::applyPresetValues(b, *b.getPreset("reset"), scene);
    EXPECT_FALSE(scene.customValues.count("t.exposure"));
}

TEST(PipelinePresets, MistakesFailAtLoad) {
    for (const json& presets : {
             json::array({1}),
             json{{"p", 1}},
             json{{"p", {{"passes", {{"Nope", false}}}}}},
             json{{"p", {{"passes", {{"Main", "off"}}}}}},
             json{{"p", {{"values", {{"t.exposure", "bright"}}}}}},
             json{{"p", {{"values", {{"t.tint", json::array()}}}}}}}) {
        FrameGraph::FrameGraphBuilder b;
        EXPECT_THROW(FrameGraph::loadGraphFromJson(b, pipelineWithPresets(presets)), std::runtime_error)
            << presets.dump();
    }
}
