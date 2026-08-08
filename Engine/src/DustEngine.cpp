// DustEngine.cpp
#include "DustEngine.hpp"
#include "Core/Rendering/ParticleComputeShaders.hpp"
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

    if (m_particleComputePipeline) { vkDestroyPipeline(vulkan.device, m_particleComputePipeline, nullptr); m_particleComputePipeline = VK_NULL_HANDLE; }
    if (m_particleComputeLayout)   { vkDestroyPipelineLayout(vulkan.device, m_particleComputeLayout, nullptr); m_particleComputeLayout = VK_NULL_HANDLE; }

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
    activeCamRight = camera.right();
    activeCamUp    = camera.up();
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

// ── Textures ─────────────────────────────────────────────────────────────

Texture DustEngine::loadTexture(const char* path) {
    return Texture::loadFromFile(vulkan, path);
}

void DustEngine::unloadTexture(Texture& tex) {
    vkDeviceWaitIdle(vulkan.device);
    tex.destroy(vulkan);
}

VkDescriptorSet DustEngine::createTextureSet(const Texture& tex) {
    if (!window) return VK_NULL_HANDLE;
    return window->renderer.createMaterialSet(vulkan, tex);
}

// ── Billboards ────────────────────────────────────────────────────────────

void DustEngine::drawBillboard(glm::vec3 position, float size,
                                VkDescriptorSet texSet,
                                glm::vec4 color) {
    if (!frameValid || !window) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }
    window->renderer.drawBillboard(window->renderer.cmd(),
        activeViewProj, activeCamRight, activeCamUp,
        position, size, color, texSet);
}

// ── Particles ─────────────────────────────────────────────────────────────

void DustEngine::drawParticles(ParticleSystem& ps, VkDescriptorSet texSet) {
    if (!frameValid || !window) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }
    window->renderer.drawParticles(window->renderer.cmd(),
        ps, activeViewProj, activeCamRight, activeCamUp, texSet);
}

bool DustEngine::ensureComputePipeline() {
    if (m_particleComputePipeline != VK_NULL_HANDLE) return true;

    // SSBO binding for the particle buffer
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding        = 0;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount= 1;
    ssboBinding.stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayout compSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings    = &ssboBinding;
    if (vkCreateDescriptorSetLayout(vulkan.device, &dslInfo, nullptr, &compSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.size       = 16; // float dt, gravityY, drag, uint particleCount

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &compSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, nullptr, &m_particleComputeLayout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(vulkan.device, compSetLayout, nullptr);
        return false;
    }
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = particles_comp_spv_len;
    smInfo.pCode    = (uint32_t*)particles_comp_spv;
    VkShaderModule compModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &smInfo, nullptr, &compModule) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(vulkan.device, compSetLayout, nullptr);
        return false;
    }

    VkComputePipelineCreateInfo cpInfo{};
    cpInfo.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpInfo.stage.module = compModule;
    cpInfo.stage.pName  = "main";
    cpInfo.layout       = m_particleComputeLayout;
    bool ok = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &cpInfo, nullptr,
                                        &m_particleComputePipeline) == VK_SUCCESS;
    vkDestroyShaderModule(vulkan.device, compModule, nullptr);
    // Destroyed only *after* pipeline creation — the pipeline layout keeps a
    // reference to it for as long as it's used to create pipelines.
    vkDestroyDescriptorSetLayout(vulkan.device, compSetLayout, nullptr);
    return ok;
}

