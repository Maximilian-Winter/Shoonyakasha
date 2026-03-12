//
// RenderTargetSaver — Implementation
//
// Captures render target images from GPU to disk via staging buffer readback.
// Supports PNG, JPG, BMP, TGA (8-bit), and HDR (32-bit float).
//

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "Vulkan/FrameGraph/RenderTargetSaver.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Core/Logger.h"

#include <filesystem>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace Shoonyakasha {
namespace FrameGraph {

// ═══════════════════════════════════════════════════════════════
// Half-float (IEEE 754 binary16) to float32 conversion
// ═══════════════════════════════════════════════════════════════

static float halfToFloat(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;

    if (exponent == 0) {
        if (mantissa == 0) {
            // Signed zero
            float result;
            std::memcpy(&result, &sign, 4);
            return result;
        } else {
            // Denormalized: convert to normalized float
            while (!(mantissa & 0x400u)) {
                mantissa <<= 1;
                exponent--;
            }
            exponent++;
            mantissa &= 0x3FFu;
            exponent += 112; // Rebias: -14 + 127 = 113, but we decremented
            uint32_t f = sign | (exponent << 23) | (mantissa << 13);
            float result;
            std::memcpy(&result, &f, 4);
            return result;
        }
    } else if (exponent == 31) {
        // Inf or NaN
        uint32_t f = sign | (0xFFu << 23) | (mantissa << 13);
        float result;
        std::memcpy(&result, &f, 4);
        return result;
    } else {
        // Normalized
        exponent += 112; // Rebias: 15 -> 127
        uint32_t f = sign | (exponent << 23) | (mantissa << 13);
        float result;
        std::memcpy(&result, &f, 4);
        return result;
    }
}

// ═══════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════

RenderTargetSaver::RenderTargetSaver(VulkanDevice& device)
    : m_device(device) {}

// ═══════════════════════════════════════════════════════════════
// Main save method
// ═══════════════════════════════════════════════════════════════

bool RenderTargetSaver::save(VkImage image, VkFormat format, VkExtent2D extent,
                              VkImageLayout currentLayout, const std::string& path,
                              Logger* logger) {
    if (image == VK_NULL_HANDLE) {
        if (logger) logger->log(LogLevel::Error, "RenderTargetSaver: null image handle");
        return false;
    }

    if (extent.width == 0 || extent.height == 0) {
        if (logger) logger->log(LogLevel::Error, "RenderTargetSaver: zero extent");
        return false;
    }

    // Determine output format from file extension
    std::string ext = getExtension(path);
    bool isHDR = (ext == ".hdr");

    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
        ext != ".bmp" && ext != ".tga" && ext != ".hdr") {
        if (logger) logger->log(LogLevel::Error, "RenderTargetSaver: unsupported format '%s'", ext.c_str());
        return false;
    }

