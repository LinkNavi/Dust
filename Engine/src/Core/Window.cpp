#include "Core/Window.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include <cstring>
#include <cstdio>

namespace Dust {

Window& WindowManager::create(const WindowConfig& cfg, VulkanContext& ctx) {
    Window w{};
    w.name       = cfg.name;
    w.title      = cfg.title;
    w.width      = cfg.width;
    w.height     = cfg.height;
    w.resizable  = cfg.resizable;
    w.fullscreen = cfg.fullscreen;
    w.vsync      = cfg.vsync;
    w.shouldClose = false;
    w.deltaTime   = 0.0f;
    w.lastTime    = 0.0;
    w.surface     = VK_NULL_HANDLE;
    w.handle      = nullptr;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  cfg.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWmonitor* monitor = cfg.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    w.handle = glfwCreateWindow((int)cfg.width, (int)cfg.height, cfg.title, monitor, nullptr);

    if (!w.handle) {
        fprintf(stderr, "dust: failed to create GLFW window '%s'\n", cfg.name);
        windows.push_back(w);
        return windows.back();
    }

    if (ctx.instance != VK_NULL_HANDLE) {
        if (glfwCreateWindowSurface(ctx.instance, w.handle, nullptr, &w.surface) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create Vulkan surface for window '%s'\n", cfg.name);
        }
    }

    w.swapchain.init(ctx, w);
    windows.push_back(w);
    return windows.back();
}


void WindowManager::destroy(const char* name, VulkanContext& ctx) {
    for (auto it = windows.begin(); it != windows.end(); ++it) {
        if (std::strcmp(it->name, name) == 0) {
            if (it->surface && ctx.instance != VK_NULL_HANDLE)
                vkDestroySurfaceKHR(ctx.instance, it->surface, nullptr);
            if (it->handle)
                glfwDestroyWindow(it->handle);
            windows.erase(it);
            return;
        }
    }
}

Window* WindowManager::get(const char* name) {
    for (auto& w : windows)
        if (std::strcmp(w.name, name) == 0)
            return &w;
    return nullptr;
}

void WindowManager::updateAll() {
    for (auto& w : windows) {
        double now  = glfwGetTime();
        w.deltaTime = (float)(now - w.lastTime);
        w.lastTime  = now;
        w.shouldClose = glfwWindowShouldClose(w.handle);

        int width, height;
        glfwGetFramebufferSize(w.handle, &width, &height);
        w.width  = (uint32_t)width;
        w.height = (uint32_t)height;
    }
}

void WindowManager::pollEvents() {
    glfwPollEvents();
}

} // namespace Dust
