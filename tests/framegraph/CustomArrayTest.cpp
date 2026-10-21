//
// CustomArrayTest.cpp - scene.custom arrays read by buffer fields
//
// Tier 1: no GPU. JSON, FrameGraphCompiler, the resolver layout and the
// bytes written for a field with arrayCount and a "[i]" source.
//

#include <gtest/gtest.h>
#include "FrameGraph/DotPathResolver.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <cstring>

using namespace Shoonyakasha;
using nlohmann::json;

namespace {

CompiledBufferLayout compileBeams() {
    const json graph = {
        {"version", 1},
        {"bufferLayouts", {{"Beams", {{"usage", "uniform_buffer"}, {"packing", "std140"}, {"fields", {
            {{"name", "color"}, {"type", "vec4"}, {"arrayCount", 4}, {"source", "scene.custom.beams.color[i]"}},
            {{"name", "count"}, {"type", "uint"}, {"source", "scene.custom.beams.count"}}
        }}}}}},
        {"resources", json::array()},
        {"passes", json::array()}};
    FrameGraph::FrameGraphBuilder builder;
    FrameGraph::loadGraphFromJson(builder, graph);
    FrameGraph::FrameGraphCompiler compiler;
    std::unordered_map<std::string, FrameGraph::CompiledBufferLayout> layouts;
    compiler.compileBufferLayouts(builder.getBufferLayouts(), layouts);
    return layouts.at("Beams").toResolverLayout();
}

glm::vec4 element(const std::vector<uint8_t>& buffer, const CompiledBufferLayout& layout, uint32_t i) {
    glm::vec4 value;
    std::memcpy(&value, buffer.data() + layout.fields[0].offset + i * layout.fields[0].arrayStride, sizeof(value));
    return value;
}

} // namespace

TEST(CustomArray, ElementsFillTheFieldAndAShorterArrayClearsTheRest) {
    const auto layout = compileBeams();
    ASSERT_EQ(layout.fields[0].arrayCount, 4u);
    ASSERT_EQ(layout.fields[0].arrayStride, 16u);

    SceneContext scene;
    scene.setCustomArray<glm::vec4>("beams.color", {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}});
    EXPECT_EQ(scene.customValues.count("beams.color[2]"), 1u);

    DotPathResolver resolver;
    BufferLayoutResolver fill(resolver);
    std::vector<uint8_t> buffer(layout.totalSize, 0xCD);
    fill.fillSceneBuffer(buffer.data(), layout, scene);
    EXPECT_EQ(element(buffer, layout, 0), glm::vec4(1, 0, 0, 1));
    EXPECT_EQ(element(buffer, layout, 2), glm::vec4(0, 0, 1, 1));
    EXPECT_EQ(element(buffer, layout, 3), glm::vec4(0));   // never set

    scene.setCustomArray<glm::vec4>("beams.color", {{0.5f, 0.5f, 0.5f, 1}});
    EXPECT_EQ(scene.customValues.count("beams.color[1]"), 0u);
    EXPECT_EQ(scene.customValues.count("beams.color[2]"), 0u);
    fill.fillSceneBuffer(buffer.data(), layout, scene);
    EXPECT_EQ(element(buffer, layout, 0), glm::vec4(0.5f, 0.5f, 0.5f, 1));
    EXPECT_EQ(element(buffer, layout, 1), glm::vec4(0));
    EXPECT_EQ(element(buffer, layout, 2), glm::vec4(0));

    scene.setCustomArray<float>("beams.color", {});
    EXPECT_EQ(scene.customValues.count("beams.color[0]"), 0u);
}
