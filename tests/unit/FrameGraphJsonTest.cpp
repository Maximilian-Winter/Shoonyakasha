//
// FrameGraphJsonTest.cpp - Tests for string ↔ enum conversions in FrameGraphJson
//
// Tier 1: Pure unit tests — no GPU context, no ECS
// Uses parametrized tests for comprehensive table coverage.
//

#include <gtest/gtest.h>
#include "Vulkan/FrameGraph/FrameGraphJson.h"

using namespace Shoonyakasha::FrameGraph;

// ═══════════════════════════════════════════════════════════════
// VkFormat Parametrized Tests
// ═══════════════════════════════════════════════════════════════

struct FormatPair {
    std::string name;
    VkFormat format;
};

class FormatConversion : public testing::TestWithParam<FormatPair> {};

TEST_P(FormatConversion, StringToFormat) {
    auto [name, format] = GetParam();
    EXPECT_EQ(JsonUtils::stringToFormat(name), format);
}

TEST_P(FormatConversion, FormatToString_RoundTrip) {
    auto [name, format] = GetParam();
    // formatToString → stringToFormat should give the same enum
    auto str = JsonUtils::formatToString(format);
    EXPECT_EQ(JsonUtils::stringToFormat(str), format);
}

INSTANTIATE_TEST_SUITE_P(VkFormats, FormatConversion, testing::Values(
    // Unsigned normalized
    FormatPair{"R8_UNORM",             VK_FORMAT_R8_UNORM},
    FormatPair{"R8G8_UNORM",           VK_FORMAT_R8G8_UNORM},
    FormatPair{"R8G8B8_UNORM",         VK_FORMAT_R8G8B8_UNORM},
    FormatPair{"R8G8B8A8_UNORM",       VK_FORMAT_R8G8B8A8_UNORM},
    FormatPair{"B8G8R8A8_UNORM",       VK_FORMAT_B8G8R8A8_UNORM},
    // SRGB
    FormatPair{"R8G8B8A8_SRGB",        VK_FORMAT_R8G8B8A8_SRGB},
    FormatPair{"B8G8R8A8_SRGB",        VK_FORMAT_B8G8R8A8_SRGB},
    // Signed normalized
    FormatPair{"R8_SNORM",             VK_FORMAT_R8_SNORM},
    FormatPair{"R8G8_SNORM",           VK_FORMAT_R8G8_SNORM},
    FormatPair{"R8G8B8A8_SNORM",       VK_FORMAT_R8G8B8A8_SNORM},
    // Float 16
    FormatPair{"R16_SFLOAT",           VK_FORMAT_R16_SFLOAT},
    FormatPair{"R16G16_SFLOAT",        VK_FORMAT_R16G16_SFLOAT},
    FormatPair{"R16G16B16_SFLOAT",     VK_FORMAT_R16G16B16_SFLOAT},
    FormatPair{"R16G16B16A16_SFLOAT",  VK_FORMAT_R16G16B16A16_SFLOAT},
    // Float 32
    FormatPair{"R32_SFLOAT",           VK_FORMAT_R32_SFLOAT},
    FormatPair{"R32G32_SFLOAT",        VK_FORMAT_R32G32_SFLOAT},
    FormatPair{"R32G32B32_SFLOAT",     VK_FORMAT_R32G32B32_SFLOAT},
    FormatPair{"R32G32B32A32_SFLOAT",  VK_FORMAT_R32G32B32A32_SFLOAT},
    // Integer
    FormatPair{"R8_UINT",              VK_FORMAT_R8_UINT},
    FormatPair{"R16_UINT",             VK_FORMAT_R16_UINT},
    FormatPair{"R32_UINT",             VK_FORMAT_R32_UINT},
    FormatPair{"R8_SINT",              VK_FORMAT_R8_SINT},
    FormatPair{"R16_SINT",             VK_FORMAT_R16_SINT},
    FormatPair{"R32_SINT",             VK_FORMAT_R32_SINT},
    // Unsigned normalized 16-bit
    FormatPair{"R16_UNORM",            VK_FORMAT_R16_UNORM},
    FormatPair{"R16G16_UNORM",         VK_FORMAT_R16G16_UNORM},
    FormatPair{"R16G16B16A16_UNORM",   VK_FORMAT_R16G16B16A16_UNORM},
    // Depth
    FormatPair{"D16_UNORM",            VK_FORMAT_D16_UNORM},
    FormatPair{"D32_SFLOAT",           VK_FORMAT_D32_SFLOAT},
    FormatPair{"D16_UNORM_S8_UINT",    VK_FORMAT_D16_UNORM_S8_UINT},
    FormatPair{"D24_UNORM_S8_UINT",    VK_FORMAT_D24_UNORM_S8_UINT},
    FormatPair{"D32_SFLOAT_S8_UINT",   VK_FORMAT_D32_SFLOAT_S8_UINT},
    // Special
    FormatPair{"UNDEFINED",            VK_FORMAT_UNDEFINED}
));

