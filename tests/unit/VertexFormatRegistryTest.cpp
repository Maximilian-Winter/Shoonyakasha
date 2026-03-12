//
// VertexFormatRegistryTest.cpp - Tests for vertex format registration and lookup
//
// Tier 1: Pure unit tests — no GPU context
//

#include <gtest/gtest.h>
#include "Vulkan/FrameGraph/VertexFormatRegistry.h"

using namespace Shoonyakasha::FrameGraph;

// ═══════════════════════════════════════════════════════════════
// Type String Conversion Tests
// ═══════════════════════════════════════════════════════════════

TEST(VertexFormatRegistry, TypeStringToVkFormat_Float) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("float"), VK_FORMAT_R32_SFLOAT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_Vec2) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("vec2"), VK_FORMAT_R32G32_SFLOAT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_Vec3) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("vec3"), VK_FORMAT_R32G32B32_SFLOAT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_Vec4) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("vec4"), VK_FORMAT_R32G32B32A32_SFLOAT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_Int) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("int"), VK_FORMAT_R32_SINT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_UVec4) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("uvec4"), VK_FORMAT_R32G32B32A32_UINT);
}

TEST(VertexFormatRegistry, TypeStringToVkFormat_Unknown) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToVkFormat("bogus"), VK_FORMAT_UNDEFINED);
}

TEST(VertexFormatRegistry, TypeStringToSize_Float) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToSize("float"), 4u);
}

TEST(VertexFormatRegistry, TypeStringToSize_Vec3) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToSize("vec3"), 12u);
}

TEST(VertexFormatRegistry, TypeStringToSize_Vec4) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToSize("vec4"), 16u);
}

TEST(VertexFormatRegistry, TypeStringToSize_UVec4) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToSize("uvec4"), 16u);
}

TEST(VertexFormatRegistry, TypeStringToSize_Unknown) {
    EXPECT_EQ(VertexFormatRegistry::typeStringToSize("bogus"), 0u);
}

// ═══════════════════════════════════════════════════════════════
// Registration and Lookup Tests
// ═══════════════════════════════════════════════════════════════

static VertexFormatDeclaration makeStandardFormat() {
    VertexFormatDeclaration fmt;
    fmt.name = "standard";
    fmt.attributes = {
        {"position", "vec3", 0, VK_FORMAT_UNDEFINED, 0, 0},
        {"normal",   "vec3", 1, VK_FORMAT_UNDEFINED, 0, 0},
        {"texCoord", "vec2", 2, VK_FORMAT_UNDEFINED, 0, 0}
    };
    return fmt;
}

TEST(VertexFormatRegistry, RegisterAndGet_RoundTrip) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());

    const auto* fmt = reg.getFormat("standard");
    ASSERT_NE(fmt, nullptr);
    EXPECT_EQ(fmt->name, "standard");
    EXPECT_EQ(fmt->attributes.size(), 3u);
}

TEST(VertexFormatRegistry, HasFormat_Registered_True) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());
    EXPECT_TRUE(reg.hasFormat("standard"));
}

TEST(VertexFormatRegistry, HasFormat_Unregistered_False) {
    VertexFormatRegistry reg;
    EXPECT_FALSE(reg.hasFormat("nonexistent"));
}

TEST(VertexFormatRegistry, GetFormat_Unregistered_ReturnsNull) {
    VertexFormatRegistry reg;
    EXPECT_EQ(reg.getFormat("missing"), nullptr);
}

TEST(VertexFormatRegistry, RegisterFormat_ComputesOffsetsAndStride) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());

    const auto* fmt = reg.getFormat("standard");
    ASSERT_NE(fmt, nullptr);

    // pos(vec3=12) + normal(vec3=12) + texCoord(vec2=8) = 32
    EXPECT_EQ(fmt->stride, 32u);
    EXPECT_EQ(fmt->attributes[0].offset, 0u);   // position
    EXPECT_EQ(fmt->attributes[1].offset, 12u);  // normal
    EXPECT_EQ(fmt->attributes[2].offset, 24u);  // texCoord
}

TEST(VertexFormatRegistry, RegisterFormat_SetsVkFormats) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());

    const auto* fmt = reg.getFormat("standard");
    ASSERT_NE(fmt, nullptr);

    EXPECT_EQ(fmt->attributes[0].vkFormat, VK_FORMAT_R32G32B32_SFLOAT);    // vec3
    EXPECT_EQ(fmt->attributes[1].vkFormat, VK_FORMAT_R32G32B32_SFLOAT);    // vec3
    EXPECT_EQ(fmt->attributes[2].vkFormat, VK_FORMAT_R32G32_SFLOAT);       // vec2
}

TEST(VertexFormatRegistry, GetVertexInputDescriptions_Correct) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());

    VkVertexInputBindingDescription binding{};
    std::vector<VkVertexInputAttributeDescription> attrs;

    EXPECT_TRUE(reg.getVertexInputDescriptions("standard", binding, attrs));
    EXPECT_EQ(binding.stride, 32u);
    EXPECT_EQ(binding.inputRate, VK_VERTEX_INPUT_RATE_VERTEX);
    EXPECT_EQ(attrs.size(), 3u);

    EXPECT_EQ(attrs[0].location, 0u);
    EXPECT_EQ(attrs[0].format, VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(attrs[0].offset, 0u);

    EXPECT_EQ(attrs[1].location, 1u);
    EXPECT_EQ(attrs[1].offset, 12u);

    EXPECT_EQ(attrs[2].location, 2u);
    EXPECT_EQ(attrs[2].format, VK_FORMAT_R32G32_SFLOAT);
    EXPECT_EQ(attrs[2].offset, 24u);
}

TEST(VertexFormatRegistry, GetVertexInputDescriptions_NotFound_ReturnsFalse) {
    VertexFormatRegistry reg;
    VkVertexInputBindingDescription binding{};
    std::vector<VkVertexInputAttributeDescription> attrs;
    EXPECT_FALSE(reg.getVertexInputDescriptions("missing", binding, attrs));
}

TEST(VertexFormatRegistry, Clear_RemovesAll) {
    VertexFormatRegistry reg;
    reg.registerFormat(makeStandardFormat());
    EXPECT_TRUE(reg.hasFormat("standard"));

    reg.clear();
    EXPECT_FALSE(reg.hasFormat("standard"));
}

TEST(VertexFormatRegistry, GetFormatNames_ListsAll) {
    VertexFormatRegistry reg;

    auto standard = makeStandardFormat();
    reg.registerFormat(standard);

    VertexFormatDeclaration skinned;
    skinned.name = "skinned";
    skinned.attributes = {
        {"position", "vec3", 0, VK_FORMAT_UNDEFINED, 0, 0},
        {"joints",   "uvec4", 1, VK_FORMAT_UNDEFINED, 0, 0}
    };
    reg.registerFormat(skinned);

    auto names = reg.getFormatNames();
    EXPECT_EQ(names.size(), 2u);

    // Check both names present (order not guaranteed)
    bool hasStandard = false, hasSkinned = false;
    for (const auto& n : names) {
        if (n == "standard") hasStandard = true;
        if (n == "skinned") hasSkinned = true;
    }
    EXPECT_TRUE(hasStandard);
    EXPECT_TRUE(hasSkinned);
}
