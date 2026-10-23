//
// MaterialBufferBindingTest.cpp - "source": "material" uniform buffer bindings
//
// Tier 1: pure JSON -> declarations, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json graphWithBinding(json binding) {
    json j = {{"version", 1}, {"name", "material_buffer_test"}, {"resources", json::array()},
              {"passes", json::array()}};
    j["descriptorSetLayouts"] = {{"materialSet", {{"bindings", json::array({std::move(binding)})}}}};
    return j;
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

} // namespace

TEST(MaterialBufferBinding, UniformBufferWithMaterialSourceParses) {
    const auto b = load(graphWithBinding(
        {{"binding", 9}, {"type", "uniform_buffer"}, {"name", "MaterialUV"}, {"source", "material"}}));
    const auto& binding = b.getDescriptorSetLayouts()[0].bindings[0];
    EXPECT_EQ(binding.source, "material");
    EXPECT_EQ(binding.name, "MaterialUV");
    EXPECT_TRUE(binding.autoBindBuffer.empty());
}

TEST(MaterialBufferBinding, MaterialSourceOnlyOnUniformBuffers) {
    EXPECT_THROW(load(graphWithBinding(
                     {{"binding", 0}, {"type", "combined_image_sampler"}, {"name", "m"}, {"source", "material"}})),
                 std::runtime_error);
    EXPECT_THROW(load(graphWithBinding(
                     {{"binding", 0}, {"type", "storage_buffer"}, {"name", "m"}, {"source", "material"}})),
                 std::runtime_error);
}

TEST(MaterialBufferBinding, MaterialBufferIsOneBufferWithoutAutoBind) {
    EXPECT_THROW(load(graphWithBinding({{"binding", 0}, {"type", "uniform_buffer"}, {"name", "m"},
                                        {"source", "material"}, {"autoBindBuffer", "m"}})),
                 std::runtime_error);
    EXPECT_THROW(load(graphWithBinding({{"binding", 0}, {"type", "uniform_buffer"}, {"name", "m"},
                                        {"source", "material"}, {"count", 2}})),
                 std::runtime_error);
}

TEST(MaterialBufferBinding, SceneTexturesSourceStillOnImageArrays) {
    EXPECT_NO_THROW(load(graphWithBinding({{"binding", 0}, {"type", "combined_image_sampler"}, {"name", "t"},
                                           {"count", 16}, {"source", "sceneTextures"}})));
    EXPECT_THROW(load(graphWithBinding(
                     {{"binding", 0}, {"type", "uniform_buffer"}, {"name", "t"}, {"source", "sceneTextures"}})),
                 std::runtime_error);
}
