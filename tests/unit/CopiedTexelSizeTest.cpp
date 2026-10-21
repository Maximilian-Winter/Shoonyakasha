//
// CopiedTexelSizeTest.cpp - bytes per texel an image-to-buffer copy writes
//
// Tier 1: no GPU.
//

#include <gtest/gtest.h>

#include "GPU/VkFormatUtils.h"
#include "Vulkan/FrameGraph/FrameGraphJson.h"

using namespace Shoonyakasha;

TEST(CopiedTexelSize, EveryPipelineFormatHasOne) {
    const std::pair<const char*, uint32_t> formats[] = {
        {"R8_UNORM", 1}, {"R8G8_UNORM", 2}, {"R8G8B8_UNORM", 3}, {"R8G8B8A8_UNORM", 4}, {"B8G8R8A8_UNORM", 4},
        {"R8G8B8A8_SRGB", 4}, {"B8G8R8A8_SRGB", 4}, {"R8_SNORM", 1}, {"R8G8_SNORM", 2}, {"R8G8B8A8_SNORM", 4},
        {"R16_SFLOAT", 2}, {"R16G16_SFLOAT", 4}, {"R16G16B16_SFLOAT", 6}, {"R16G16B16A16_SFLOAT", 8},
        {"R32_SFLOAT", 4}, {"R32G32_SFLOAT", 8}, {"R32G32B32_SFLOAT", 12}, {"R32G32B32A32_SFLOAT", 16},
        {"R8_UINT", 1}, {"R16_UINT", 2}, {"R32_UINT", 4}, {"R32G32_UINT", 8}, {"R32G32B32A32_UINT", 16},
        {"R8_SINT", 1}, {"R16_SINT", 2}, {"R32_SINT", 4}, {"R16_UNORM", 2}, {"R16G16_UNORM", 4},
        {"R16G16B16A16_UNORM", 8}, {"B10G11R11_UFLOAT_PACK32", 4}, {"A2B10G10R10_UNORM_PACK32", 4},
        // Depth formats: the depth aspect alone.
        {"D16_UNORM", 2}, {"D32_SFLOAT", 4}, {"D16_UNORM_S8_UINT", 2}, {"D24_UNORM_S8_UINT", 4},
        {"D32_SFLOAT_S8_UINT", 4},
    };
    for (const auto& [name, size] : formats) {
        EXPECT_EQ(copiedTexelSize(FrameGraph::JsonUtils::stringToFormat(name)), size) << name;
    }
    EXPECT_EQ(copiedTexelSize(VK_FORMAT_UNDEFINED), 0u);
    EXPECT_EQ(copiedTexelSize(VK_FORMAT_BC7_UNORM_BLOCK), 0u);
}
