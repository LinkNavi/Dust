// DustEngine.cpp
#include "DustEngine.hpp"
#include <cstdio>

namespace Dust {

bool DustEngine::init(const char* appName) {
    if (!glfwInit()) {
        fprintf(stderr, "dust: failed to init GLFW\n");
        return false;
    }

    if (!vulkan.init(appName, true)) // false in release
        return false;

    root = { ecs.create(), "root", &ecs };
    return true;
}

Entity* DustEngine::createEntity(const char* name, Entity* parent) {
    entities.push_back({ ecs.create(), name, &ecs });
    Entity* e = &entities.back();
    e->setParent(parent ? parent : &root);
    return e;
}

void DustEngine::shutdown() {
    vkDeviceWaitIdle(vulkan.device);

    // 1. destroy swapchains + image views first
    for (auto& w : windows.windows)
        w.swapchain.shutdown(vulkan);

    // 2. destroy surfaces
    for (auto& w : windows.windows) {
        if (w.surface)
            vkDestroySurfaceKHR(vulkan.instance, w.surface, nullptr);
        if (w.handle)
            glfwDestroyWindow(w.handle);
    }
    windows.windows.clear();

    // 3. destroy device + instance last
    vulkan.shutdown();
    glfwTerminate();
}

void DustEngine::run(std::function<void(float dt)> onUpdate) {
    while (running) {
        windows.pollEvents();
        windows.updateAll();

        bool anyOpen = false;
        for (auto& w : windows.windows)
            if (!w.shouldClose) { anyOpen = true; break; }
        if (!anyOpen) break;

        float dt = windows.windows.empty() ? 0.0f : windows.windows[0].deltaTime;

        // TODO: tick ECS systems
        // TODO: begin render frame

        onUpdate(dt);

        // TODO: end render frame / present
    }
}

void DustEngine::stop() { running = false; }

} // namespace Dust
