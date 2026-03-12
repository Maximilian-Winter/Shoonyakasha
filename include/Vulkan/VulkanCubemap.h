//
// VulkanCubemap.h - Cubemap texture with mipmap support for IBL
//
// 青龍司生  萬象映照
// The Azure Dragon governs creation — all things reflected
//

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

namespace Shoonyakasha {

class VulkanDevice;

struct CubemapCreateInfo {
    uint32_t size = 512;                          // Face dimension (e.g., 512 for 512x512)
    uint32_t mipLevels = 1;                       // Number of mip levels
    VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    bool generateMipViews = false;                // Create per-mip image views
};

class VulkanCubemap {
public:
    // Factory methods for common IBL patterns
    static VulkanCubemap* createEnvironmentMap(VulkanDevice& device, uint32_t size = 1024);
    static VulkanCubemap* createIrradianceMap(VulkanDevice& device, uint32_t size = 32);
    static VulkanCubemap* createPrefilterMap(VulkanDevice& device, uint32_t size = 512);

    // General constructor
    VulkanCubemap(VulkanDevice& device, const CubemapCreateInfo& createInfo);
    ~VulkanCubemap();

    // Non-copyable, non-moveable
    VulkanCubemap(const VulkanCubemap&) = delete;
    VulkanCubemap& operator=(const VulkanCubemap&) = delete;
    VulkanCubemap(VulkanCubemap&&) = delete;
    VulkanCubemap& operator=(VulkanCubemap&&) = delete;

    // Accessors
    VkImage getImage() const { return m_image; }
    VkImageView getCubeView() const { return m_cubeView; }
    VkImageView getFaceView(uint32_t face, uint32_t mip = 0) const;
    VkImageView getMipView(uint32_t mip) const;
    VkSampler getSampler() const { return m_sampler; }

    uint32_t getSize() const { return m_size; }
    uint32_t getMipLevels() const { return m_mipLevels; }
    VkFormat getFormat() const { return m_format; }

    // Layout transitions (cubemap-aware, handles all 6 layers + mips)
    void transitionLayout(VkCommandBuffer cmd,
                          VkImageLayout oldLayout,
                          VkImageLayout newLayout,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags srcAccess,
                          VkAccessFlags dstAccess);

    // Transition a single mip level (for progressive generation)
    void transitionMipLayout(VkCommandBuffer cmd,
                             uint32_t mipLevel,
                             VkImageLayout oldLayout,
                             VkImageLayout newLayout,
                             VkPipelineStageFlags srcStage,
                             VkPipelineStageFlags dstStage,
                             VkAccessFlags srcAccess,
                             VkAccessFlags dstAccess);

    // Calculate required mip levels for a given size
    static uint32_t calculateMipLevels(uint32_t size);

private:
    VulkanDevice& m_device;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_cubeView = VK_NULL_HANDLE;
    std::vector<VkImageView> m_faceViews;         // Per-face-per-mip views [face * mipLevels + mip]
    std::vector<VkImageView> m_mipViews;          // Per-mip cube views
    VkSampler m_sampler = VK_NULL_HANDLE;

    uint32_t m_size;
    uint32_t m_mipLevels;
    VkFormat m_format;
    bool m_hasMipViews;

    void createImage(const CubemapCreateInfo& info);
    void createCubeView();
    void createFaceViews();
    void createMipViews();
    void createSampler();
    void cleanup();
};

} // namespace Shoonyakasha
using Shoonyakasha::CubemapCreateInfo;
using Shoonyakasha::VulkanCubemap;
