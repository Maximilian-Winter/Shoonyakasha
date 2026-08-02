//
// VkFormatUtils.h — format classification shared by every layer that has to
// build a VkImageSubresourceRange.
//
// Placed under GPU/ because GPUResourceFactory.cpp is one of its consumers and
// the Vulkan -> GPU dependency edge already exists (FrameGraph.h includes
// GPU/GPUResourceFactory.h) while the reverse does not. The header itself
// depends on nothing but <vulkan/vulkan.h>.
//
// It replaces four hand-rolled copies of the depth-format list that had drifted
// apart — each was individually incomplete, and none matched the others.
//

#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Shoonyakasha {

/// True for the depth-only and depth+stencil formats.
constexpr bool isDepthFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

/// True for the stencil-only and depth+stencil formats.
constexpr bool isStencilFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_S8_UINT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

/// The aspect mask an image of this format is addressed by.
///
/// Getting this wrong is not a cosmetic error: a barrier whose aspectMask does
/// not match the image's format violates VUID-VkImageMemoryBarrier-image-01207
/// and the layout transition is simply not performed.
constexpr VkImageAspectFlags formatToAspectMask(VkFormat format) {
    VkImageAspectFlags aspect = 0;
    if (isDepthFormat(format))   aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (isStencilFormat(format)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
    return aspect != 0 ? aspect : VK_IMAGE_ASPECT_COLOR_BIT;
}

/// Subresource range covering an entire image.
///
/// mipLevels/arrayLayers default to 1 because that is all VulkanImage currently
/// creates; taking them as parameters means the mip and array work does not have
/// to revisit every call site.
constexpr VkImageSubresourceRange fullSubresourceRange(VkFormat format,
                                                       uint32_t mipLevels = 1,
                                                       uint32_t arrayLayers = 1) {
    VkImageSubresourceRange range{};
    range.aspectMask     = formatToAspectMask(format);
    range.baseMipLevel   = 0;
    range.levelCount     = mipLevels;
    range.baseArrayLayer = 0;
    range.layerCount     = arrayLayers;
    return range;
}

} // namespace Shoonyakasha