void DustEngine::dispatchParticles(ParticleSystem& ps, float dt,
                                    float gravityY, float drag,
                                    VkPipeline computePipeline,
                                    VkPipelineLayout computeLayout) {
    if (!window || ps.maxParticles() == 0) return;
    if (!ensureComputePipeline() && computePipeline == VK_NULL_HANDLE) return;

    VkPipeline       usePipeline = computePipeline  != VK_NULL_HANDLE ? computePipeline  : m_particleComputePipeline;
    VkPipelineLayout useLayout   = computeLayout    != VK_NULL_HANDLE ? computeLayout    : m_particleComputeLayout;

    // One-shot command buffer for the compute dispatch (runs before the frame's graphics cmd).
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = vulkan.graphicsFamily;
    VkCommandPool pool;
    vkCreateCommandPool(vulkan.device, &poolInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo cbInfo{};
    cbInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool        = pool;
    cbInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vulkan.device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Allocate a temporary descriptor set for the SSBO.
    // We use a tiny throwaway pool — this is fine for now since
    // dispatchParticles is called once per frame, not in a tight inner loop.
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding        = 0;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount= 1;
    ssboBinding.stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings    = &ssboBinding;
    VkDescriptorSetLayout dsl;
    vkCreateDescriptorSetLayout(vulkan.device, &dslInfo, nullptr, &dsl);

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets       = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes    = &poolSize;
    VkDescriptorPool dp;
    vkCreateDescriptorPool(vulkan.device, &dpInfo, nullptr, &dp);

    VkDescriptorSetAllocateInfo dsaInfo{};
    dsaInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsaInfo.descriptorPool     = dp;
    dsaInfo.descriptorSetCount = 1;
    dsaInfo.pSetLayouts        = &dsl;
    VkDescriptorSet ds;
    vkAllocateDescriptorSets(vulkan.device, &dsaInfo, &ds);

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = ps.particleBuffer();
    bufInfo.offset = 0;
    bufInfo.range  = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = ds;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo     = &bufInfo;
    vkUpdateDescriptorSets(vulkan.device, 1, &write, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, usePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, useLayout, 0, 1, &ds, 0, nullptr);

    struct { float dt; float gravityY; float drag; uint32_t count; } pc;
    pc.dt       = dt;
    pc.gravityY = gravityY;
    pc.drag     = drag;
    pc.count    = ps.maxParticles();
    vkCmdPushConstants(cmd, useLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    uint32_t groups = (ps.maxParticles() + 63) / 64;
    vkCmdDispatch(cmd, groups, 1, 1);

    // Barrier: ensure compute writes are visible to vertex shader reads
    VkBufferMemoryBarrier barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.buffer        = ps.particleBuffer();
    barrier.offset        = 0;
    barrier.size          = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 0, nullptr, 1, &barrier, 0, nullptr);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(vulkan.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkan.graphicsQueue);

    vkDestroyDescriptorPool(vulkan.device, dp, nullptr);
    vkDestroyDescriptorSetLayout(vulkan.device, dsl, nullptr);
    vkDestroyCommandPool(vulkan.device, pool, nullptr);
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

    // Lay out against the *swapchain* extent, not the framebuffer size. They
    // normally match, but the surface can clamp the swapchain to something
    // smaller — and since the rects we hand the shader are divided by this
    // same extent, using the window size instead would draw the whole UI
    // magnified with its bottom/right edge outside the render area.
    VkExtent2D screenSize = window->swapchain.extent;
    UI::Rect viewport{ 0.0f, 0.0f, (float)screenSize.width, (float)screenSize.height };
    uiRoot.layout(viewport);

    // ── Input — hit-test + hover/click/focus dispatch (UITimeline.md Phase 3) ──
    // Reads window->input directly (not the isKeyDown()/isMouseButtonDown()
    // wrappers) — those are capture-aware and would just read back
    // whatever uiHoveredId/uiWantsMouse() already was *last* frame, which is
    // exactly the stale state this pass exists to update.
    {
        // Mouse comes in framebuffer pixels; widget rects are in swapchain
        // pixels. Same numbers whenever the two agree, which is the norm.
        float mouseX = (float)window->input.mouseX *
                       (window->width  ? (float)screenSize.width  / (float)window->width  : 1.0f);
        float mouseY = (float)window->input.mouseY *
                       (window->height ? (float)screenSize.height / (float)window->height : 1.0f);

        const UI::Widget* hit = nullptr;
        uiRoot.forEachInteractive([&](const UI::Widget& w) {
            const UI::Rect& r = w.computedRect;
            if (mouseX >= r.x && mouseX <= r.x + r.w && mouseY >= r.y && mouseY <= r.y + r.h)
                hit = &w; // keep overwriting — later in the walk means drawn on top (child over parent)
        });

        uiHoveredId = hit ? hit->computedId : 0;
        if (hit && hit->onHoverFn) hit->onHoverFn();

        bool leftPressed  = window->input.mousePressed[GLFW_MOUSE_BUTTON_LEFT];
        bool leftReleased = window->input.mouseReleased[GLFW_MOUSE_BUTTON_LEFT];

        if (leftPressed) {
            uiPressedId = uiHoveredId;
            uiFocusedId = (hit && hit->isFocusable()) ? uiHoveredId : 0; // click elsewhere (or a non-focusable widget) blurs
        }
        if (leftReleased) {
            if (hit && uiPressedId == uiHoveredId && hit->onClickFn)
                hit->onClickFn();
            uiPressedId = 0;
        }

        if (uiFocusedId != 0) {
            uiRoot.forEachInteractive([&](const UI::Widget& w) {
                if (w.computedId == uiFocusedId && w.onFocusFn) w.onFocusFn();
            });
        }
    }

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
            float sizePx = w.textSize.resolve(w.computedRect.h, (float)screenSize.height);

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

// ── Input ────────────────────────────────────────────────────────────────

bool DustEngine::uiWantsMouse() const    { return uiHoveredId != 0; }
bool DustEngine::uiWantsKeyboard() const { return uiFocusedId != 0; }

bool DustEngine::isKeyDown(int key, bool ignoreUICapture) const {
    if (!window || key < 0 || key > GLFW_KEY_LAST) return false;
    if (!ignoreUICapture && uiWantsKeyboard()) return false;
    return window->input.keysDown[key];
}
bool DustEngine::isKeyPressed(int key, bool ignoreUICapture) const {
    if (!window || key < 0 || key > GLFW_KEY_LAST) return false;
    if (!ignoreUICapture && uiWantsKeyboard()) return false;
    return window->input.keysPressed[key];
}
bool DustEngine::isKeyReleased(int key, bool ignoreUICapture) const {
    if (!window || key < 0 || key > GLFW_KEY_LAST) return false;
    if (!ignoreUICapture && uiWantsKeyboard()) return false;
    return window->input.keysReleased[key];
}

bool DustEngine::isMouseButtonDown(int button, bool ignoreUICapture) const {
    if (!window || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    if (!ignoreUICapture && uiWantsMouse()) return false;
    return window->input.mouseDown[button];
}
bool DustEngine::isMouseButtonPressed(int button, bool ignoreUICapture) const {
    if (!window || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    if (!ignoreUICapture && uiWantsMouse()) return false;
    return window->input.mousePressed[button];
}
bool DustEngine::isMouseButtonReleased(int button, bool ignoreUICapture) const {
    if (!window || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    if (!ignoreUICapture && uiWantsMouse()) return false;
    return window->input.mouseReleased[button];
}

glm::vec2 DustEngine::mousePosition() const {
    if (!window) return { 0.0f, 0.0f };
    return { (float)window->input.mouseX, (float)window->input.mouseY };
}
glm::vec2 DustEngine::mouseDelta(bool ignoreUICapture) const {
    if (!window) return { 0.0f, 0.0f };
    if (!ignoreUICapture && uiWantsMouse()) return { 0.0f, 0.0f };
    return { (float)window->input.mouseDeltaX, (float)window->input.mouseDeltaY };
}
float DustEngine::mouseScrollY(bool ignoreUICapture) const {
    if (!window) return 0.0f;
    if (!ignoreUICapture && uiWantsMouse()) return 0.0f;
    return (float)window->input.scrollY;
}

const std::string& DustEngine::typedTextThisFrame() const {
    static const std::string empty;
    return window ? window->input.textInput : empty;
}

} // namespace Dust