    // Ensure output directory exists
    auto parentPath = std::filesystem::path(path).parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath);
    }

    // Calculate staging buffer size
    uint32_t bpp = getBytesPerPixel(format);
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(extent.width) * extent.height * bpp;

    if (logger) {
        logger->log(LogLevel::Info, "RenderTargetSaver: saving %ux%u image (format %d, %u bpp) to '%s'",
                    extent.width, extent.height, static_cast<int>(format), bpp, path.c_str());
    }

    // ── Step 1: Create staging buffer ──
    auto& allocator = m_device.getAllocator();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    auto stagingAlloc = allocator.createBuffer(
        bufferInfo,
        VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );

    if (!stagingAlloc.valid()) {
        if (logger) logger->log(LogLevel::Error, "RenderTargetSaver: failed to create staging buffer");
        return false;
    }

    // ── Step 2: Record and execute one-shot command buffer ──
    VkCommandBuffer cmd = m_device.beginSingleTimeCommands();

    // Determine aspect mask
    VkImageAspectFlags aspectMask = isDepthFormat(format)
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;

    // Transition: currentLayout -> TRANSFER_SRC_OPTIMAL
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = currentLayout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Copy image to staging buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {extent.width, extent.height, 1};

    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            stagingAlloc.buffer, 1, &region);

    // Transition: TRANSFER_SRC_OPTIMAL -> original layout
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = currentLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = aspectMask;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Submit and wait
    m_device.endSingleTimeCommands(cmd);

    // ── Step 3: Read back pixel data ──
    void* mappedData = stagingAlloc.allocationInfo.pMappedData;
    if (!mappedData) {
        mappedData = allocator.mapMemory(stagingAlloc.allocation);
    }

    if (!mappedData) {
        if (logger) logger->log(LogLevel::Error, "RenderTargetSaver: failed to map staging buffer");
        allocator.destroyBuffer(stagingAlloc);
        return false;
    }

    // ── Step 4: Convert and write ──
    bool success = false;

    if (isHDR) {
        // HDR output: convert to float RGBA
        std::vector<float> floatData = convertToRGBAFloat(mappedData, format,
                                                           extent.width, extent.height);
        success = stbi_write_hdr(path.c_str(), extent.width, extent.height, 4, floatData.data()) != 0;
    } else {
        // LDR output: convert to uint8 RGBA
        std::vector<uint8_t> rgba8 = convertToRGBA8(mappedData, format,
                                                      extent.width, extent.height);
        int channels = 4;

        if (ext == ".png") {
            int stride = extent.width * channels;
            success = stbi_write_png(path.c_str(), extent.width, extent.height, channels,
                                      rgba8.data(), stride) != 0;
        } else if (ext == ".jpg" || ext == ".jpeg") {
            success = stbi_write_jpg(path.c_str(), extent.width, extent.height, channels,
                                      rgba8.data(), 90) != 0;
        } else if (ext == ".bmp") {
            success = stbi_write_bmp(path.c_str(), extent.width, extent.height, channels,
                                      rgba8.data()) != 0;
        } else if (ext == ".tga") {
            success = stbi_write_tga(path.c_str(), extent.width, extent.height, channels,
                                      rgba8.data()) != 0;
        }
    }

    // ── Step 5: Cleanup ──
    // If we manually mapped, unmap (but VMA_ALLOCATION_CREATE_MAPPED_BIT keeps it mapped)
    allocator.destroyBuffer(stagingAlloc);

    if (logger) {
        if (success) {
            logger->log(LogLevel::Info, "RenderTargetSaver: saved '%s' (%ux%u)", path.c_str(),
                        extent.width, extent.height);
        } else {
            logger->log(LogLevel::Error, "RenderTargetSaver: failed to write '%s'", path.c_str());
        }
    }

    return success;
}

// ═══════════════════════════════════════════════════════════════
// Format conversion: GPU pixel data -> 8-bit RGBA
// ═══════════════════════════════════════════════════════════════

