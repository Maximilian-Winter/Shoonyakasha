//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "Core/Logger.h"
#include "Core/EventSystem.h"

namespace Shoonyakasha {

class VulkanDevice;

class VulkanImage {
public:
    // Create a new image (owned, allocated via VMA)
    VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                VkImageUsageFlags usage, VkMemoryPropertyFlags properties);

    // Wrap an existing image (not owned, no VMA allocation)
    VulkanImage(VulkanDevice& device, VkImage existingImage, VkFormat format, uint32_t width, uint32_t height,
                VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
    ~VulkanImage();

    VkImage getImage() const { return m_image; }
    VkImageView getImageView() const { return m_imageView; }
    VkFormat getFormat() const { return m_format; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }

    void createImageView(VkImageAspectFlags aspectFlags);
    void transitionImageLayout(VkImageLayout oldLayout, VkImageLayout newLayout);

private:
    bool m_ownImage;
    VulkanDevice& m_device;
    VkImage m_image;
    VmaAllocation m_allocation;  // VMA allocation (null for imported images)
    VkImageView m_imageView;
    VkFormat m_format;
    uint32_t m_width;
    uint32_t m_height;
    VkImageTiling m_tiling;
    VkImageUsageFlags m_usage;
    VkMemoryPropertyFlags m_properties;
    Logger* m_logger = nullptr;
    EventDispatcher* m_eventDispatcher = nullptr;

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VkMemoryPropertyFlags properties);
};

// Events
struct ImageCreatedEvent : public Event {
    VkImage image;
    uint32_t width;
    uint32_t height;
    VkFormat format;

    ImageCreatedEvent(VkImage i, uint32_t w, uint32_t h, VkFormat f)
            : image(i), width(w), height(h), format(f) {}
};

struct ImageViewCreatedEvent : public Event {
    VkImageView imageView;
    explicit ImageViewCreatedEvent(VkImageView iv) : imageView(iv) {}
};

struct ImageLayoutTransitionedEvent : public Event {
    VkImage image;
    VkImageLayout oldLayout;
    VkImageLayout newLayout;
    ImageLayoutTransitionedEvent(VkImage i, VkImageLayout ol, VkImageLayout nl)
            : image(i), oldLayout(ol), newLayout(nl) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanImage;
using Shoonyakasha::ImageCreatedEvent;
using Shoonyakasha::ImageViewCreatedEvent;
using Shoonyakasha::ImageLayoutTransitionedEvent;
