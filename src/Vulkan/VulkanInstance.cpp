//
// Created by maxim on 05.07.2024.
//

#include "../../include/Vulkan/VulkanInstance.h"
#include <stdexcept>
#include <iostream>
#include <GLFW/glfw3.h>

namespace Shoonyakasha {

VulkanInstance::VulkanInstance(bool enableValidationLayers)
        : m_instance(VK_NULL_HANDLE)
        , m_debugMessenger(VK_NULL_HANDLE)
        , m_validationLayersEnabled(enableValidationLayers)
        , m_logger(nullptr)
        , m_eventDispatcher(nullptr)
{
    m_logger = new Logger("vulkan_instance.log");
    m_eventDispatcher = new EventDispatcher();

    m_logger->log(LogLevel::Info, "Creating Vulkan Instance");
    glfwInit();
    createInstance();
    if (m_validationLayersEnabled) {
        setupDebugMessenger();
    }
}

VulkanInstance::~VulkanInstance() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Instance");

    if (m_validationLayersEnabled) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(m_instance, m_debugMessenger, nullptr);
        }
    }

    vkDestroyInstance(m_instance, nullptr);

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanInstance::createInstance() {
    if (m_validationLayersEnabled && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_validationLayersEnabled) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create Vulkan instance");
        throw std::runtime_error("failed to create instance!");
    }

    m_logger->log(LogLevel::Info, "Vulkan instance created successfully");
}

void VulkanInstance::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    populateDebugMessengerCreateInfo(createInfo);

    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        if (func(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
            m_logger->log(LogLevel::Error, "Failed to set up debug messenger");
            throw std::runtime_error("failed to set up debug messenger!");
        }
    } else {
        m_logger->log(LogLevel::Error, "Failed to find vkCreateDebugUtilsMessengerEXT function");
        throw std::runtime_error("failed to find vkCreateDebugUtilsMessengerEXT function!");
    }

    m_logger->log(LogLevel::Info, "Debug messenger set up successfully");
}

std::vector<const char*> VulkanInstance::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (m_validationLayersEnabled) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}


bool VulkanInstance::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : m_validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void VulkanInstance::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = this;  // Optional
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData) {

    VulkanInstance* instance = static_cast<VulkanInstance*>(pUserData);

    LogLevel logLevel;
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            logLevel = LogLevel::Debug;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            logLevel = LogLevel::Info;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            logLevel = LogLevel::Warning;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            logLevel = LogLevel::Error;
            break;
        default:
            logLevel = LogLevel::Info;
    }

    instance->m_logger->log(logLevel, "Validation layer: %s", pCallbackData->pMessage);

    return VK_FALSE;
}

const std::vector<const char *> &VulkanInstance::getValidationLayers() const
{
    return m_validationLayers;
}

} // namespace Shoonyakasha

