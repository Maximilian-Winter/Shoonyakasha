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

/// True for the integer colour formats (UINT, SINT), which a multisample
/// resolve cannot average.
constexpr bool isIntegerFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UINT: case VK_FORMAT_R8_SINT:
        case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT:
        case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R16_UINT: case VK_FORMAT_R16_SINT:
        case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT:
        case VK_FORMAT_R16G16B16A16_UINT: case VK_FORMAT_R16G16B16A16_SINT:
        case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT:
        case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R32G32B32A32_UINT: case VK_FORMAT_R32G32B32A32_SINT:
        case VK_FORMAT_A2B10G10R10_UINT_PACK32:
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

/// One unsigned float of a packed format: `mantissaBits` of mantissa above
/// five of exponent with a bias of 15, as in B10G11R11_UFLOAT_PACK32.
inline float unpackUnsignedSmallFloat(uint32_t bits, uint32_t mantissaBits) {
    const uint32_t mantissa = bits & ((1u << mantissaBits) - 1u);
    const uint32_t exponent = (bits >> mantissaBits) & 0x1Fu;
    const float fraction = static_cast<float>(mantissa) / static_cast<float>(1u << mantissaBits);
    if (exponent == 0) return fraction * (1.0f / 16384.0f);  // denormal: 2^-14
    if (exponent == 31) return mantissa == 0 ? 3.4028235e38f : 0.0f;  // infinity clamps, NaN reads 0
    const float scale = exponent >= 15 ? static_cast<float>(1u << (exponent - 15))
                                       : 1.0f / static_cast<float>(1u << (15 - exponent));
    return (1.0f + fraction) * scale;
}

/// A B10G11R11_UFLOAT_PACK32 texel as rgb: R in bits 0-10, G in 11-21, B in 22-31.
inline void unpackB10G11R11(uint32_t texel, float rgb[3]) {
    rgb[0] = unpackUnsignedSmallFloat(texel & 0x7FFu, 6);
    rgb[1] = unpackUnsignedSmallFloat((texel >> 11) & 0x7FFu, 6);
    rgb[2] = unpackUnsignedSmallFloat((texel >> 22) & 0x3FFu, 5);
}

/// An A2B10G10R10_UNORM_PACK32 texel as rgba in 0..1: R in bits 0-9, G in
/// 10-19, B in 20-29, A in 30-31.
inline void unpackA2B10G10R10(uint32_t texel, float rgba[4]) {
    rgba[0] = static_cast<float>(texel & 0x3FFu) / 1023.0f;
    rgba[1] = static_cast<float>((texel >> 10) & 0x3FFu) / 1023.0f;
    rgba[2] = static_cast<float>((texel >> 20) & 0x3FFu) / 1023.0f;
    rgba[3] = static_cast<float>(texel >> 30) / 3.0f;
}

} // namespace Shoonyakasha
