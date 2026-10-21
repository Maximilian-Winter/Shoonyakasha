//
// FrameGraphAliasTest.cpp - transient images sharing memory
//
// Covers lifetimes, which images may alias, packing them into memory blocks,
// and the barriers when one image of a block takes over from another.
//
// Tier 1: pure JSON -> declarations and scheduling, no GPU context.
//

#include <gtest/gtest.h>

#include "Vulkan/FrameGraph/FrameGraphJson.h"
#include "Vulkan/FrameGraph/FrameGraph.h"
#include "Vulkan/FrameGraph/FrameGraphSchedule.h"

#include <nlohmann/json.hpp>

using namespace Shoonyakasha;
using namespace Shoonyakasha::FrameGraph;
using nlohmann::json;

namespace {

json image(const std::string& name, json more = json::object()) {
    json desc = {{"format", "R16G16B16A16_SFLOAT"}, {"width", 64}, {"height", 64}, {"transient", true}};
    desc.update(more);
    return {{"name", name}, {"kind", "image"}, {"image", desc}};
}

json access(const std::string& resource, const std::string& usage) {
    return {{"resource", resource}, {"usage", usage}};
}

json pass(const std::string& name, json inputs, json outputs, const std::string& type = "compute") {
    return {{"name", name}, {"type", type}, {"inputs", inputs}, {"outputs", outputs},
            {"execution", {{"type", type == "compute" ? "compute_dispatch" : "fullscreen"}}}};
}

/// A writes a, B reads a and writes b, C reads b and writes c, D (a
/// graphics pass) reads c and writes out: a lives over A..B, b over B..C,
/// c over C..D.
json chain() {
    return {{"version", 1}, {"name", "alias_test"},
            {"resources", json::array({image("a"), image("b"), image("c"), image("out", {{"transient", false}})})},
            {"passes", json::array({
                pass("A", json::array(), json::array({access("a", "storage_image_write")})),
                pass("B", json::array({access("a", "shader_read")}), json::array({access("b", "storage_image_write")})),
                pass("C", json::array({access("b", "shader_read")}), json::array({access("c", "storage_image_write")})),
                pass("D", json::array({access("c", "shader_read")}), json::array({access("out", "storage_image_write")}),
                     "graphics"),
            })}};
}

FrameGraphBuilder load(const json& j) {
    FrameGraphBuilder builder;
    loadGraphFromJson(builder, j);
    return builder;
}

const std::vector<uint32_t> kOrder = {0, 1, 2, 3};

} // namespace

// ── Lifetimes ───────────────────────────────────────────────────

TEST(AliasLifetimes, FromAccessesAndDescriptorSets) {
    json j = chain();
    // D also binds a through a descriptor set, so a lives to the end.
    j["descriptorSetLayouts"] = {{"late", {{"bindings", {
        {{"binding", 0}, {"type", "combined_image_sampler"}, {"name", "a"}, {"autoBindResource", "a"}}}}}}};
    j["passes"][3]["descriptorSets"] = {"late"};
    auto b = load(j);
    const auto spans = resourceLifetimes(b.getPassDeclarations(), b.getResourceDeclarations(),
                                         b.getDescriptorSetLayouts(), kOrder);
    EXPECT_EQ(spans[0].first, 0u);
    EXPECT_EQ(spans[0].last, 3u);
    EXPECT_EQ(spans[1].first, 1u);
    EXPECT_EQ(spans[1].last, 2u);
    EXPECT_TRUE(spans[0].overlaps(spans[1]));
}

TEST(AliasLifetimes, UnusedImagesOverlapNothing) {
    PassSpan unused, used;
    used.first = 0;
    used.last = 5;
    EXPECT_FALSE(unused.used());
    EXPECT_FALSE(unused.overlaps(used));
    EXPECT_FALSE(used.overlaps(unused));
}

// ── Eligibility ─────────────────────────────────────────────────

