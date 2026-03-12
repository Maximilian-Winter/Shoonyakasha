//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "VulkanInstance.h"
#include "VulkanMemoryAllocator.h"
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include <vector>
#include <optional>
#include <memory>

namespace Shoonyakasha {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    std::optional<uint32_t> computeFamily;     // Dedicated compute (prefer non-graphics)
    std::optional<uint32_t> transferFamily;    // Dedicated transfer (nice-to-have)

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanDevice {
public:
    VulkanDevice(VulkanInstance& instance, VkSurfaceKHR surface);
    ~VulkanDevice();

    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkDevice getLogicalDevice() const { return m_device; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    VkQueue getComputeQueue() const { return m_computeQueue; }

    VkCommandPool getCommandPool() const { return m_commandPool; }
    VkCommandPool getComputeCommandPool() const { return m_computeCommandPool; }

    uint32_t getGraphicsQueueFamily() const;
    uint32_t getComputeQueueFamily() const;
    bool hasDedicatedComputeQueue() const { return m_hasDedicatedCompute; }

    VulkanInstance& getInstance() const { return m_instance; }
    VulkanMemoryAllocator& getAllocator() const { return *m_vmaAllocator; }

    VkMemoryPropertyFlags getMemoryProperties() const;
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    VkFormat findDepthFormat();
private:
    VulkanInstance& m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkQueue m_computeQueue;
    VkCommandPool m_commandPool;
    VkCommandPool m_computeCommandPool = VK_NULL_HANDLE;
    bool m_hasDedicatedCompute = false;
    QueueFamilyIndices m_queueFamilyIndices;
    std::unique_ptr<VulkanMemoryAllocator> m_vmaAllocator;

    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;

    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    bool isDeviceSuitable(VkPhysicalDevice device);

    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;
    const std::vector<const char*> m_deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

// Define the DeviceCreatedEvent
struct DeviceCreatedEvent : public Event {
    VkDevice device;
    explicit DeviceCreatedEvent(VkDevice d) : device(d) {}
};

} // namespace Shoonyakasha
using Shoonyakasha::QueueFamilyIndices;
using Shoonyakasha::SwapChainSupportDetails;
using Shoonyakasha::VulkanDevice;
using Shoonyakasha::DeviceCreatedEvent;

