//
// Created by maxim on 05.07.2024.
//
#include "Vulkan/VulkanTexture.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanBuffer.h"
#include <stdexcept>

namespace Shoonyakasha {

VulkanTexture::VulkanTexture(VulkanDevice& device, const uint8_t* pixels, uint32_t width, uint32_t height,
                             uint32_t channels, VkFormat format)
        : m_device(device)
        , m_textureImage(nullptr)
        , m_textureImageView(VK_NULL_HANDLE)
        , m_textureSampler(VK_NULL_HANDLE)
        , m_width(width)
        , m_height(height)
        , m_channels(channels)
{
    m_logger = new Logger("vulkan_texture.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan Texture (%ux%u, %u channels)", width, height, channels);
    createTextureImage(pixels, width, height, format);
    createTextureImageView();
    createTextureSampler();
}

// HDR float texture constructor
VulkanTexture::VulkanTexture(VulkanDevice& device, const float* pixels, uint32_t width, uint32_t height,
                             uint32_t channels, VkFormat format)
        : m_device(device)
        , m_textureImage(nullptr)
        , m_textureImageView(VK_NULL_HANDLE)
        , m_textureSampler(VK_NULL_HANDLE)
        , m_width(width)
        , m_height(height)
        , m_channels(channels)
{
    m_logger = new Logger("vulkan_texture.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating HDR Vulkan Texture (%ux%u, %u channels, float)", width, height, channels);

    if (!pixels) {
        m_logger->log(LogLevel::Error, "Null pixel data provided to HDR texture");
        throw std::runtime_error("Null pixel data provided to HDR texture!");
    }

    // HDR textures use float data: 4 bytes per channel
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * channels * sizeof(float);

    VulkanBuffer stagingBuffer(m_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.map();
    stagingBuffer.copyFrom(pixels, static_cast<size_t>(imageSize));
    stagingBuffer.unmap();

    m_textureImage = new VulkanImage(m_device, width, height, format, VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    transitionImageLayout(m_textureImage->getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer.getBuffer(), m_textureImage->getImage(), width, height);
    transitionImageLayout(m_textureImage->getImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_logger->log(LogLevel::Info, "HDR texture image created successfully");
    m_eventDispatcher->publish(TextureImageCreatedEvent{m_textureImage->getImage(), static_cast<int>(width), static_cast<int>(height)});

    createTextureImageView();
    createTextureSampler();
}

VulkanTexture::~VulkanTexture() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Texture");

    vkDestroySampler(m_device.getLogicalDevice(), m_textureSampler, nullptr);
    // Note: m_textureImageView is owned by m_textureImage, which will destroy it
    // Do NOT call vkDestroyImageView here to avoid double-free
    delete m_textureImage;

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanTexture::createTextureImage(const uint8_t* pixels, uint32_t width, uint32_t height, VkFormat format) {
    if (!pixels) {
        m_logger->log(LogLevel::Error, "Null pixel data provided to texture");
        throw std::runtime_error("Null pixel data provided to texture!");
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * m_channels;

    VulkanBuffer stagingBuffer(m_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.map();
    stagingBuffer.copyFrom(pixels, static_cast<size_t>(imageSize));
    stagingBuffer.unmap();

    m_textureImage = new VulkanImage(m_device, width, height, format, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    transitionImageLayout(m_textureImage->getImage(), format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer.getBuffer(), m_textureImage->getImage(), width, height);
    transitionImageLayout(m_textureImage->getImage(), format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_logger->log(LogLevel::Info, "Texture image created successfully");
    m_eventDispatcher->publish(TextureImageCreatedEvent{m_textureImage->getImage(), static_cast<int>(width), static_cast<int>(height)});
}

void VulkanTexture::createTextureImageView() {
    m_textureImage->createImageView(VK_IMAGE_ASPECT_COLOR_BIT);
    m_textureImageView = m_textureImage->getImageView();
    m_logger->log(LogLevel::Info, "Texture image view created successfully");
    m_eventDispatcher->publish(TextureImageViewCreatedEvent{m_textureImageView});
}

void VulkanTexture::createTextureSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_device.getLogicalDevice(), &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create texture sampler");
        throw std::runtime_error("Failed to create texture sampler!");
    }

    m_logger->log(LogLevel::Info, "Texture sampler created successfully");
    m_eventDispatcher->publish(TextureSamplerCreatedEvent{m_textureSampler});
}

void VulkanTexture::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        m_logger->log(LogLevel::Error, "Unsupported layout transition");
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
    );

    m_device.endSingleTimeCommands(commandBuffer);

    m_logger->log(LogLevel::Debug, "Image layout transitioned from %d to %d", oldLayout, newLayout);
}

void VulkanTexture::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
            width,
            height,
            1
    };

    vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region
    );

    m_device.endSingleTimeCommands(commandBuffer);

    m_logger->log(LogLevel::Debug, "Buffer copied to image");
}

} // namespace Shoonyakasha