TEST(AliasEligibility, OnlyPlainTransientImages) {
    json j = chain();
    j["resources"][1]["image"]["persistent"] = true;
    j["resources"].push_back(image("saved"));
    j["resources"].back()["save"] = {{"path", "x.png"}};
    auto b = load(j);
    const auto ok = aliasableImages(b.getPassDeclarations(), b.getResourceDeclarations(),
                                    b.getDescriptorSetLayouts(), kOrder);
    EXPECT_TRUE(ok[0]);   // a
    EXPECT_FALSE(ok[1]);  // persistent
    EXPECT_TRUE(ok[2]);   // c
    EXPECT_FALSE(ok[3]);  // not transient
    EXPECT_FALSE(ok[4]);  // saved
}

TEST(AliasEligibility, NotWhatCodeTheGraphCannotSeeUses) {
    json j = chain();
    j["passes"][1]["execution"] = {{"type", "manual"}};  // B uses a and b
    j["passes"][2]["queue"] = "compute";                // C uses b and c
    auto b = load(j);
    const auto ok = aliasableImages(b.getPassDeclarations(), b.getResourceDeclarations(),
                                    b.getDescriptorSetLayouts(), kOrder);
    EXPECT_FALSE(ok[0]);
    EXPECT_FALSE(ok[1]);
    EXPECT_FALSE(ok[2]);
}

// ── Packing ─────────────────────────────────────────────────────

TEST(AliasPacking, DisjointLifetimesShareABlock) {
    std::vector<PassSpan> spans(3);
    spans[0] = {0, 1};
    spans[1] = {1, 2};
    spans[2] = {2, 3};
    const auto blocks = packAliases({{0, 100, 64, 0b11}, {1, 300, 256, 0b11}, {2, 200, 64, 0b01}}, spans);
    // b (largest) first; a overlaps b at pass 1; c overlaps b at pass 2.
    // a and c do not overlap: they share the second block.
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].resources, (std::vector<uint32_t>{1}));
    EXPECT_EQ(blocks[1].resources, (std::vector<uint32_t>{2, 0}));
    EXPECT_EQ(blocks[1].size, 200u);
    EXPECT_EQ(blocks[1].alignment, 64u);
    EXPECT_EQ(blocks[1].memoryTypeBits, 0b01u);
}

TEST(AliasPacking, NoCommonMemoryTypeMeansSeparateBlocks) {
    std::vector<PassSpan> spans(2);
    spans[0] = {0, 0};
    spans[1] = {1, 1};
    const auto blocks = packAliases({{0, 100, 64, 0b01}, {1, 100, 64, 0b10}}, spans);
    EXPECT_EQ(blocks.size(), 2u);
}

// ── Barriers ────────────────────────────────────────────────────

TEST(AliasBarriers, TakingOverWaitsForThePreviousImage) {
    auto b = load(chain());
    auto resources = describeResources(b.getResourceDeclarations());
    resources[0].aliasBlock = 0;  // a and c share a block
    resources[2].aliasBlock = 0;
    const auto plan = planBarriers(b.getPassDeclarations(), kOrder, resources);

    // C writes c after B last read a: c's barrier starts from UNDEFINED and
    // waits for that read.
    const BarrierInfo* cWrite = nullptr;
    for (const auto& barrier : plan.preBarriers[2]) {
        if (barrier.resource.index == 2) cWrite = &barrier;
    }
    ASSERT_NE(cWrite, nullptr);
    EXPECT_EQ(cWrite->oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(cWrite->srcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));

    // Next frame, A writes a after D's fragment shader last read c.
    const BarrierInfo* aWrite = nullptr;
    for (const auto& barrier : plan.preBarriers[0]) {
        if (barrier.resource.index == 0) aWrite = &barrier;
    }
    ASSERT_NE(aWrite, nullptr);
    EXPECT_EQ(aWrite->oldLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(aWrite->srcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT));
}

TEST(AliasBarriers, WithoutABlockEachImageWaitsOnItself) {
    auto b = load(chain());
    const auto resources = describeResources(b.getResourceDeclarations());
    const auto plan = planBarriers(b.getPassDeclarations(), kOrder, resources);
    // A's write of a waits for B's compute read of a in the previous frame.
    const BarrierInfo* aWrite = nullptr;
    for (const auto& barrier : plan.preBarriers[0]) {
        if (barrier.resource.index == 0) aWrite = &barrier;
    }
    ASSERT_NE(aWrite, nullptr);
    EXPECT_EQ(aWrite->srcStage, static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
}
