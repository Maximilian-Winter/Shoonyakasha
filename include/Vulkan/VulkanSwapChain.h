//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include <vector>
#include <memory>

#include "VulkanDevice.h"
#include "VulkanImage.h"

namespace Shoonyakasha {

class VulkanDevice;

class VulkanSwapChain {
public:
    VulkanSwapChain(VulkanDevice& device, VkSurfaceKHR surface, VkExtent2D windowExtent);
    ~VulkanSwapChain();

    // Getters
    VkSwapchainKHR getSwapChain() const { return m_swapChain; }
    VkFormat getSwapChainImageFormat() const { return m_swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return m_swapChainExtent; }
    size_t getImageCount() const { return m_swapChainImages.size(); }
    VkFramebuffer getFramebuffer(size_t index) const { return m_swapChainFramebuffers[index]; }

    // Individual swapchain image access (for frame graph import)
    VkImage getSwapChainImage(size_t index) const { return m_swapChainImages[index]->getImage(); }
    VkImageView getSwapChainImageView(size_t index) const { return m_swapChainImages[index]->getImageView(); }

    // Depth buffer access
    VkFormat getDepthFormat() const;
    VkImageView getDepthImageView() const;

    // Swap chain operations
    void recreate();
    uint32_t acquireNextImage(VkSemaphore imageAvailableSemaphore);
    void present(uint32_t imageIndex, VkSemaphore renderFinishedSemaphore);

    // Framebuffer creation - must be called after render pass is created
    void createFramebuffers(VkRenderPass renderPass);

private:
    VulkanDevice& m_device;
    VkSurfaceKHR m_surface;
    VkExtent2D m_windowExtent;

    VkSwapchainKHR m_swapChain;
    std::vector<std::unique_ptr<VulkanImage>> m_swapChainImages;
    VkFormat m_swapChainImageFormat;
    VkExtent2D m_swapChainExtent{};
    std::vector<VkFramebuffer> m_swapChainFramebuffers;

    std::unique_ptr<VulkanImage> m_depthImage;

    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;

    // Store render pass for recreation
    VkRenderPass m_renderPass;

    void init();
    void cleanup();
    void createSwapChain();
    void createImageViews();
    void createDepthResources();
    VkFormat findDepthFormat();

    // Helper methods
    SwapChainSupportDetails querySwapChainSupport();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
};

// Event definitions
struct SwapChainCreatedEvent : public Event {
    VkSwapchainKHR swapChain;
    uint32_t imageCount;
    SwapChainCreatedEvent(VkSwapchainKHR sc, uint32_t count) : swapChain(sc), imageCount(count) {}
};

struct SwapChainRecreatedEvent : public Event {};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanSwapChain;
using Shoonyakasha::SwapChainCreatedEvent;
using Shoonyakasha::SwapChainRecreatedEvent;