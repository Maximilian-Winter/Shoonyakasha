//
// Created by maxim on 05.07.2024.
//


#include "../../include/Vulkan/VulkanWindow.h"
#include "../../include/Vulkan/VulkanInstance.h"
#include <stdexcept>

namespace Shoonyakasha {

VulkanWindow::VulkanWindow(int width, int height, const std::string& title, VulkanInstance& instance, EventDispatcher* eventDispatcher, Logger* logger)
        : m_window(nullptr)
        , m_surface(VK_NULL_HANDLE)
        , m_width(width)
        , m_height(height)
        , m_title(title)
        , m_instance(instance)
{
    m_logger = logger;
    m_eventDispatcher = eventDispatcher;

    m_logger->log(LogLevel::Info, "Creating Vulkan Window");
    initWindow();
    createSurface();
}

VulkanWindow::~VulkanWindow() {
    m_logger->log(LogLevel::Info, "Destroying Vulkan Window");

    vkDestroySurfaceKHR(m_instance.getInstance(), m_surface, nullptr);
    glfwDestroyWindow(m_window);
    glfwTerminate();

    delete m_logger;
    delete m_eventDispatcher;
}

void VulkanWindow::initWindow() {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);
    if (!m_window) {
        m_logger->log(LogLevel::Error, "Failed to create GLFW window");
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferResizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetCursorPosCallback(m_window, cursorPositionCallback);
    glfwSetScrollCallback(m_window, scrollCallback);

    m_logger->log(LogLevel::Info, "GLFW window created successfully");
}

void VulkanWindow::createSurface() {
    VkResult res = glfwCreateWindowSurface(m_instance.getInstance(), m_window, nullptr, &m_surface);
    if (res != VK_SUCCESS) {
        m_logger->log(LogLevel::Error, "Failed to create window surface");
        throw std::runtime_error("Failed to create window surface");
    }

    m_logger->log(LogLevel::Info, "Window surface created successfully");
}

bool VulkanWindow::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void VulkanWindow::pollEvents() {
    glfwPollEvents();
}

void VulkanWindow::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto vulkanWindow = getWindowUserPointer(window);
    vulkanWindow->m_width = width;
    vulkanWindow->m_height = height;
    vulkanWindow->m_logger->log(LogLevel::Info, "Window resized to %dx%d", width, height);
    vulkanWindow->m_eventDispatcher->publish(WindowResizeEvent{width, height});
}

void VulkanWindow::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto vulkanWindow = getWindowUserPointer(window);
    bool pressed = (action != GLFW_RELEASE);
    vulkanWindow->m_logger->log(LogLevel::Debug, "Key event: key=%d, action=%d", key, action);
    vulkanWindow->m_eventDispatcher->publish(KeyEvent{key, pressed});
}

void VulkanWindow::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto vulkanWindow = getWindowUserPointer(window);
    bool pressed = (action == GLFW_PRESS);
    vulkanWindow->m_logger->log(LogLevel::Debug, "Mouse button event: button=%d, action=%d", button, action);
    vulkanWindow->m_eventDispatcher->publish(MouseButtonEvent{button, pressed});
}

void VulkanWindow::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    auto vulkanWindow = getWindowUserPointer(window);
    vulkanWindow->m_logger->log(LogLevel::Debug, "Mouse moved to (%f, %f)", xpos, ypos);
    vulkanWindow->m_eventDispatcher->publish(MouseMoveEvent{static_cast<int>(xpos), static_cast<int>(ypos)});
}

VulkanWindow* VulkanWindow::getWindowUserPointer(GLFWwindow* window) {
    return static_cast<VulkanWindow*>(glfwGetWindowUserPointer(window));
}

void VulkanWindow::scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto vulkanWindow = getWindowUserPointer(window);
    vulkanWindow->m_logger->log(LogLevel::Debug, "Mouse scroll: (%f, %f)", xoffset, yoffset);
    vulkanWindow->m_eventDispatcher->publish(MouseScrollEvent{xoffset, yoffset});
}

void VulkanWindow::setMouseCaptured(bool captured) {
    m_mouseCaptured = captured;
    if (captured) {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // Enable raw mouse motion if available (better for FPS camera)
        if (glfwRawMouseMotionSupported()) {
            glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
    } else {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    m_logger->log(LogLevel::Info, "Mouse capture: %s", captured ? "ON" : "OFF");
}

VkExtent2D VulkanWindow::getWindowExtent(VkPhysicalDevice physicalDevice)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_surface, &capabilities);

    return capabilities.currentExtent;
}

} // namespace Shoonyakasha
