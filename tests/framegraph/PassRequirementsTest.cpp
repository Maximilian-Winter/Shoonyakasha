//
// PassRequirementsTest.cpp - pass "requires" and the capabilities of a device
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json graph(json needs) {
    json traced = {{"name", "Traced{i}"}, {"type", "graphics"}, {"repeat", {{"count", 2}, {"index", "i"}}},
                   {"outputs", json::array({{{"resource", "mask"}, {"usage", "color_write"}}})}};
    if (!needs.is_null()) traced["requires"] = std::move(needs);
    return {
        {"version", 1},
        {"resources", json::array({{{"name", "mask"}, {"kind", "image"}, {"image", {{"format", "R8_UNORM"}}}}})},
        {"passes", json::array({
            {{"name", "Mapped"}, {"type", "graphics"},
             {"outputs", json::array({{{"resource", "mask"}, {"usage", "color_write"}, {"clear", {1, 1, 1, 1}}}})}},
            traced})},
        {"presets", {{"fallback", {{"passes", {{"Traced{i}", false}, {"Mapped", true}}}}}}}
    };
}

} // namespace

TEST(PassRequirements, PassesNeedingAMissingCapabilityAreLeftOut) {
    PipelineCapabilities none;
    none.rayQuery = false;
    FrameGraphBuilder b;
    loadGraphFromJson(b, graph(json::array({"rayQuery"})), none);
    ASSERT_EQ(b.getPassDeclarations().size(), 1u);
    EXPECT_EQ(b.getPassDeclarations()[0].name, "Mapped");
    EXPECT_TRUE(b.isUnavailablePass("Traced{i}"));   // the declared name
    EXPECT_TRUE(b.isUnavailablePass("Traced0"));
    EXPECT_NE(b.getPreset("fallback"), nullptr);      // presets may still name them
}

TEST(PassRequirements, PassesWithTheCapabilityStay) {
    FrameGraphBuilder b;
    loadGraphFromJson(b, graph(json::array({"rayQuery"})));   // default: everything available
    EXPECT_EQ(b.getPassDeclarations().size(), 3u);
    EXPECT_FALSE(b.isUnavailablePass("Traced0"));
}

TEST(PassRequirements, UnknownCapabilitiesFailAtLoad) {
    for (const json& bad : {json("rayQuery"), json::array({"meshShaders"})}) {
        FrameGraphBuilder b;
        EXPECT_THROW(loadGraphFromJson(b, graph(bad)), std::runtime_error) << bad.dump();
    }
}

TEST(PassRequirements, AccelerationStructureIsADescriptorType) {
    EXPECT_EQ(JsonUtils::stringToDescriptorType("acceleration_structure"),
              VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR);
    EXPECT_EQ(JsonUtils::descriptorTypeToString(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR),
              "acceleration_structure");
}
