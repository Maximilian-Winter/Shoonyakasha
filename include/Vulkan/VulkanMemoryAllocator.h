//
// Shoonyakasha Engine - VMA Wrapper
// Wraps Vulkan Memory Allocator for centralized GPU memory management
//

#pragma once

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "Core/Logger.h"

namespace Shoonyakasha {

class VulkanDevice;
class VulkanInstance;

// Result of a VMA image allocation
struct VmaImageAllocation {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    bool valid() const { return image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE; }
};

// Result of a VMA buffer allocation
struct VmaBufferAllocation {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo allocationInfo{};

    bool valid() const { return buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE; }
};

class VulkanMemoryAllocator {
public:
    VulkanMemoryAllocator(VulkanInstance& instance, VulkanDevice& device);
    ~VulkanMemoryAllocator();

    // Non-copyable, non-movable
    VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
    VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;

    // Image allocation
    VmaImageAllocation createImage(const VkImageCreateInfo& imageInfo,
                                   VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
                                   VmaAllocationCreateFlags flags = 0);
    void destroyImage(VmaImageAllocation& alloc);

    // Buffer allocation
    VmaBufferAllocation createBuffer(const VkBufferCreateInfo& bufferInfo,
                                     VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
                                     VmaAllocationCreateFlags flags = 0);
    void destroyBuffer(VmaBufferAllocation& alloc);

    // Memory mapping
    void* mapMemory(VmaAllocation allocation);
    void unmapMemory(VmaAllocation allocation);

    // Flush mapped memory range (for non-coherent memory)
    void flushAllocation(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    // Stats
    VmaTotalStatistics getStats() const;

    VmaAllocator getHandle() const { return m_allocator; }

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    Logger* m_logger = nullptr;
};

} // namespace Shoonyakasha
using Shoonyakasha::VmaImageAllocation;
using Shoonyakasha::VmaBufferAllocation;
using Shoonyakasha::VulkanMemoryAllocator;
