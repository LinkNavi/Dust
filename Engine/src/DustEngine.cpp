// DustEngine.cpp
#include "DustEngine.hpp"
#include <cstdio>

namespace Dust {

bool DustEngine::init(const char* appName) {
    if (!glfwInit()) {
        fprintf(stderr, "dust: failed to init GLFW\n");
        return false;
    }
    root = { ecs.create(), "root", &ecs };
    (void)appName;
    return true;
}

Entity* DustEngine::createEntity(const char* name, Entity* parent) {
    entities.push_back({ ecs.create(), name, &ecs });
    Entity* e = &entities.back();
    e->setParent(parent ? parent : &root);
    return e;
}

void DustEngine::shutdown() {
    for (auto& w : windows.windows) {
        if (w.surface && windows.instance != VK_NULL_HANDLE)
            vkDestroySurfaceKHR(windows.instance, w.surface, nullptr);
        if (w.handle)
            glfwDestroyWindow(w.handle);
    }
    windows.windows.clear();
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