TEST(FrameGraphJson, StringToFormat_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToFormat("NONEXISTENT"), std::runtime_error);
}

TEST(FrameGraphJson, FormatToString_UnknownEnum_ReturnsUndefined) {
    // An unmapped VkFormat value should return "UNDEFINED"
    EXPECT_EQ(JsonUtils::formatToString(static_cast<VkFormat>(99999)), "UNDEFINED");
}

// ═══════════════════════════════════════════════════════════════
// ResourceUsage Tests
// ═══════════════════════════════════════════════════════════════

struct UsagePair {
    std::string name;
    ResourceUsage usage;
};

class ResourceUsageConversion : public testing::TestWithParam<UsagePair> {};

TEST_P(ResourceUsageConversion, StringToUsage) {
    auto [name, usage] = GetParam();
    EXPECT_EQ(JsonUtils::stringToResourceUsage(name), usage);
}

INSTANTIATE_TEST_SUITE_P(ResourceUsages, ResourceUsageConversion, testing::Values(
    // Primary names
    UsagePair{"color_write",           ResourceUsage::ColorAttachmentWrite},
    UsagePair{"color_attachment_write", ResourceUsage::ColorAttachmentWrite},
    UsagePair{"color_blend",           ResourceUsage::ColorAttachmentBlend},
    UsagePair{"color_attachment_blend", ResourceUsage::ColorAttachmentBlend},
    UsagePair{"depth_write",           ResourceUsage::DepthStencilWrite},
    UsagePair{"depth_stencil_write",   ResourceUsage::DepthStencilWrite},
    UsagePair{"depth_read",            ResourceUsage::DepthStencilReadOnly},
    UsagePair{"shader_read",           ResourceUsage::ShaderReadOnly},
    UsagePair{"shader_read_write",     ResourceUsage::ShaderReadWrite},
    UsagePair{"storage_image_write",   ResourceUsage::StorageImageWrite},
    UsagePair{"input_attachment",      ResourceUsage::InputAttachment},
    UsagePair{"transfer_src",          ResourceUsage::TransferSrc},
    UsagePair{"transfer_dst",          ResourceUsage::TransferDst},
    UsagePair{"present",               ResourceUsage::Present}
));

TEST(FrameGraphJson, ResourceUsageToString_RoundTrips) {
    // Test that toString → fromString gives the same enum for canonical names
    auto str = JsonUtils::resourceUsageToString(ResourceUsage::ColorAttachmentWrite);
    EXPECT_EQ(JsonUtils::stringToResourceUsage(str), ResourceUsage::ColorAttachmentWrite);
}

TEST(FrameGraphJson, StringToResourceUsage_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToResourceUsage("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// PassType Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToPassType_Graphics) {
    EXPECT_EQ(JsonUtils::stringToPassType("graphics"), PassType::Graphics);
}

TEST(FrameGraphJson, StringToPassType_Compute) {
    EXPECT_EQ(JsonUtils::stringToPassType("compute"), PassType::Compute);
}

TEST(FrameGraphJson, StringToPassType_Transfer) {
    EXPECT_EQ(JsonUtils::stringToPassType("transfer"), PassType::Transfer);
}

TEST(FrameGraphJson, StringToPassType_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToPassType("bogus"), std::runtime_error);
}

TEST(FrameGraphJson, PassTypeToString_RoundTrip) {
    EXPECT_EQ(JsonUtils::passTypeToString(PassType::Graphics), "graphics");
    EXPECT_EQ(JsonUtils::passTypeToString(PassType::Compute), "compute");
    EXPECT_EQ(JsonUtils::passTypeToString(PassType::Transfer), "transfer");
}

// ═══════════════════════════════════════════════════════════════
// ResourceKind Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToResourceKind_Image) {
    EXPECT_EQ(JsonUtils::stringToResourceKind("image"), ResourceKind::Image);
}

