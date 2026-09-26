//
// BufferFieldDefaultTest.cpp - buffer-layout field "default" values
//
// Tier 1: no GPU. The production path end to end: JSON, FrameGraphCompiler,
// the resolver layout it hands DotPathResolver, and the bytes written.
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <stdexcept>

using namespace Shoonyakasha;
using nlohmann::json;

namespace {

json settingsLayout(json fields) {
    return {
        {"version", 1},
        {"bufferLayouts", {{"Settings", {{"usage", "uniform_buffer"}, {"packing", "std140"},
                                         {"fields", std::move(fields)}}}}},
        {"resources", json::array()},
        {"passes", json::array()}
    };
}

CompiledBufferLayout compileSettings(const json& fields) {
    FrameGraph::FrameGraphBuilder builder;
    FrameGraph::loadGraphFromJson(builder, settingsLayout(fields));
    FrameGraph::FrameGraphCompiler compiler;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> layouts;
    compiler.compileBufferLayouts(builder.getBufferLayouts(), layouts);
    EXPECT_EQ(layouts.count("Settings"), 1u);
    return layouts["Settings"].toResolverLayout();
}

template <typename T>
T read(const std::vector<uint8_t>& buffer, const CompiledBufferLayout& layout, size_t field) {
    T value{};
    std::memcpy(&value, buffer.data() + layout.fields[field].offset, sizeof(T));
    return value;
}

} // namespace

TEST(BufferFieldDefault, UnresolvedSourcesTakeTheirDefault) {
    const auto layout = compileSettings(json::array({
        {{"name", "exposure"}, {"type", "float"}, {"source", "scene.custom.test.exposure"}, {"default", 1.5}},
        {{"name", "bias"}, {"type", "float"}, {"source", "scene.custom.test.bias"}, {"default", 0.25}},
        {{"name", "plain"}, {"type", "float"}, {"source", "scene.custom.test.plain"}},
        {{"name", "index"}, {"type", "int"}, {"source", "scene.custom.test.index"}, {"default", -1}},
        {{"name", "tint"}, {"type", "vec4"}, {"source", "scene.custom.test.tint"}, {"default", {1, 0.5, 0.25, 1}}}
    }));

    SceneContext scene;
    scene.setCustom("test.bias", 0.75f);   // published: the default does not apply

    DotPathResolver resolver;
    BufferLayoutResolver fill(resolver);
    std::vector<uint8_t> buffer(layout.totalSize, 0xCD);
    fill.fillSceneBuffer(buffer.data(), layout, scene);

    EXPECT_FLOAT_EQ(read<float>(buffer, layout, 0), 1.5f);
    EXPECT_FLOAT_EQ(read<float>(buffer, layout, 1), 0.75f);
    EXPECT_EQ(read<int32_t>(buffer, layout, 3), -1);
    const auto tint = read<glm::vec4>(buffer, layout, 4);
    EXPECT_FLOAT_EQ(tint.y, 0.5f);
    EXPECT_FLOAT_EQ(tint.z, 0.25f);
}

TEST(BufferFieldDefault, ADefaultThatDoesNotFitTheTypeIsIgnored) {
    const auto layout = compileSettings(json::array({
        {{"name", "tint"}, {"type", "vec4"}, {"source", "scene.custom.test.tint"}, {"default", {1, 2}}}
    }));
    EXPECT_FALSE(layout.fields[0].fallback.isValid());
}

TEST(BufferFieldDefault, DefaultsRoundTripThroughTheLayoutDesc) {
    FrameGraph::FrameGraphBuilder builder;
    FrameGraph::loadGraphFromJson(builder, settingsLayout(json::array({
        {{"name", "tint"}, {"type", "vec3"}, {"source", "scene.custom.test.tint"}, {"default", {1, 2, 3}}},
        {{"name", "scale"}, {"type", "float"}, {"source", "scene.custom.test.scale"}, {"default", 4}}
    })));
    const auto& fields = builder.getBufferLayouts().at(0).fields;
    EXPECT_EQ(fields[0].defaultValue, (std::vector<float>{1, 2, 3}));
    EXPECT_EQ(fields[1].defaultValue, (std::vector<float>{4}));
}

TEST(BufferFieldDefault, DefaultsThatAreNotNumbersFailAtLoad) {
    for (const json& bad : {json("one"), json::array({1, "two"}), json::object()}) {
        FrameGraph::FrameGraphBuilder builder;
        EXPECT_THROW(FrameGraph::loadGraphFromJson(builder, settingsLayout(json::array({
                         {{"name", "x"}, {"type", "float"}, {"source", "scene.custom.x"}, {"default", bad}}}))),
                     std::runtime_error)
            << bad.dump();
    }
}
