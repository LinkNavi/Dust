// DustEngine.cpp
#include "DustEngine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstring>

namespace Dust {

namespace {
bool hasSuffix(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}
}

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

    unloadUIFont();

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

void DustEngine::drawMesh(Mesh& mesh, glm::vec3 position, glm::vec3 rotationAxis,
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
    window->renderer.draw(window->renderer.cmd(), mesh,
                          window->renderer.defaultPipeline,
                          window->renderer.defaultLayout,
                          &mvp[0][0],
                          window->renderer.defaultMaterialSet); // untextured — samples the 1x1 white fallback
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

    for (auto& sm : model.submeshes) {
        glm::mat4 mvp = activeViewProj * t * sm.transform;

        const Material* mat = (sm.materialIndex >= 0 && (size_t)sm.materialIndex < model.materials.size())
                             ? &model.materials[sm.materialIndex] : nullptr;
        VkDescriptorSet materialSet = mat ? mat->materialSet : window->renderer.defaultMaterialSet;
        const float*     baseColor  = mat ? mat->baseColor : nullptr;

        window->renderer.draw(window->renderer.cmd(), sm.mesh,
                              window->renderer.defaultPipeline,
                              window->renderer.defaultLayout,
                              &mvp[0][0], materialSet, baseColor);
    }
}

Model DustEngine::loadModel(const char* objPath) {
    Mesh m = Mesh::loadOBJ(objPath);
    m.upload(vulkan);
    return wrapMesh(std::move(m));
}

Model DustEngine::loadModelFromPack(AssetManager& assets, const std::string& name) {
    AssetHandle h = assets.load(name);
    if (!valid(h)) {
        fprintf(stderr, "dust: '%s' not found in pack\n", name.c_str());
        return Model{};
    }
    const std::vector<uint8_t>* bytes = assets.data(h);

    if (hasSuffix(name, ".obj")) {
        Mesh mesh = Mesh::loadOBJFromMemory(bytes->data(), bytes->size());
        assets.release(h); // GPU upload below copies it off
        mesh.upload(vulkan);
        return wrapMesh(std::move(mesh));
    }

    // Anything else is expected to be a DustModel binary — the format
    // DustPacker's assimp importer normalizes every other supported source
    // format into (see AssetManager/ModelFormat.hpp).
    if (!window) {
        fprintf(stderr, "dust: loadModelFromPack() needs a window (material descriptor sets belong to its renderer)\n");
        assets.release(h);
        return Model{};
    }
    Model m = loadModelFromMemory(vulkan, window->renderer, bytes->data(), bytes->size());
    assets.release(h);
    return m;
}

void DustEngine::unloadModel(Model& model) {
    if (!window) return;
    vkDeviceWaitIdle(vulkan.device);
    model.destroy(vulkan, window->renderer);
}

// ── DustUI ───────────────────────────────────────────────────────────────

UI::Widget& DustEngine::beginUI() {
    uiRoot = UI::Widget{};
    uiRoot.size(vw(1.0f), vh(1.0f)); // invisible, spans the full viewport — everything else anchors against it
    return uiRoot;
}

void DustEngine::endUI() {
    if (!frameValid || !window) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }

    UI::Rect viewport{ 0.0f, 0.0f, (float)window->width, (float)window->height };
    uiRoot.layout(viewport);

    VkExtent2D screenSize{ window->width, window->height };
    uiRoot.forEachVisible([&](const UI::Widget& w) {
        float fill[4]   = { w.backgroundColor.r, w.backgroundColor.g, w.backgroundColor.b, w.backgroundColor.a };
        float border[4] = { w.borderColor.r,     w.borderColor.g,     w.borderColor.b,     w.borderColor.a };
        window->renderer.drawUIRect(window->renderer.cmd(),
            w.computedRect.x, w.computedRect.y, w.computedRect.w, w.computedRect.h,
            w.computedBorderWidth, w.computedBorderRadius,
            fill, border, w.opacityValue, screenSize);
    });

    if (uiFontLoaded) {
        std::vector<UI::GlyphInstance> instances;
        uiRoot.forEachText([&](const UI::Widget& w) {
            float sizePx = w.textSize.resolve(w.computedRect.h, (float)window->height);

            // Left-aligned, vertically centered within the widget's content
            // box using the font's em-box (ascender/descender) rather than
            // this string's actual ink — keeps a run of mixed glyphs (some
            // with descenders, some without) from jittering the baseline.
            float textHeight = (uiFont.ascender - uiFont.descender) * sizePx;
            float topY       = w.computedContentRect.y + (w.computedContentRect.h - textHeight) * 0.5f;
            float baselineY  = topY + uiFont.ascender * sizePx;

            UI::layoutText(uiFont, w.textContent, sizePx, w.textColor,
                           w.computedContentRect.x, baselineY, instances);
        });
        if (!instances.empty())
            window->renderer.drawTextInstances(window->renderer.cmd(), uiFont, instances, screenSize);
    }
}

bool DustEngine::loadUIFont(AssetManager& assets, const std::string& name) {
    if (!window) {
        fprintf(stderr, "dust: loadUIFont() needs a window (the atlas descriptor set belongs to its renderer)\n");
        return false;
    }
    AssetHandle h = assets.load(name);
    if (!valid(h)) {
        fprintf(stderr, "dust: '%s' not found in pack\n", name.c_str());
        return false;
    }
    const std::vector<uint8_t>* bytes = assets.data(h);

    if (uiFontLoaded) unloadUIFont();
    uiFont = UI::loadFontFromMemory(vulkan, window->renderer, bytes->data(), bytes->size());
    assets.release(h);

    uiFontLoaded = !uiFont.glyphs.empty();
    return uiFontLoaded;
}

void DustEngine::unloadUIFont() {
    if (!uiFontLoaded || !window) return;
    vkDeviceWaitIdle(vulkan.device);
    uiFont.destroy(vulkan, window->renderer);
    uiFont = UI::Font{};
    uiFontLoaded = false;
}

} // namespace Dust
