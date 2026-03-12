//
// Created by maxim on 05.07.2024.
//

#include "Vulkan/VulkanImage.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanMemoryAllocator.h"
#include <stdexcept>

namespace Shoonyakasha {

VulkanImage::VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                         VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
        : m_device(device)
        , m_image(VK_NULL_HANDLE)
        , m_allocation(VK_NULL_HANDLE)
        , m_imageView(VK_NULL_HANDLE)
        , m_format(format)
        , m_width(width)
        , m_height(height)
        , m_tiling(tiling)
        , m_usage(usage)
        , m_properties(properties)
        , m_ownImage(true)
{
    m_logger = new Logger("vulkan_image.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan Image of size %ux%u", width, height);
    createImage(width, height, format, tiling, usage, properties);
}

VulkanImage::VulkanImage(VulkanDevice &device, VkImage existingImage, VkFormat format, uint32_t width, uint32_t height,
                         VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties)
        : m_device(device)
        , m_image(existingImage)
        , m_allocation(VK_NULL_HANDLE)
        , m_imageView(VK_NULL_HANDLE)
        , m_format(format)
        , m_width(width)
        , m_height(height)
        , m_tiling(tiling)
        , m_usage(usage)
        , m_properties(properties)
        , m_ownImage(false)
{
    m_logger = new Logger("vulkan_image.log");
    m_eventDispatcher = new EventDispatcher();
}

VulkanImage::~VulkanImage() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Image");

    if (m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device.getLogicalDevice(), m_imageView, nullptr);
    }

    if (m_ownImage && m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device.getAllocator().getHandle(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanImage::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                              VkImageUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Use VMA for allocation
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.requiredFlags = properties;

    VmaAllocationInfo resultInfo{};
    VkResult result = vmaCreateImage(
        m_device.getAllocator().getHandle(),
        &imageInfo, &allocInfo,
        &m_image, &m_allocation, &resultInfo
    );

    if (result != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create image via VMA");
        throw std::runtime_error("Failed to create image via VMA!");
    }

    m_logger->log(LogLevel::Info, "Image created successfully via VMA");
    m_eventDispatcher->publish(ImageCreatedEvent{m_image, width, height, format});
}

void VulkanImage::createImageView(VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device.getLogicalDevice(), &viewInfo, nullptr, &m_imageView) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create image view");
        throw std::runtime_error("Failed to create image view!");
    }

    m_logger->log(LogLevel::Info, "Image view created successfully");
    m_eventDispatcher->publish(ImageViewCreatedEvent{m_imageView});
}

void VulkanImage::transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = m_device.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_image;
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

    m_logger->log(LogLevel::Info, "Image layout transitioned from %d to %d", oldLayout, newLayout);
    m_eventDispatcher->publish(ImageLayoutTransitionedEvent{m_image, oldLayout, newLayout});
}

} // namespace Shoonyakasha
