// DustEngine.cpp
#include "DustEngine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>

namespace Dust {

bool DustEngine::init(const char* title, uint32_t width, uint32_t height, bool createWindow) {
    if (!glfwInit()) {
        fprintf(stderr, "dust: failed to init GLFW\n");
        return false;
    }

    if (!vulkan.init(title, true)) // false in release
        return false;

    root = { ecs.create(), "root", &ecs };

    if (!createWindow) return true;

    windows.create({ .name = "main", .title = title, .width = width, .height = height }, vulkan);
    window = windows.get("main");
    if (!window) {
        fprintf(stderr, "dust: init failed to create window\n");
        return false;
    }
    return window->renderer.init(vulkan, window->swapchain);
}

Entity* DustEngine::createEntity(const char* name, Entity* parent) {
    entities.push_back({ ecs.create(), name, &ecs });
    Entity* e = &entities.back();
    e->setParent(parent ? parent : &root);
    return e;
}

void DustEngine::shutdown() {
    vkDeviceWaitIdle(vulkan.device);

    for (auto& w : windows.windows) {
        w.renderer.shutdown(vulkan);      // 1. renderer first
        w.swapchain.shutdown(vulkan);     // 2. swapchain
        if (w.surface)
            vkDestroySurfaceKHR(vulkan.instance, w.surface, nullptr); // 3. surface
        if (w.handle)
            glfwDestroyWindow(w.handle);
    }
    windows.windows.clear();
    window = nullptr;

    vulkan.shutdown();                    // 4. device + instance last
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

// ── Raylib-shaped convenience API ──────────────────────────────────────

bool DustEngine::shouldClose() {
    windows.pollEvents();
    windows.updateAll();
    return !window || window->shouldClose;
}

float DustEngine::deltaTime() const {
    return window ? window->deltaTime : 0.0f;
}

void DustEngine::beginDrawing() {
    frameValid     = window && window->renderer.beginFrame(vulkan, *window);
    renderingBegun = false;
}

void DustEngine::clearBackground(float r, float g, float b) {
    if (!window) return;
    window->setClearColor(r, g, b);
    if (!frameValid) return; // swapchain mid-rebuild this frame — nothing to draw into
    window->renderer.beginRendering(vulkan, *window);
    renderingBegun = true;
}

void DustEngine::endDrawing() {
    if (!frameValid) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]); // fall back to last-set color
    }
    window->renderer.endRendering();
    window->renderer.endFrame(vulkan, *window);
}

void DustEngine::beginMode3D(const Camera& camera) {
    if (!window) return;
    float aspect = (float)window->width / (float)(window->height ? window->height : 1);
    activeViewProj = camera.viewProj(aspect);
}

void DustEngine::endMode3D() {
    // No-op for now — kept for symmetry with beginMode3D and raylib parity.
}

void DustEngine::drawModel(Model& model, glm::vec3 position, glm::vec3 rotationAxis,
                           float rotationDeg, glm::vec3 scale) {
    if (!frameValid || !window) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }

    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    if (glm::length(rotationAxis) > 0.0001f)
        t = glm::rotate(t, glm::radians(rotationDeg), glm::normalize(rotationAxis));
    t = glm::scale(t, scale);

    glm::mat4 mvp = activeViewProj * t;
    window->renderer.draw(window->renderer.cmd(), model,
                          window->renderer.defaultPipeline,
                          window->renderer.defaultLayout,
                          &mvp[0][0]);
}

Model DustEngine::loadModel(const char* objPath) {
    Model m = Mesh::loadOBJ(objPath);
    m.upload(vulkan);
    return m;
}

Model DustEngine::loadModelFromPack(AssetManager& assets, const std::string& name) {
    Model m;
    AssetHandle h = assets.load(name);
    if (!valid(h)) {
        fprintf(stderr, "dust: '%s' not found in pack\n", name.c_str());
        return m;
    }
    const std::vector<uint8_t>* bytes = assets.data(h);
    m = Mesh::loadOBJFromMemory(bytes->data(), bytes->size());
    assets.release(h); // GPU upload below copies it off
    m.upload(vulkan);
    return m;
}

void DustEngine::unloadModel(Model& model) {
    vkDeviceWaitIdle(vulkan.device);
    model.destroy(vulkan);
}

} // namespace Dust