std::vector<uint8_t> RenderTargetSaver::convertToRGBA8(const void* data, VkFormat srcFormat,
                                                         uint32_t width, uint32_t height) {
    uint32_t pixelCount = width * height;
    std::vector<uint8_t> result(pixelCount * 4);

    switch (srcFormat) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: {
            // Direct copy
            std::memcpy(result.data(), data, pixelCount * 4);
            break;
        }

        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB: {
            // Swizzle B <-> R
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = src[i * 4 + 2]; // R <- B
                result[i * 4 + 1] = src[i * 4 + 1]; // G <- G
                result[i * 4 + 2] = src[i * 4 + 0]; // B <- R
                result[i * 4 + 3] = src[i * 4 + 3]; // A <- A
            }
            break;
        }

        case VK_FORMAT_R16G16B16A16_SFLOAT: {
            // Half-float -> uint8 with tone mapping (clamp to [0,1])
            const uint16_t* src = static_cast<const uint16_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                for (uint32_t c = 0; c < 4; c++) {
                    float val = halfToFloat(src[i * 4 + c]);
                    val = std::max(0.0f, std::min(1.0f, val));
                    result[i * 4 + c] = static_cast<uint8_t>(val * 255.0f + 0.5f);
                }
            }
            break;
        }

        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            // Float32 -> uint8
            const float* src = static_cast<const float*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                for (uint32_t c = 0; c < 4; c++) {
                    float val = std::max(0.0f, std::min(1.0f, src[i * 4 + c]));
                    result[i * 4 + c] = static_cast<uint8_t>(val * 255.0f + 0.5f);
                }
            }
            break;
        }

        case VK_FORMAT_R8G8_UNORM: {
            // 2-channel -> RGBA (RG, B=0, A=255)
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = src[i * 2 + 0];
                result[i * 4 + 1] = src[i * 2 + 1];
                result[i * 4 + 2] = 0;
                result[i * 4 + 3] = 255;
            }
            break;
        }

        case VK_FORMAT_R8_UNORM: {
            // Single channel -> RGBA grayscale
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = src[i];
                result[i * 4 + 1] = src[i];
                result[i * 4 + 2] = src[i];
                result[i * 4 + 3] = 255;
            }
            break;
        }

        case VK_FORMAT_D32_SFLOAT: {
            // Depth float -> grayscale
            const float* src = static_cast<const float*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                float val = std::max(0.0f, std::min(1.0f, src[i]));
                uint8_t byte = static_cast<uint8_t>(val * 255.0f + 0.5f);
                result[i * 4 + 0] = byte;
                result[i * 4 + 1] = byte;
                result[i * 4 + 2] = byte;
                result[i * 4 + 3] = 255;
            }
            break;
        }

        case VK_FORMAT_D24_UNORM_S8_UINT: {
            // Depth 24 + stencil 8 -> grayscale (use depth only)
            const uint32_t* src = static_cast<const uint32_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                float depth = static_cast<float>(src[i] & 0x00FFFFFFu) / 16777215.0f;
                uint8_t byte = static_cast<uint8_t>(depth * 255.0f + 0.5f);
                result[i * 4 + 0] = byte;
                result[i * 4 + 1] = byte;
                result[i * 4 + 2] = byte;
                result[i * 4 + 3] = 255;
            }
            break;
        }

        default: {
            // Unknown format — try to read as raw RGBA8
            std::memcpy(result.data(), data, std::min<size_t>(pixelCount * 4, pixelCount * getBytesPerPixel(srcFormat)));
            break;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Format conversion: GPU pixel data -> float RGBA (for HDR)
// ═══════════════════════════════════════════════════════════════

std::vector<float> RenderTargetSaver::convertToRGBAFloat(const void* data, VkFormat srcFormat,
                                                           uint32_t width, uint32_t height) {
    uint32_t pixelCount = width * height;
    std::vector<float> result(pixelCount * 4);

    switch (srcFormat) {
        case VK_FORMAT_R16G16B16A16_SFLOAT: {
            // Half-float -> float32
            const uint16_t* src = static_cast<const uint16_t*>(data);
            for (uint32_t i = 0; i < pixelCount * 4; i++) {
                result[i] = halfToFloat(src[i]);
            }
            break;
        }

        case VK_FORMAT_R32G32B32A32_SFLOAT: {
            // Direct copy
            std::memcpy(result.data(), data, pixelCount * 4 * sizeof(float));
            break;
        }

        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB: {
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint32_t i = 0; i < pixelCount * 4; i++) {
                result[i] = static_cast<float>(src[i]) / 255.0f;
            }
            break;
        }

        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB: {
            const uint8_t* src = static_cast<const uint8_t*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = static_cast<float>(src[i * 4 + 2]) / 255.0f; // R <- B
                result[i * 4 + 1] = static_cast<float>(src[i * 4 + 1]) / 255.0f;
                result[i * 4 + 2] = static_cast<float>(src[i * 4 + 0]) / 255.0f; // B <- R
                result[i * 4 + 3] = static_cast<float>(src[i * 4 + 3]) / 255.0f;
            }
            break;
        }

        case VK_FORMAT_D32_SFLOAT: {
            const float* src = static_cast<const float*>(data);
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = src[i];
                result[i * 4 + 1] = src[i];
                result[i * 4 + 2] = src[i];
                result[i * 4 + 3] = 1.0f;
            }
            break;
        }

        default: {
            // Unknown format — fill with magenta to signal error
            for (uint32_t i = 0; i < pixelCount; i++) {
                result[i * 4 + 0] = 1.0f;
                result[i * 4 + 1] = 0.0f;
                result[i * 4 + 2] = 1.0f;
                result[i * 4 + 3] = 1.0f;
            }
            break;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════
// Utility helpers
// ═══════════════════════════════════════════════════════════════

uint32_t RenderTargetSaver::getBytesPerPixel(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
            return 2;
        case VK_FORMAT_R8G8B8_UNORM:
        case VK_FORMAT_R8G8B8_SRGB:
        case VK_FORMAT_B8G8R8_UNORM:
        case VK_FORMAT_B8G8R8_SRGB:
            return 3;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return 16;
        default:
            return 4; // Assume 4 as safe default
    }
}

bool RenderTargetSaver::isDepthFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return true;
        default:
            return false;
    }
}

std::string RenderTargetSaver::getExtension(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

} // namespace FrameGraph
} // namespace Shoonyakasha
