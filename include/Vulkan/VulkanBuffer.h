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

class VulkanBuffer {
public:
    VulkanBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
    ~VulkanBuffer();

    VkBuffer getBuffer() const { return m_buffer; }
    VkDeviceSize getSize() const { return m_size; }

    void map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    void unmap();
    void copyTo(void* data, VkDeviceSize size);
    void copyFrom(const void* data, VkDeviceSize size);

    void update(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

private:
    VulkanDevice& m_device;
    VkBuffer m_buffer;
    VmaAllocation m_allocation;
    VkDeviceSize m_size;
    void* m_mappedData;
    void* m_mappedBase;         // Base pointer from VMA (for correct unmap)
    VkDeviceSize m_mappedOffset; // Offset applied to m_mappedData

    Logger* m_logger = nullptr;
    EventDispatcher* m_eventDispatcher = nullptr;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
};


// Event for notifying that a buffer has been created
struct BufferCreatedEvent : public Event {
    VkBuffer buffer;
    VkDeviceSize size;
    BufferCreatedEvent(VkBuffer b, VkDeviceSize s) : buffer(b), size(s) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanBuffer;
using Shoonyakasha::BufferCreatedEvent;
