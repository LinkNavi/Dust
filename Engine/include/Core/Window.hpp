#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "Core/Rendering/Swapchain.hpp"
#include <vector>
#include <cstdint>

namespace Dust {

    struct VulkanContext;
struct Window {
    VkClearValue clearColor = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
    // Identity
    const char*  name;
    const char*  title;
    Swapchain swapchain;
    // Display
    uint32_t     width;
    uint32_t     height;
    bool         resizable;
    bool         fullscreen;
    bool         vsync;
    // GLFW
    GLFWwindow*  handle;
    // Vulkan
    VkSurfaceKHR surface;
    // State
    bool         shouldClose;
    float        deltaTime;
    double       lastTime;

    void setClearColor(float r, float g, float b, float a = 1.0f) {
        clearColor = { .color = { .float32 = { r, g, b, a } } };
    }
};

struct WindowConfig {
    const char* name       = "main";
    const char* title      = "Dust";
    uint32_t    width      = 1280;
    uint32_t    height     = 720;
    bool        resizable  = true;
    bool        fullscreen = false;
    bool        vsync      = true;
};

struct WindowManager {
    std::vector<Window> windows;
    Window& create(const WindowConfig& cfg, VulkanContext& ctx);
    void    destroy(const char* name, VulkanContext& ctx);
    Window* get(const char* name);
    void    updateAll();
    void    pollEvents();
};

} // namespace Dust