TEST(FrameGraphJson, StringToResourceKind_Buffer) {
    EXPECT_EQ(JsonUtils::stringToResourceKind("buffer"), ResourceKind::Buffer);
}

TEST(FrameGraphJson, StringToResourceKind_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToResourceKind("bogus"), std::runtime_error);
}

TEST(FrameGraphJson, ResourceKindToString_RoundTrip) {
    EXPECT_EQ(JsonUtils::resourceKindToString(ResourceKind::Image), "image");
    EXPECT_EQ(JsonUtils::resourceKindToString(ResourceKind::Buffer), "buffer");
}

// ═══════════════════════════════════════════════════════════════
// DescriptorType Tests
// ═══════════════════════════════════════════════════════════════

struct DescriptorPair {
    std::string name;
    VkDescriptorType type;
};

class DescriptorTypeConversion : public testing::TestWithParam<DescriptorPair> {};

TEST_P(DescriptorTypeConversion, StringToDescriptorType) {
    auto [name, type] = GetParam();
    EXPECT_EQ(JsonUtils::stringToDescriptorType(name), type);
}

INSTANTIATE_TEST_SUITE_P(DescriptorTypes, DescriptorTypeConversion, testing::Values(
    DescriptorPair{"uniform_buffer",         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
    DescriptorPair{"uniform_buffer_dynamic", VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
    DescriptorPair{"storage_buffer",         VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    DescriptorPair{"storage_buffer_dynamic", VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC},
    DescriptorPair{"combined_image_sampler", VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
    DescriptorPair{"sampled_image",          VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
    DescriptorPair{"storage_image",          VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    DescriptorPair{"input_attachment",       VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT},
    DescriptorPair{"sampler",                VK_DESCRIPTOR_TYPE_SAMPLER}
));

TEST(FrameGraphJson, DescriptorTypeToString_RoundTrip) {
    auto str = JsonUtils::descriptorTypeToString(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_EQ(JsonUtils::stringToDescriptorType(str), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
}

TEST(FrameGraphJson, StringToDescriptorType_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToDescriptorType("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// QueueType Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToQueueType_Compute) {
    EXPECT_EQ(JsonUtils::stringToQueueType("compute"), QueueType::Compute);
}

TEST(FrameGraphJson, StringToQueueType_Graphics) {
    EXPECT_EQ(JsonUtils::stringToQueueType("graphics"), QueueType::Graphics);
}

TEST(FrameGraphJson, StringToQueueType_Unknown_DefaultsGraphics) {
    // Unknown queue type defaults to Graphics
    EXPECT_EQ(JsonUtils::stringToQueueType("bogus"), QueueType::Graphics);
}

TEST(FrameGraphJson, QueueTypeToString_RoundTrip) {
    EXPECT_EQ(JsonUtils::queueTypeToString(QueueType::Graphics), "graphics");
    EXPECT_EQ(JsonUtils::queueTypeToString(QueueType::Compute), "compute");
}

// ═══════════════════════════════════════════════════════════════
// Shader Stage Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToShaderStage_Vertex) {
    EXPECT_EQ(JsonUtils::stringToShaderStage("vertex"), VK_SHADER_STAGE_VERTEX_BIT);
}

TEST(FrameGraphJson, StringToShaderStage_Fragment) {
    EXPECT_EQ(JsonUtils::stringToShaderStage("fragment"), VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST(FrameGraphJson, StringToShaderStage_Compute) {
    EXPECT_EQ(JsonUtils::stringToShaderStage("compute"), VK_SHADER_STAGE_COMPUTE_BIT);
}

TEST(FrameGraphJson, StringToShaderStage_All) {
    EXPECT_EQ(JsonUtils::stringToShaderStage("all"), VK_SHADER_STAGE_ALL);
}

TEST(FrameGraphJson, StringToShaderStage_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToShaderStage("bogus"), std::runtime_error);
}

TEST(FrameGraphJson, StringsToShaderStages_Combined) {
    auto flags = JsonUtils::stringsToShaderStages({"vertex", "fragment"});
    EXPECT_EQ(flags, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
}

TEST(FrameGraphJson, StringsToShaderStages_Empty) {
    auto flags = JsonUtils::stringsToShaderStages({});
    EXPECT_EQ(flags, 0u);
}

// ═══════════════════════════════════════════════════════════════
// Sampler Filter Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToFilter_Linear) {
    EXPECT_EQ(JsonUtils::stringToFilter("linear"), VK_FILTER_LINEAR);
}

TEST(FrameGraphJson, StringToFilter_Nearest) {
    EXPECT_EQ(JsonUtils::stringToFilter("nearest"), VK_FILTER_NEAREST);
}

TEST(FrameGraphJson, StringToFilter_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToFilter("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Sampler Mipmap Mode Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToMipmapMode_Linear) {
    EXPECT_EQ(JsonUtils::stringToMipmapMode("linear"), VK_SAMPLER_MIPMAP_MODE_LINEAR);
}

TEST(FrameGraphJson, StringToMipmapMode_Nearest) {
    EXPECT_EQ(JsonUtils::stringToMipmapMode("nearest"), VK_SAMPLER_MIPMAP_MODE_NEAREST);
}

TEST(FrameGraphJson, StringToMipmapMode_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToMipmapMode("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Sampler Address Mode Tests
// ═══════════════════════════════════════════════════════════════

struct AddressModePair {
    std::string name;
    VkSamplerAddressMode mode;
};

class AddressModeConversion : public testing::TestWithParam<AddressModePair> {};

TEST_P(AddressModeConversion, StringToAddressMode) {
    auto [name, mode] = GetParam();
    EXPECT_EQ(JsonUtils::stringToAddressMode(name), mode);
}

INSTANTIATE_TEST_SUITE_P(AddressModes, AddressModeConversion, testing::Values(
    AddressModePair{"repeat",               VK_SAMPLER_ADDRESS_MODE_REPEAT},
    AddressModePair{"mirrored_repeat",      VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT},
    AddressModePair{"clamp_to_edge",        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
    AddressModePair{"clamp_to_border",      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER},
    AddressModePair{"mirror_clamp_to_edge", VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE}
));

TEST(FrameGraphJson, StringToAddressMode_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToAddressMode("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Border Color Tests
// ═══════════════════════════════════════════════════════════════

TEST(FrameGraphJson, StringToBorderColor_FloatOpaqueBlack) {
    EXPECT_EQ(JsonUtils::stringToBorderColor("float_opaque_black"), VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
}

TEST(FrameGraphJson, StringToBorderColor_FloatOpaqueWhite) {
    EXPECT_EQ(JsonUtils::stringToBorderColor("float_opaque_white"), VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
}

TEST(FrameGraphJson, StringToBorderColor_FloatTransparentBlack) {
    EXPECT_EQ(JsonUtils::stringToBorderColor("float_transparent_black"), VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
}

TEST(FrameGraphJson, StringToBorderColor_Shorthand_Black) {
    EXPECT_EQ(JsonUtils::stringToBorderColor("black"), VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
}

TEST(FrameGraphJson, StringToBorderColor_Shorthand_White) {
    EXPECT_EQ(JsonUtils::stringToBorderColor("white"), VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE);
}

TEST(FrameGraphJson, StringToBorderColor_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToBorderColor("bogus"), std::runtime_error);
}

// ═══════════════════════════════════════════════════════════════
// Compare Op Tests
// ═══════════════════════════════════════════════════════════════

struct CompareOpPair {
    std::string name;
    VkCompareOp op;
};

class CompareOpConversion : public testing::TestWithParam<CompareOpPair> {};

TEST_P(CompareOpConversion, StringToCompareOp) {
    auto [name, op] = GetParam();
    EXPECT_EQ(JsonUtils::stringToCompareOp(name), op);
}

INSTANTIATE_TEST_SUITE_P(CompareOps, CompareOpConversion, testing::Values(
    CompareOpPair{"never",            VK_COMPARE_OP_NEVER},
    CompareOpPair{"less",             VK_COMPARE_OP_LESS},
    CompareOpPair{"equal",            VK_COMPARE_OP_EQUAL},
    CompareOpPair{"less_or_equal",    VK_COMPARE_OP_LESS_OR_EQUAL},
    CompareOpPair{"greater",          VK_COMPARE_OP_GREATER},
    CompareOpPair{"not_equal",        VK_COMPARE_OP_NOT_EQUAL},
    CompareOpPair{"greater_or_equal", VK_COMPARE_OP_GREATER_OR_EQUAL},
    CompareOpPair{"always",           VK_COMPARE_OP_ALWAYS}
));

TEST(FrameGraphJson, StringToCompareOp_Unknown_Throws) {
    EXPECT_THROW(JsonUtils::stringToCompareOp("bogus"), std::runtime_error);
}
