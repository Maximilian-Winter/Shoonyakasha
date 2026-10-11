//
// Created by maxim on 05.07.2024.
//

#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "Core/Logger.h"
#include "Core/EventSystem.h"
#include "Platform/NativeWindow.h"
#include <memory>
#include <string>

namespace Shoonyakasha {

class VulkanInstance;

class VulkanWindow {
public:
    VulkanWindow(int width, int height, const std::string& title, VulkanInstance& instance, EventDispatcher* eventDispatcher, Logger* logger);
    ~VulkanWindow();

    GLFWwindow* getWindow() const { return m_window; }
    VkSurfaceKHR getSurface() const { return m_surface; }

    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    VkExtent2D getWindowExtent(VkPhysicalDevice physicalDevice);
    bool shouldClose() const;
    void pollEvents();

private:
    GLFWwindow* m_window;
    VkSurfaceKHR m_surface;
    int m_width;
    int m_height;
    std::string m_title;
    VulkanInstance& m_instance;

    Logger* m_logger;
    EventDispatcher* m_eventDispatcher;

    void initWindow();
    void createSurface();

    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void charCallback(GLFWwindow* window, unsigned int codepoint);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    static VulkanWindow* getWindowUserPointer(GLFWwindow* window);

public:
    // Mouse capture control for camera systems
    void setMouseCaptured(bool captured);
    bool isMouseCaptured() const { return m_mouseCaptured; }

    // The system clipboard, as UTF-8 text; empty when it holds none.
    std::string getClipboardText() const;
    void setClipboardText(const std::string& text);

    // Input method composition and the window's messages.
    Platform::NativeWindow& getNativeWindow() { return *m_native; }

private:
    bool m_mouseCaptured = false;
    std::unique_ptr<Platform::NativeWindow> m_native;
};

} // namespace Shoonyakasha
using Shoonyakasha::VulkanWindow;

