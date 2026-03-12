//
// VulkanCubemap.cpp - Cubemap texture implementation
//
// 青龍司生  萬象映照
// The Azure Dragon governs creation — all things reflected
//

#include "Vulkan/VulkanCubemap.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace Shoonyakasha {

// ═══════════════════════════════════════════════════════════════
// Factory Methods
// ═══════════════════════════════════════════════════════════════

VulkanCubemap* VulkanCubemap::createEnvironmentMap(VulkanDevice& device, uint32_t size) {
    CubemapCreateInfo info{};
    info.size = size;
    info.mipLevels = calculateMipLevels(size);
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.generateMipViews = true;
    return new VulkanCubemap(device, info);
}

VulkanCubemap* VulkanCubemap::createIrradianceMap(VulkanDevice& device, uint32_t size) {
    CubemapCreateInfo info{};
    info.size = size;
    info.mipLevels = 1;  // Irradiance is low-frequency, no mipmaps needed
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.generateMipViews = false;
    return new VulkanCubemap(device, info);
}

VulkanCubemap* VulkanCubemap::createPrefilterMap(VulkanDevice& device, uint32_t size) {
    CubemapCreateInfo info{};
    info.size = size;
    info.mipLevels = calculateMipLevels(size);  // Each mip = different roughness
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    info.generateMipViews = true;
    return new VulkanCubemap(device, info);
}

uint32_t VulkanCubemap::calculateMipLevels(uint32_t size) {
    return static_cast<uint32_t>(std::floor(std::log2(size))) + 1;
}

// ═══════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════

VulkanCubemap::VulkanCubemap(VulkanDevice& device, const CubemapCreateInfo& info)
    : m_device(device)
    , m_size(info.size)
    , m_mipLevels(info.mipLevels)
    , m_format(info.format)
    , m_hasMipViews(info.generateMipViews)
{
    createImage(info);
    createCubeView();
    createFaceViews();
    if (m_hasMipViews && m_mipLevels > 1) {
        createMipViews();
    }
    createSampler();
}

VulkanCubemap::~VulkanCubemap() {
    cleanup();
}

void VulkanCubemap::cleanup() {
    VkDevice logicalDevice = m_device.getLogicalDevice();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(logicalDevice, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    for (auto view : m_mipViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(logicalDevice, view, nullptr);
        }
    }
    m_mipViews.clear();

    for (auto view : m_faceViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(logicalDevice, view, nullptr);
        }
    }
    m_faceViews.clear();

    if (m_cubeView != VK_NULL_HANDLE) {
        vkDestroyImageView(logicalDevice, m_cubeView, nullptr);
        m_cubeView = VK_NULL_HANDLE;
    }

    if (m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device.getAllocator().getHandle(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

// ═══════════════════════════════════════════════════════════════
// Image Creation
// ═══════════════════════════════════════════════════════════════

void VulkanCubemap::createImage(const CubemapCreateInfo& info) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;  // Critical for cubemaps
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = info.format;
    imageInfo.extent.width = info.size;
    imageInfo.extent.height = info.size;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = 6;  // 6 faces for cubemap
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = info.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VmaAllocationInfo resultInfo{};
    VkResult result = vmaCreateImage(
        m_device.getAllocator().getHandle(),
        &imageInfo, &allocInfo,
        &m_image, &m_allocation, &resultInfo
    );

    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap image via VMA!");
    }
}

void VulkanCubemap::createCubeView() {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = m_format;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &m_cubeView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap view!");
    }
}

void VulkanCubemap::createFaceViews() {
    // Create per-face, per-mip views for compute shader storage image writes
    m_faceViews.resize(6 * m_mipLevels, VK_NULL_HANDLE);

    for (uint32_t face = 0; face < 6; ++face) {
        for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;  // 2D view for compute writes
            viewInfo.format = m_format;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = mip;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = face;
            viewInfo.subresourceRange.layerCount = 1;

            uint32_t index = face * m_mipLevels + mip;
            if (vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &m_faceViews[index]) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create cubemap face view!");
            }
        }
    }
}

void VulkanCubemap::createMipViews() {
    // Create per-mip cube views (all 6 faces at specific mip level)
    m_mipViews.resize(m_mipLevels, VK_NULL_HANDLE);

    for (uint32_t mip = 0; mip < m_mipLevels; ++mip) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = m_format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = mip;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;

        if (vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &m_mipViews[mip]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create cubemap mip view!");
        }
    }
}

void VulkanCubemap::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(m_device.getLogicalDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cubemap sampler!");
    }
}

// ═══════════════════════════════════════════════════════════════
// Accessors
// ═══════════════════════════════════════════════════════════════

VkImageView VulkanCubemap::getFaceView(uint32_t face, uint32_t mip) const {
    if (face >= 6 || mip >= m_mipLevels) {
        throw std::out_of_range("Face or mip index out of range");
    }
    return m_faceViews[face * m_mipLevels + mip];
}

VkImageView VulkanCubemap::getMipView(uint32_t mip) const {
    if (mip >= m_mipLevels || mip >= m_mipViews.size()) {
        throw std::out_of_range("Mip index out of range");
    }
    return m_mipViews[mip];
}

// ═══════════════════════════════════════════════════════════════
// Layout Transitions
// ═══════════════════════════════════════════════════════════════

void VulkanCubemap::transitionLayout(VkCommandBuffer cmd,
                                      VkImageLayout oldLayout,
                                      VkImageLayout newLayout,
                                      VkPipelineStageFlags srcStage,
                                      VkPipelineStageFlags dstStage,
                                      VkAccessFlags srcAccess,
                                      VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = m_mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;  // All faces
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace Shoonyakasha
void VulkanCubemap::transitionMipLayout(VkCommandBuffer cmd,
                                         uint32_t mipLevel,
                                         VkImageLayout oldLayout,
                                         VkImageLayout newLayout,
                                         VkPipelineStageFlags srcStage,
                                         VkPipelineStageFlags dstStage,
                                         VkAccessFlags srcAccess,
                                         VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 6;  // All faces at this mip
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}
