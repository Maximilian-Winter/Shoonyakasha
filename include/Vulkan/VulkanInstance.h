//
// Created by maxim on 05.07.2024.
//

#pragma once

#include <vulkan/vulkan.h>
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include <vector>
#include <string>

namespace Shoonyakasha {

class VulkanInstance {
public:
    explicit VulkanInstance(bool enableValidationLayers = true);
    ~VulkanInstance();

    VkInstance getInstance() const { return m_instance; }
    bool isValidationLayersEnabled() const { return m_validationLayersEnabled; }
    const std::vector<const char*>& getValidationLayers() const;
private:
    VkInstance m_instance;
    VkDebugUtilsMessengerEXT m_debugMessenger;
    bool m_validationLayersEnabled;
    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;
    std::vector<const char*> m_validationLayers;
    void createInstance();
    void setupDebugMessenger();
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
            void* pUserData);
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanInstance;

