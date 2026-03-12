//
// GPUResourceFactory.h - Simple factory functions for GPU resources
//
// These are THIN utility functions, not a universal abstraction.
// They create GPUBuffer and GPUTexture instances using VMA.
//
// Lifetime management is the caller's responsibility. Use destroy*()
// functions when done, or wrap in smart pointers with custom deleters.
//

#pragma once

#include "GPU/GPUTypes.h"
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstring>

// Forward declaration
namespace Shoonyakasha {
class VulkanDevice;
}

namespace Shoonyakasha {

// ============================================================================
// GPUResourceFactory - Static utility functions for resource creation
// ============================================================================

class GPUResourceFactory {
public:
    // ─── Buffer Creation ────────────────────────────────────────

    // Create a GPU buffer with the specified usage and memory properties
    static GPUBuffer createBuffer(
        VmaAllocator allocator,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY
    );

    // Create a vertex buffer and optionally upload data
    static GPUBuffer createVertexBuffer(
        VmaAllocator allocator,
        const void* data,
        VkDeviceSize size,
        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE,
        VmaAllocator stagingAllocator = nullptr  // If null, uses same allocator
    );

    // Create an index buffer and optionally upload data
    static GPUBuffer createIndexBuffer(
        VmaAllocator allocator,
        const void* data,
        VkDeviceSize size,
        IndexType indexType = IndexType::UInt32,
        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE
    );

    // Create a uniform buffer (host-visible for easy updates)
    static GPUBuffer createUniformBuffer(
        VmaAllocator allocator,
        VkDeviceSize size
    );

    // Create a storage buffer
    static GPUBuffer createStorageBuffer(
        VmaAllocator allocator,
        VkDeviceSize size,
        bool hostVisible = false
    );

    // ─── Buffer Operations ──────────────────────────────────────

    // Upload data to a buffer (uses staging if needed)
    static void uploadBuffer(
        VmaAllocator allocator,
        VkDevice device,
        VkQueue queue,
        VkCommandPool cmdPool,
        GPUBuffer& buffer,
        const void* data,
        VkDeviceSize size,
        VkDeviceSize offset = 0
    );

    // Map buffer memory for CPU access (only works for host-visible buffers)
    static void* mapBuffer(VmaAllocator allocator, GPUBuffer& buffer);
    static void unmapBuffer(VmaAllocator allocator, GPUBuffer& buffer);

    // Destroy a buffer and free its memory
    static void destroyBuffer(VmaAllocator allocator, GPUBuffer& buffer);

    // ─── Texture Creation ───────────────────────────────────────

    // Create a 2D texture with the specified format and dimensions
    static GPUTexture createTexture2D(
        VmaAllocator allocator,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        uint32_t mipLevels = 1,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
    );

    // Create a texture and upload pixel data
    static GPUTexture createTexture2DWithData(
        VmaAllocator allocator,
        VkDevice device,
        VkQueue queue,
        VkCommandPool cmdPool,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        const void* pixels,
        VkDeviceSize pixelDataSize,
        bool generateMipmaps = false
    );

    // Create a cubemap texture
    static GPUTexture createTextureCube(
        VmaAllocator allocator,
        VkDevice device,
        uint32_t size,
        VkFormat format,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        uint32_t mipLevels = 1
    );

    // Create a depth texture
    static GPUTexture createDepthTexture(
        VmaAllocator allocator,
        VkDevice device,
        uint32_t width,
        uint32_t height,
        VkFormat format = VK_FORMAT_D32_SFLOAT
    );

    // Create an image view for an existing texture
    static VkImageView createImageView(
        VkDevice device,
        VkImage image,
        VkFormat format,
        VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
        uint32_t mipLevels = 1,
        uint32_t layerCount = 1
    );

    // Create a sampler
    static VkSampler createSampler(
        VkDevice device,
        VkFilter magFilter = VK_FILTER_LINEAR,
        VkFilter minFilter = VK_FILTER_LINEAR,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        float maxAnisotropy = 1.0f,
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        float maxLod = VK_LOD_CLAMP_NONE
    );

    // ─── Texture Operations ─────────────────────────────────────

    // Transition image layout (barrier)
    static void transitionImageLayout(
        VkCommandBuffer cmdBuffer,
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels = 1,
        uint32_t layerCount = 1
    );

    // Generate mipmaps for a texture
    static void generateMipmaps(
        VkCommandBuffer cmdBuffer,
        VkImage image,
        VkFormat format,
        uint32_t width,
        uint32_t height,
        uint32_t mipLevels
    );

    // Destroy a texture and free all its resources
    static void destroyTexture(VmaAllocator allocator, VkDevice device, GPUTexture& texture);

    // ─── Default Textures ───────────────────────────────────────

    // Create a 1x1 solid color texture (for fallbacks)
    static GPUTexture createSolidColorTexture(
        VmaAllocator allocator,
        VkDevice device,
        VkQueue queue,
        VkCommandPool cmdPool,
        uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255
    );

    // Create standard default textures for PBR fallbacks
    struct DefaultTextures {
        GPUTexture white;           // (255, 255, 255, 255) - default albedo
        GPUTexture black;           // (0, 0, 0, 255)
        GPUTexture normal;          // (128, 128, 255, 255) - flat normal map
        GPUTexture metallicRoughness; // (0, 128, 0, 255) - non-metallic, mid-rough
    };

    static DefaultTextures createDefaultTextures(
        VmaAllocator allocator,
        VkDevice device,
        VkQueue queue,
        VkCommandPool cmdPool
    );

    static void destroyDefaultTextures(
        VmaAllocator allocator,
        VkDevice device,
        DefaultTextures& defaults
    );

    // ─── Utility ────────────────────────────────────────────────

    // Calculate the number of mip levels for given dimensions
    static uint32_t calculateMipLevels(uint32_t width, uint32_t height);

    // Get the size in bytes for a format
    static VkDeviceSize getFormatSize(VkFormat format);
};

// ============================================================================
// Inline implementations for simple functions
// ============================================================================

inline uint32_t GPUResourceFactory::calculateMipLevels(uint32_t width, uint32_t height) {
    return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

} // namespace Shoonyakasha
