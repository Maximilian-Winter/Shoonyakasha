//
// Created by maxim on 05.07.2024.
//

#include "../../include/Vulkan/VulkanBuffer.h"
#include "../../include/Vulkan/VulkanDevice.h"
#include "../../include/Vulkan/VulkanMemoryAllocator.h"
#include <stdexcept>
#include <cstring>

namespace Shoonyakasha {

VulkanBuffer::VulkanBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
        : m_device(device)
        , m_buffer(VK_NULL_HANDLE)
        , m_allocation(VK_NULL_HANDLE)
        , m_size(size)
        , m_mappedData(nullptr)
        , m_mappedBase(nullptr)
        , m_mappedOffset(0)
{
    m_logger = new Logger("vulkan_buffer.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan Buffer of size %llu", size);
    createBuffer(size, usage, properties);
}

VulkanBuffer::~VulkanBuffer() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Buffer");

    if (m_mappedBase) {
        unmap();
    }

    if (m_buffer != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_device.getAllocator().getHandle(), m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanBuffer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Determine VMA memory usage from Vulkan memory property flags
    VmaMemoryUsage vmaUsage = VMA_MEMORY_USAGE_AUTO;
    VmaAllocationCreateFlags vmaFlags = 0;

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        // Host-visible memory: needs HOST_ACCESS flag for VMA_MEMORY_USAGE_AUTO
        vmaFlags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = vmaUsage;
    allocInfo.flags = vmaFlags;
    allocInfo.requiredFlags = properties;

    VmaAllocationInfo resultInfo{};
    VkResult result = vmaCreateBuffer(
        m_device.getAllocator().getHandle(),
        &bufferInfo, &allocInfo,
        &m_buffer, &m_allocation, &resultInfo
    );

    if (result != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create buffer via VMA");
        throw std::runtime_error("Failed to create buffer via VMA!");
    }

    m_logger->log(LogLevel::Info, "Buffer created successfully via VMA");
    m_eventDispatcher->publish(BufferCreatedEvent{m_buffer, size});
}

void VulkanBuffer::map(VkDeviceSize size, VkDeviceSize offset) {
    if (m_mappedData) {
        m_logger->log(LogLevel::Warning, "Buffer is already mapped. Unmapping first.");
        unmap();
    }

    void* basePtr = nullptr;
    if (vmaMapMemory(m_device.getAllocator().getHandle(), m_allocation, &basePtr) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to map buffer memory via VMA");
        throw std::runtime_error("Failed to map buffer memory via VMA!");
    }

    // Store base pointer for correct unmap; apply offset for user access
    m_mappedBase = basePtr;
    m_mappedData = static_cast<char*>(basePtr) + offset;
    m_mappedOffset = offset;

    m_logger->log(LogLevel::Debug, "Buffer mapped via VMA (offset=%llu)", offset);
}

void VulkanBuffer::unmap() {
    if (m_mappedBase) {
        vmaUnmapMemory(m_device.getAllocator().getHandle(), m_allocation);
        m_mappedBase = nullptr;
        m_mappedData = nullptr;
        m_mappedOffset = 0;
        m_logger->log(LogLevel::Debug, "Buffer unmapped via VMA");
    }
}

void VulkanBuffer::copyTo(void* data, VkDeviceSize size) {
    if (!m_mappedData) {
        m_logger->log(LogLevel::Error, "Attempt to copy from unmapped buffer");
        throw std::runtime_error("Cannot copy from unmapped buffer!");
    }

    memcpy(data, m_mappedData, size);
    m_logger->log(LogLevel::Debug, "Data copied from buffer to host");
}

void VulkanBuffer::copyFrom(const void* data, VkDeviceSize size) {
    if (!m_mappedData) {
        map(size);
    }

    memcpy(m_mappedData, data, size);
    m_logger->log(LogLevel::Debug, "Data copied from buffer");

    // Flush the correct range accounting for any mapping offset
    vmaFlushAllocation(m_device.getAllocator().getHandle(), m_allocation, m_mappedOffset, size);
}

void VulkanBuffer::update(const void *data, VkDeviceSize size, VkDeviceSize offset)
{
    void* mappedData;
    if (vmaMapMemory(m_device.getAllocator().getHandle(), m_allocation, &mappedData) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to map buffer for update");
        throw std::runtime_error("Failed to map buffer for update!");
    }

    memcpy(static_cast<char*>(mappedData) + offset, data, static_cast<size_t>(size));
    vmaFlushAllocation(m_device.getAllocator().getHandle(), m_allocation, offset, size);
    vmaUnmapMemory(m_device.getAllocator().getHandle(), m_allocation);
}

} // namespace Shoonyakasha
