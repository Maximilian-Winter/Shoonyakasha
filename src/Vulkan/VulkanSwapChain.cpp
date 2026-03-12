//
// Created by maxim on 05.07.2024.
//

#include "Vulkan/VulkanSwapChain.h"
#include "Vulkan/VulkanDevice.h"
#include "Vulkan/VulkanImage.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace Shoonyakasha {

VulkanSwapChain::VulkanSwapChain(VulkanDevice& device, VkSurfaceKHR surface, VkExtent2D windowExtent)
    : m_device(device)
    , m_surface(surface)
    , m_windowExtent(windowExtent)
    , m_swapChain(VK_NULL_HANDLE)
    , m_renderPass(VK_NULL_HANDLE)
{
    m_logger = new Logger("vulkan_swapchain.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan SwapChain");
    init();
}

VulkanSwapChain::~VulkanSwapChain() {
    cleanup();
    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanSwapChain::init() {
    createSwapChain();
    createImageViews();
    createDepthResources();
    // Note: Framebuffers are created later when render pass is available
}

void VulkanSwapChain::cleanup() {
    m_logger->log(LogLevel::Info, "Cleaning up Vulkan SwapChain");

    // Clean up framebuffers
    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device.getLogicalDevice(), framebuffer, nullptr);
        }
    }
    m_swapChainFramebuffers.clear();

    // Clean up images and depth buffer
    m_swapChainImages.clear();
    m_depthImage.reset();

    // Clean up swap chain
    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device.getLogicalDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

void VulkanSwapChain::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport();

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = m_device.findQueueFamilies(m_device.getPhysicalDevice());
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device.getLogicalDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create swap chain");
        throw std::runtime_error("Failed to create swap chain!");
    }

    // Get swap chain images
    vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_swapChain, &imageCount, nullptr);
    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(m_device.getLogicalDevice(), m_swapChain, &imageCount, images.data());

    m_swapChainImages.clear();
    for (auto& image : images) {
        auto vulkanImage = std::make_unique<VulkanImage>(
            m_device,
            image,  // Use existing VkImage from swap chain
            surfaceFormat.format,
            extent.width,
            extent.height,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        m_swapChainImages.push_back(std::move(vulkanImage));
    }

    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;

    m_logger->log(LogLevel::Info, "Swap chain created successfully with %u images", imageCount);
    m_eventDispatcher->publish(SwapChainCreatedEvent{m_swapChain, imageCount});
}

void VulkanSwapChain::createImageViews() {
    for (auto& image : m_swapChainImages) {
        image->createImageView(VK_IMAGE_ASPECT_COLOR_BIT);
    }
    m_logger->log(LogLevel::Info, "Swap chain image views created successfully");
}

void VulkanSwapChain::createDepthResources() {
    VkFormat depthFormat = findDepthFormat();

    m_depthImage = std::make_unique<VulkanImage>(
        m_device,
        m_swapChainExtent.width,
        m_swapChainExtent.height,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_depthImage->createImageView(VK_IMAGE_ASPECT_DEPTH_BIT);
    m_logger->log(LogLevel::Info, "Depth resources created successfully");
}

void VulkanSwapChain::createFramebuffers(VkRenderPass renderPass) {
    m_logger->log(LogLevel::Info, "Creating framebuffers");

    // Store render pass for potential recreation
    m_renderPass = renderPass;

    // Clean up existing framebuffers
    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_device.getLogicalDevice(), framebuffer, nullptr);
        }
    }

    m_swapChainFramebuffers.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        std::vector<VkImageView> attachments = {
            m_swapChainImages[i]->getImageView(),
            m_depthImage->getImageView()
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_device.getLogicalDevice(), &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "Failed to create framebuffer");
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }

    m_logger->log(LogLevel::Info, "Framebuffers created successfully");
}

VkFormat VulkanSwapChain::findDepthFormat() {
    return m_device.findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

VkFormat VulkanSwapChain::getDepthFormat() const {
    return m_depthImage ? m_depthImage->getFormat() : VK_FORMAT_UNDEFINED;
}

VkImageView VulkanSwapChain::getDepthImageView() const {
    return m_depthImage ? m_depthImage->getImageView() : VK_NULL_HANDLE;
}

void VulkanSwapChain::recreate() {
    m_logger->log(LogLevel::Info, "Recreating swap chain");

    vkDeviceWaitIdle(m_device.getLogicalDevice());

    // Get old render pass before cleanup
    VkRenderPass oldRenderPass = m_renderPass;

    cleanup();
    init();

    // Recreate framebuffers if we had a render pass
    if (oldRenderPass != VK_NULL_HANDLE) {
        createFramebuffers(oldRenderPass);
    }

    m_eventDispatcher->publish(SwapChainRecreatedEvent{});
}

uint32_t VulkanSwapChain::acquireNextImage(VkSemaphore imageAvailableSemaphore) {
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        m_device.getLogicalDevice(),
        m_swapChain,
        UINT64_MAX,
        imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate();
        return std::numeric_limits<uint32_t>::max(); // Signal that recreation happened
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        m_logger->log(LogLevel::Error, "Failed to acquire swap chain image");
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    return imageIndex;
}

void VulkanSwapChain::present(uint32_t imageIndex, VkSemaphore renderFinishedSemaphore) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;

    VkSwapchainKHR swapChains[] = {m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(m_device.getPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate();
    } else if (result != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to present swap chain image");
        throw std::runtime_error("Failed to present swap chain image!");
    }
}

SwapChainSupportDetails VulkanSwapChain::querySwapChainSupport() {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device.getPhysicalDevice(), m_surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.getPhysicalDevice(), m_surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device.getPhysicalDevice(), m_surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.getPhysicalDevice(), m_surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device.getPhysicalDevice(), m_surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR VulkanSwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VulkanSwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = m_windowExtent;
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

} // namespace Shoonyakasha