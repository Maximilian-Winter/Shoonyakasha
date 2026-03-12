//
// Created by maxim on 05.07.2024.
//

#include "../../include/Vulkan/VulkanDevice.h"
#include "../../include/Vulkan/VulkanMemoryAllocator.h"
#include <set>
#include <string>
#include <stdexcept>

namespace Shoonyakasha {

VulkanDevice::VulkanDevice(VulkanInstance& instance, VkSurfaceKHR surface)
        : m_instance(instance)
        , m_surface(surface)
        , m_physicalDevice(VK_NULL_HANDLE)
        , m_device(VK_NULL_HANDLE)
        , m_graphicsQueue(VK_NULL_HANDLE)
        , m_presentQueue(VK_NULL_HANDLE)
        , m_computeQueue(VK_NULL_HANDLE)
        , m_commandPool(VK_NULL_HANDLE)
{
    m_logger = new Logger("vulkan_device.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan Device");
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();

    // Initialize VMA after device and command pool are ready
    m_vmaAllocator = std::make_unique<VulkanMemoryAllocator>(m_instance, *this);
}

VulkanDevice::~VulkanDevice() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Device");

    // Destroy VMA before device — all VMA allocations must be freed first
    m_vmaAllocator.reset();

    if (m_computeCommandPool != VK_NULL_HANDLE && m_computeCommandPool != m_commandPool) {
        vkDestroyCommandPool(m_device, m_computeCommandPool, nullptr);
    }
    vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    vkDestroyDevice(m_device, nullptr);

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanDevice::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance.getInstance(), &deviceCount, nullptr);

    if (deviceCount == 0) {
        m_logger->log(LogLevel::Error, "Failed to find GPUs with Vulkan support");
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance.getInstance(), &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        m_logger->log(LogLevel::Error, "Failed to find a suitable GPU");
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    m_logger->log(LogLevel::Info, "Physical device selected successfully");
}

void VulkanDevice::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    m_queueFamilyIndices = indices;

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    // Add compute family if discovered
    if (indices.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(indices.computeFamily.value());
    }

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.largePoints = VK_TRUE;       // Required for gl_PointSize > 1.0 (particle rendering)
    deviceFeatures.wideLines = VK_TRUE;         // Nice-to-have for debug line rendering
    deviceFeatures.samplerAnisotropy = VK_TRUE; // Better texture quality

    // Enable timeline semaphore feature for multi-queue synchronization
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{};
    timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timelineFeatures.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vulkan12Features{};
    vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12Features.timelineSemaphore = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &vulkan12Features;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

    if (m_instance.isValidationLayersEnabled()) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_instance.getValidationLayers().size());
        createInfo.ppEnabledLayerNames = m_instance.getValidationLayers().data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create logical device");
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);

    // Get compute queue
    if (indices.computeFamily.has_value()) {
        vkGetDeviceQueue(m_device, indices.computeFamily.value(), 0, &m_computeQueue);
        m_hasDedicatedCompute = (indices.computeFamily.value() != indices.graphicsFamily.value());
        m_logger->log(LogLevel::Info, "Compute queue family: %u (dedicated: %s)",
                      indices.computeFamily.value(),
                      m_hasDedicatedCompute ? "yes" : "no (shared with graphics)");
    } else {
        // Fallback: use graphics queue for compute
        m_computeQueue = m_graphicsQueue;
        m_hasDedicatedCompute = false;
        m_logger->log(LogLevel::Info, "No compute queue found, using graphics queue for compute");
    }

    m_logger->log(LogLevel::Info, "Logical device created successfully (graphics=%u, present=%u, compute=%u)",
                  indices.graphicsFamily.value(), indices.presentFamily.value(),
                  indices.computeFamily.value_or(indices.graphicsFamily.value()));
    m_eventDispatcher->publish(DeviceCreatedEvent{m_device});
}

void VulkanDevice::createCommandPool() {
    // Graphics command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create command pool");
        throw std::runtime_error("failed to create command pool!");
    }
    m_logger->log(LogLevel::Info, "Graphics command pool created successfully");

    // Compute command pool (separate if dedicated compute queue exists)
    if (m_queueFamilyIndices.computeFamily.has_value() &&
        m_queueFamilyIndices.computeFamily.value() != m_queueFamilyIndices.graphicsFamily.value()) {
        VkCommandPoolCreateInfo computePoolInfo{};
        computePoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        computePoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        computePoolInfo.queueFamilyIndex = m_queueFamilyIndices.computeFamily.value();

        if (vkCreateCommandPool(m_device, &computePoolInfo, nullptr, &m_computeCommandPool) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "Failed to create compute command pool");
            throw std::runtime_error("failed to create compute command pool!");
        }
        m_logger->log(LogLevel::Info, "Compute command pool created successfully (family %u)",
                      m_queueFamilyIndices.computeFamily.value());
    } else {
        // Share graphics command pool for compute
        m_computeCommandPool = m_commandPool;
        m_logger->log(LogLevel::Info, "Compute shares graphics command pool");
    }
}

SwapChainSupportDetails VulkanDevice::querySwapChainSupport(VkPhysicalDevice device) const {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}
VkFormat VulkanDevice::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            m_logger->log(LogLevel::Debug, "Found supported format (linear tiling): %d", format);
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            m_logger->log(LogLevel::Debug, "Found supported format (optimal tiling): %d", format);
            return format;
        }
    }

    m_logger->log(LogLevel::Error, "Failed to find supported format");
    throw std::runtime_error("Failed to find supported format!");
}
bool VulkanDevice::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

QueueFamilyIndices VulkanDevice::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    // First pass: find graphics and present queues
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        i++;
    }

    // Second pass: find dedicated compute queue (prefer COMPUTE without GRAPHICS)
    for (uint32_t j = 0; j < queueFamilyCount; ++j) {
        const auto& props = queueFamilies[j];
        bool hasCompute = (props.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        bool hasGraphics = (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;

        if (hasCompute && !hasGraphics) {
            // Found a dedicated compute family — ideal for async compute
            indices.computeFamily = j;
            break;
        }
    }

    // Fallback: any family with compute (may be the same as graphics)
    if (!indices.computeFamily.has_value()) {
        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            if (queueFamilies[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                indices.computeFamily = j;
                break;
            }
        }
    }

    // Third pass: find dedicated transfer queue (nice-to-have, not required)
    for (uint32_t j = 0; j < queueFamilyCount; ++j) {
        const auto& props = queueFamilies[j];
        bool hasTransfer = (props.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
        bool hasGraphics = (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        bool hasCompute  = (props.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

        if (hasTransfer && !hasGraphics && !hasCompute) {
            indices.transferFamily = j;
            break;
        }
    }

    return indices;
}

bool VulkanDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

VkCommandBuffer VulkanDevice::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

void VulkanDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

VkMemoryPropertyFlags VulkanDevice::getMemoryProperties() const
{
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

uint32_t VulkanDevice::getGraphicsQueueFamily() const {
    return m_queueFamilyIndices.graphicsFamily.value();
}

uint32_t VulkanDevice::getComputeQueueFamily() const {
    if (m_queueFamilyIndices.computeFamily.has_value()) {
        return m_queueFamilyIndices.computeFamily.value();
    }
    return m_queueFamilyIndices.graphicsFamily.value();
}

VkFormat VulkanDevice::findDepthFormat()
{
    return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

} // namespace Shoonyakasha

