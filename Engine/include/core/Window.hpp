#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace Dust {

struct Window {
    // Identity
    const char*  name;
    const char*  title;
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
    VkInstance          instance = VK_NULL_HANDLE;

    Window& create(const WindowConfig& cfg);
    void    destroy(const char* name);
    Window* get(const char* name);
    void    updateAll();
    void    pollEvents();
};

} // namespace Dust
