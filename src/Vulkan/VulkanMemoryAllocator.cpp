//
// Shoonyakasha Engine - VMA Implementation
//

#define VMA_IMPLEMENTATION
#include "Vulkan/VulkanMemoryAllocator.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanInstance.h"
#include <stdexcept>

namespace Shoonyakasha {

VulkanMemoryAllocator::VulkanMemoryAllocator(VulkanInstance& instance, VulkanDevice& device) {
    m_logger = new Logger("vulkan_vma.log");
    m_logger->log(LogLevel::Info, "Initializing Vulkan Memory Allocator");

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = device.getPhysicalDevice();
    allocatorInfo.device = device.getLogicalDevice();
    allocatorInfo.instance = instance.getInstance();
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create VMA allocator");
        throw std::runtime_error("Failed to create VMA allocator!");
    }

    m_logger->log(LogLevel::Info, "VMA allocator created successfully");
}

VulkanMemoryAllocator::~VulkanMemoryAllocator() {
    m_logger->log(LogLevel::Info, "Destroying VMA allocator");

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
    }

    delete m_logger;
}

VmaImageAllocation VulkanMemoryAllocator::createImage(
    const VkImageCreateInfo& imageInfo,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags flags)
{
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = flags;

    VmaImageAllocation result{};
    VkResult vkResult = vmaCreateImage(m_allocator, &imageInfo, &allocInfo,
                                        &result.image, &result.allocation, &result.allocationInfo);

    if (vkResult != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create VMA image allocation");
        throw std::runtime_error("Failed to create VMA image allocation!");
    }

    m_logger->log(LogLevel::Debug, "VMA image allocated: %ux%u",
                  imageInfo.extent.width, imageInfo.extent.height);
    return result;
}

void VulkanMemoryAllocator::destroyImage(VmaImageAllocation& alloc) {
    if (alloc.valid()) {
        vmaDestroyImage(m_allocator, alloc.image, alloc.allocation);
        alloc.image = VK_NULL_HANDLE;
        alloc.allocation = VK_NULL_HANDLE;
        m_logger->log(LogLevel::Debug, "VMA image destroyed");
    }
}

VmaBufferAllocation VulkanMemoryAllocator::createBuffer(
    const VkBufferCreateInfo& bufferInfo,
    VmaMemoryUsage memoryUsage,
    VmaAllocationCreateFlags flags)
{
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = flags;

    VmaBufferAllocation result{};
    VkResult vkResult = vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo,
                                         &result.buffer, &result.allocation, &result.allocationInfo);

    if (vkResult != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create VMA buffer allocation");
        throw std::runtime_error("Failed to create VMA buffer allocation!");
    }

    m_logger->log(LogLevel::Debug, "VMA buffer allocated: %llu bytes", bufferInfo.size);
    return result;
}

void VulkanMemoryAllocator::destroyBuffer(VmaBufferAllocation& alloc) {
    if (alloc.valid()) {
        vmaDestroyBuffer(m_allocator, alloc.buffer, alloc.allocation);
        alloc.buffer = VK_NULL_HANDLE;
        alloc.allocation = VK_NULL_HANDLE;
        m_logger->log(LogLevel::Debug, "VMA buffer destroyed");
    }
}

void* VulkanMemoryAllocator::mapMemory(VmaAllocation allocation) {
    void* data = nullptr;
    if (vmaMapMemory(m_allocator, allocation, &data) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to map VMA memory");
        throw std::runtime_error("Failed to map VMA memory!");
    }
    return data;
}

void VulkanMemoryAllocator::unmapMemory(VmaAllocation allocation) {
    vmaUnmapMemory(m_allocator, allocation);
}

void VulkanMemoryAllocator::flushAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) {
    vmaFlushAllocation(m_allocator, allocation, offset, size);
}

VmaTotalStatistics VulkanMemoryAllocator::getStats() const {
    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_allocator, &stats);
    return stats;
}

} // namespace Shoonyakasha
