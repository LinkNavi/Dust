// DustEngine.cpp
#include "DustEngine.hpp"
#include "Core/Rendering/ParticleComputeShaders.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstring>
#include <cmath>

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

    if (m_particleComputeFence)    { vkDestroyFence(vulkan.device, m_particleComputeFence, nullptr); m_particleComputeFence = VK_NULL_HANDLE; }
    if (m_particleComputeDescPool) { vkDestroyDescriptorPool(vulkan.device, m_particleComputeDescPool, nullptr); m_particleComputeDescPool = VK_NULL_HANDLE; }
    if (m_particleComputePool)     { vkDestroyCommandPool(vulkan.device, m_particleComputePool, nullptr); m_particleComputePool = VK_NULL_HANDLE; }
    if (m_particleComputePipeline) { vkDestroyPipeline(vulkan.device, m_particleComputePipeline, nullptr); m_particleComputePipeline = VK_NULL_HANDLE; }
    if (m_particleComputeLayout)   { vkDestroyPipelineLayout(vulkan.device, m_particleComputeLayout, nullptr); m_particleComputeLayout = VK_NULL_HANDLE; }
    if (m_particleComputeSetLayout) { vkDestroyDescriptorSetLayout(vulkan.device, m_particleComputeSetLayout, nullptr); m_particleComputeSetLayout = VK_NULL_HANDLE; }

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

        // No systems registry exists yet (ecs::Registry has no concept of
        // registered per-frame systems) — nothing to tick here until one is
        // built. onUpdate(dt) is the caller's own per-frame hook in the
        // meantime.
        beginDrawing();

        onUpdate(dt);

        endDrawing();
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
    activeCamera   = camera;

    // No directional light configured = shadow pass is skipped, not an
    // error — see setShadowsEnabled()'s doc comment.
    shadowPassActive = shadowsEnabled && dirLightOn;
    if (shadowPassActive) {
        shadowDrawBuffer.clear();

        // Orthographic frustum around shadowCenter, looking along the sun's
        // travel direction. `up` just needs to not be parallel with the
        // light direction; the near-vertical fallback keeps glm::lookAt
        // well-defined for a near-straight-down sun (the common case).
        glm::vec3 dir = dirLightDirection;
        glm::vec3 up  = (fabsf(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 eye = shadowCenter - dir * shadowFar * 0.5f;
        glm::mat4 lightView = glm::lookAt(eye, eye + dir, up);
        glm::mat4 lightProj = glm::ortho(-shadowHalfExtent, shadowHalfExtent,
                                         -shadowHalfExtent, shadowHalfExtent,
                                         shadowNear, shadowFar);
        // Vulkan clip space is Y-down — same flip Camera::projection applies
        // for the main camera's projection.
        lightProj[1][1] *= -1.0f;
        lightSpaceViewProj = lightProj * lightView;
    }

    // Refreshed once per frame, not per draw — see LightsUBOData's doc
    // comment in Renderer.hpp. Cheap even when no lights are configured
    // (anyLightsActive() == false), so it isn't worth gating.
    updateLightsUBO();
}

void DustEngine::endMode3D() {
    if (!shadowPassActive || !frameValid || !window) { shadowPassActive = false; return; }

    VkCommandBuffer cmd = window->renderer.cmd();

    // vkCmdBeginRendering can't nest — the color pass (started by
    // clearBackground()/the first draw call) has to be suspended around the
    // shadow pass's own dynamic-rendering pass into a different target, then
    // resumed. Re-clearing on resume is harmless: nothing color-side has
    // drawn yet this frame (every lit draw inside this beginMode3D/
    // endMode3D bracket went into shadowDrawBuffer instead — see
    // drawModel()/drawMesh()).
    bool wasRendering = renderingBegun;
    if (wasRendering) window->renderer.endRendering();

    // Replay 1: depth-only into the shadow map, from the light's POV.
    window->renderer.beginShadowPass(cmd);
    for (auto& d : shadowDrawBuffer)
        window->renderer.drawShadow(cmd, *d.mesh, &d.world[0][0], &lightSpaceViewProj[0][0]);
    window->renderer.endShadowPass(cmd);

    if (wasRendering) window->renderer.beginRendering(vulkan, *window);

    // Replay 2: the actual color pass, now sampling the populated shadow map.
    float fogColorPc[4]  = { fogColor.x, fogColor.y, fogColor.z, fogEnabled ? 1.0f : 0.0f };
    float fogParamsPc[4] = { fogStart, fogEnd, 0.0f, 0.0f };
    for (auto& d : shadowDrawBuffer) {
        window->renderer.drawLit(cmd, *d.mesh, d.featureMask,
                                 vulkan, window->swapchain,
                                 &d.world[0][0], d.litMaterialSet,
                                 &d.baseColorFactor[0], fogColorPc, fogParamsPc,
                                 &d.materialParams[0]);
    }

    shadowDrawBuffer.clear();
    shadowPassActive = false;
}

// ── Lighting ─────────────────────────────────────────────────────────────

DustEngine& DustEngine::setDirectionalLight(glm::vec3 direction, glm::vec3 color, float intensity) {
    dirLightOn        = intensity > 0.0f;
    dirLightDirection = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
    dirLightColor     = color;
    dirLightIntensity = intensity;
    return *this;
}

DustEngine& DustEngine::setAmbientLight(glm::vec3 color, float intensity) {
    ambientColor     = color;
    ambientIntensity = intensity;
    return *this;
}

int DustEngine::addPointLight(glm::vec3 pos, glm::vec3 color, float intensity, float radius) {
    for (int i = 0; i < kMaxPointLights; i++) {
        if (pointLights[i].active) continue;
        pointLights[i] = { true, pos, color, intensity, radius };
        return i;
    }
    fprintf(stderr, "dust: addPointLight() — all %d slots in use\n", kMaxPointLights);
    return -1;
}

void DustEngine::removePointLight(int handle) {
    if (handle >= 0 && handle < kMaxPointLights) pointLights[handle].active = false;
}

int DustEngine::addSpotLight(glm::vec3 pos, glm::vec3 direction, glm::vec3 color, float intensity,
                             float range, float innerDeg, float outerDeg) {
    for (int i = 0; i < kMaxSpotLights; i++) {
        if (spotLights[i].active) continue;
        SpotLightSlot& s = spotLights[i];
        s.active    = true;
        s.pos       = pos;
        s.dir       = glm::length(direction) > 0.0001f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
        s.color     = color;
        s.intensity = intensity;
        s.range     = range;
        s.innerCos  = cosf(glm::radians(innerDeg));
        s.outerCos  = cosf(glm::radians(outerDeg));
        return i;
    }
    fprintf(stderr, "dust: addSpotLight() — all %d slots in use\n", kMaxSpotLights);
    return -1;
}

void DustEngine::removeSpotLight(int handle) {
    if (handle >= 0 && handle < kMaxSpotLights) spotLights[handle].active = false;
}

bool DustEngine::anyLightsActive() const {
    if (dirLightOn) return true;
    for (auto& p : pointLights) if (p.active) return true;
    for (auto& s : spotLights)  if (s.active)  return true;
    return false;
}

void DustEngine::updateLightsUBO() {
    if (!window) return;

    LightsUBOData data{};
    data.viewProj     = activeViewProj;
    data.cameraPos    = glm::vec4(activeCamera.position, 0.0f);
    data.dirLightDir  = glm::vec4(dirLightDirection, 0.0f);
    data.dirLightColor= glm::vec4(dirLightColor, dirLightOn ? dirLightIntensity : 0.0f);
    data.ambient      = glm::vec4(ambientColor, ambientIntensity);

    uint32_t pointCount = 0;
    for (auto& p : pointLights) {
        if (!p.active || pointCount >= kMaxPointLights) continue;
        GPUPointLight& gp = data.pointLights[pointCount++];
        gp.posRadius      = glm::vec4(p.pos, p.radius);
        gp.colorIntensity = glm::vec4(p.color, p.intensity);
    }

    uint32_t spotCount = 0;
    for (auto& s : spotLights) {
        if (!s.active || spotCount >= kMaxSpotLights) continue;
        GPUSpotLight& gs  = data.spotLights[spotCount++];
        gs.posRange       = glm::vec4(s.pos, s.range);
        gs.dirInnerCos    = glm::vec4(s.dir, s.innerCos);
        gs.colorIntensity = glm::vec4(s.color, s.intensity);
        gs.outerCos       = glm::vec4(s.outerCos, 0.0f, 0.0f, 0.0f);
    }

    data.lightCounts = glm::vec4((float)pointCount, (float)spotCount, 0.0f, 0.0f);
    // Only meaningful when shadowPassActive wrote a fresh one this frame —
    // lit.frag only reads it when HAS_SHADOWS is set, so a stale matrix here
    // otherwise is harmless.
    data.lightSpaceViewProj = lightSpaceViewProj;

    window->renderer.updateLights(data);
}

void DustEngine::setCursorLocked(bool locked) {
    if (!window || !window->handle) return;
    glfwSetInputMode(window->handle, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void DustEngine::drawMesh(Mesh& mesh, glm::vec3 position, glm::vec3 rotationAxis,
                          float rotationDeg, glm::vec3 scale) {
    if (!frameValid || !window) return;
    // Cheap point-distance cull — skip the draw entirely once the object is
    // past the far plane instead of relying on fog/GPU clipping to hide it
    // (fog end is usually much shorter than farClip in practice, so this is
    // the only thing that actually stops a far-out object from drawing).
    if (glm::distance(activeCamera.position, position) > activeCamera.farClip) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }

    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    if (glm::length(rotationAxis) > 0.0001f)
        t = glm::rotate(t, glm::radians(rotationDeg), glm::normalize(rotationAxis));
    t = glm::scale(t, scale);

    float fogColorPc[4]  = { fogColor.x, fogColor.y, fogColor.z, fogEnabled ? 1.0f : 0.0f };
    float fogParamsPc[4] = { fogStart, fogEnd, 0.0f, 0.0f };

    // Auto lit/unlit selection — see the doc comment on kMaxPointLights.
    if (anyLightsActive()) {
        uint32_t featureMask = shadowPassActive ? LitFeature_Shadows : 0;
        if (shadowPassActive) {
            // Buffer instead of drawing immediately — see endMode3D(), which
            // replays this list into the shadow pass then the color pass.
            shadowDrawBuffer.push_back({
                &mesh, t, featureMask, window->renderer.defaultLitMaterialSet,
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), // Mesh carries no Material — matches drawLit()'s default white baseColorFactor
                glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), // metallic=0, roughness=1, no emissive
            });
            return;
        }
        float materialParams[4] = { 0.0f, 1.0f, 0.0f, 0.0f }; // metallic=0, roughness=1, no emissive — Mesh carries no Material
        window->renderer.drawLit(window->renderer.cmd(), mesh, featureMask,
                                 vulkan, window->swapchain,
                                 &t[0][0], window->renderer.defaultLitMaterialSet,
                                 nullptr, fogColorPc, fogParamsPc, materialParams);
        return;
    }

    glm::mat4 mvp = activeViewProj * t;
    window->renderer.draw(window->renderer.cmd(), mesh,
                          window->renderer.defaultPipeline,
                          window->renderer.defaultLayout,
                          &mvp[0][0],
                          window->renderer.defaultMaterialSet, // untextured — samples the 1x1 white fallback
                          nullptr, fogColorPc, fogParamsPc);
}

void DustEngine::drawModel(Model& model, glm::vec3 position, glm::vec3 rotationAxis,
                           float rotationDeg, glm::vec3 scale) {
    if (!frameValid || !window) return;
    // Same cull as drawMesh() — see comment there.
    if (glm::distance(activeCamera.position, position) > activeCamera.farClip) return;
    if (!renderingBegun) {
        auto& c = window->clearColor.color.float32;
        clearBackground(c[0], c[1], c[2]);
    }

    glm::mat4 t = glm::translate(glm::mat4(1.0f), position);
    if (glm::length(rotationAxis) > 0.0001f)
        t = glm::rotate(t, glm::radians(rotationDeg), glm::normalize(rotationAxis));
    t = glm::scale(t, scale);

    float fogColorPc[4]  = { fogColor.x, fogColor.y, fogColor.z, fogEnabled ? 1.0f : 0.0f };
    float fogParamsPc[4] = { fogStart, fogEnd, 0.0f, 0.0f };
    bool  lit = anyLightsActive(); // auto lit/unlit selection — see kMaxPointLights' doc comment

    for (auto& sm : model.submeshes) {
        glm::mat4 world = t * sm.transform;

        const Material* mat = (sm.materialIndex >= 0 && (size_t)sm.materialIndex < model.materials.size())
                             ? &model.materials[sm.materialIndex] : nullptr;
        const float* baseColor = mat ? mat->baseColor : nullptr;

        if (lit) {
            uint32_t featureMask = shadowPassActive ? LitFeature_Shadows : 0;
            if (mat) {
                if (mat->normalTexture            >= 0) featureMask |= LitFeature_NormalMap;
                if (mat->metallicRoughnessTexture >= 0) featureMask |= LitFeature_MetallicRoughnessMap;
                if (mat->emissiveTexture          >= 0) featureMask |= LitFeature_EmissiveMap;
                if (mat->occlusionTexture         >= 0) featureMask |= LitFeature_OcclusionMap;
            }
            VkDescriptorSet litSet = mat ? mat->litMaterialSet : window->renderer.defaultLitMaterialSet;
            // Emissive tint collapsed to one strength scalar — see lit.frag's
            // comment on materialParams.z for why (128-byte push budget).
            float emissiveStrength = mat ? (mat->emissive[0] + mat->emissive[1] + mat->emissive[2]) / 3.0f : 0.0f;
            float materialParams[4] = { mat ? mat->metallic : 0.0f, mat ? mat->roughness : 1.0f, emissiveStrength, 0.0f };

            if (shadowPassActive) {
                // Buffer instead of drawing immediately — see endMode3D().
                glm::vec4 baseColorFactor = baseColor
                    ? glm::vec4(baseColor[0], baseColor[1], baseColor[2], baseColor[3])
                    : glm::vec4(1.0f);
                shadowDrawBuffer.push_back({
                    &sm.mesh, world, featureMask, litSet, baseColorFactor,
                    glm::vec4(materialParams[0], materialParams[1], materialParams[2], materialParams[3]),
                });
                continue;
            }

            window->renderer.drawLit(window->renderer.cmd(), sm.mesh, featureMask,
                                     vulkan, window->swapchain,
                                     &world[0][0], litSet, baseColor,
                                     fogColorPc, fogParamsPc, materialParams);
        } else {
            glm::mat4 mvp = activeViewProj * world;
            VkDescriptorSet materialSet = mat ? mat->materialSet : window->renderer.defaultMaterialSet;

            window->renderer.draw(window->renderer.cmd(), sm.mesh,
                                  window->renderer.defaultPipeline,
                                  window->renderer.defaultLayout,
                                  &mvp[0][0], materialSet, baseColor,
                                  fogColorPc, fogParamsPc);
        }
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
    float fogColorPc[4]  = { fogColor.x, fogColor.y, fogColor.z, fogEnabled ? 1.0f : 0.0f };
    float fogParamsPc[4] = { fogStart, fogEnd, 0.0f, 0.0f };
    window->renderer.drawParticles(window->renderer.cmd(),
        ps, activeViewProj, activeCamRight, activeCamUp, texSet,
        fogColorPc, fogParamsPc);
}

bool DustEngine::ensureComputePipeline() {
    if (m_particleComputePipeline != VK_NULL_HANDLE) return true;

    // SSBO binding for the particle buffer
    VkDescriptorSetLayoutBinding ssboBinding{};
    ssboBinding.binding        = 0;
    ssboBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ssboBinding.descriptorCount= 1;
    ssboBinding.stageFlags     = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dslInfo{};
    dslInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslInfo.bindingCount = 1;
    dslInfo.pBindings    = &ssboBinding;
    if (vkCreateDescriptorSetLayout(vulkan.device, &dslInfo, nullptr, &m_particleComputeSetLayout) != VK_SUCCESS)
        return false;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.size       = 16; // float dt, gravityY, drag, uint particleCount

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = 1;
    layoutInfo.pSetLayouts            = &m_particleComputeSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, nullptr, &m_particleComputeLayout) != VK_SUCCESS)
        return false;
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = particles_comp_spv_len;
    smInfo.pCode    = (uint32_t*)particles_comp_spv;
    VkShaderModule compModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &smInfo, nullptr, &compModule) != VK_SUCCESS)
        return false;

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
    if (!ok) return false; // m_particleComputeSetLayout stays alive until shutdown — see its declaration

    // One-time setup for the objects dispatchParticles() reuses every call —
    // only their contents (descriptor writes, recorded commands) change.
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = vulkan.graphicsFamily;
    if (vkCreateCommandPool(vulkan.device, &poolInfo, nullptr, &m_particleComputePool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cbInfo{};
    cbInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbInfo.commandPool        = m_particleComputePool;
    cbInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(vulkan.device, &cbInfo, &m_particleComputeCmd) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpInfo{};
    dpInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpInfo.maxSets       = 1;
    dpInfo.poolSizeCount = 1;
    dpInfo.pPoolSizes    = &poolSize;
    if (vkCreateDescriptorPool(vulkan.device, &dpInfo, nullptr, &m_particleComputeDescPool) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dsaInfo{};
    dsaInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsaInfo.descriptorPool     = m_particleComputeDescPool;
    dsaInfo.descriptorSetCount = 1;
    dsaInfo.pSetLayouts        = &m_particleComputeSetLayout;
    if (vkAllocateDescriptorSets(vulkan.device, &dsaInfo, &m_particleComputeDescSet) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // starts signaled — nothing to wait on yet
    if (vkCreateFence(vulkan.device, &fenceInfo, nullptr, &m_particleComputeFence) != VK_SUCCESS)
        return false;

    return true;
}

void DustEngine::dispatchParticles(ParticleSystem& ps, float dt,
                                    float gravityY, float drag,
                                    VkPipeline computePipeline,
                                    VkPipelineLayout computeLayout) {
    if (!window || ps.maxParticles() == 0) return;
    if (!ensureComputePipeline() && computePipeline == VK_NULL_HANDLE) return;

    VkPipeline       usePipeline = computePipeline  != VK_NULL_HANDLE ? computePipeline  : m_particleComputePipeline;
    VkPipelineLayout useLayout   = computeLayout    != VK_NULL_HANDLE ? computeLayout    : m_particleComputeLayout;

    // Wait for last dispatch's commands to finish before reusing the pool/cmd/
    // descriptor set — by the time we get back here (a whole frame later) the
    // GPU has normally long since finished, so this rarely actually blocks.
    vkWaitForFences(vulkan.device, 1, &m_particleComputeFence, VK_TRUE, UINT64_MAX);
    vkResetFences(vulkan.device, 1, &m_particleComputeFence);

    VkCommandBuffer cmd = m_particleComputeCmd;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Descriptor set structure is allocated once in ensureComputePipeline();
    // only its contents (which particle buffer it points at) update per call.
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = ps.particleBuffer();
    bufInfo.offset = 0;
    bufInfo.range  = VK_WHOLE_SIZE;
    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_particleComputeDescSet;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo     = &bufInfo;
    vkUpdateDescriptorSets(vulkan.device, 1, &write, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, usePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, useLayout, 0, 1, &m_particleComputeDescSet, 0, nullptr);

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
    vkQueueSubmit(vulkan.graphicsQueue, 1, &submitInfo, m_particleComputeFence);
    m_particleComputeFencePending = true;
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

    // World/Hand layer projection (UITimeline.md Phase 10) — must run before
    // layout() since it's what turns worldPos/handOffset into the anchor
    // layout() actually reads. Top-level widgets only (see the MVP-scope
    // comment on Widget::layer): each is walked directly rather than via
    // forEachWidget so nested World/Hand widgets are left alone rather than
    // silently mis-anchored.
    for (UI::Widget& w : uiRoot.children) {
        if (w.layer != UI::Layer::World && w.layer != UI::Layer::Hand) continue;

        glm::vec3 worldPos = w.worldPos;
        if (w.layer == UI::Layer::Hand) {
            worldPos = activeCamera.position
                     + activeCamera.forward() * w.handOffset.x
                     + activeCamera.right()   * w.handOffset.y
                     + activeCamera.up()      * w.handOffset.z;
        }

        glm::vec4 clip = activeViewProj * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0001f) { w.offScreen = true; continue; } // behind the camera
        w.offScreen = false;

        float ndcX = clip.x / clip.w, ndcY = clip.y / clip.w;
        float screenX = (ndcX * 0.5f + 0.5f) * viewport.w;
        float screenY = (ndcY * 0.5f + 0.5f) * viewport.h;

        // Anchor::Center against the (0,0,W,H) viewport basis resolves to
        // (W/2,H/2) + offset — offsetting by (screenX-W/2, screenY-H/2)
        // lands the widget's center exactly on the projected point.
        w.hasAnchor  = true;
        w.anchorType = UI::Anchor::Center;
        w.anchorOffsetX = px(screenX - viewport.w * 0.5f);
        w.anchorOffsetY = px(screenY - viewport.h * 0.5f);

        // Depth-sort World widgets among themselves only — farther objects
        // get a more negative zIndex so nearer ones draw on top. Not real
        // occlusion against 3D scene geometry (no depth buffer read here),
        // just relative ordering between World-layer widgets — see the
        // MVP-scope comment on Widget::layer.
        if (w.layer == UI::Layer::World) {
            float dist = glm::distance(activeCamera.position, worldPos);
            w.zIndex = -(int)(dist * 100.0f);
        }
    }

    uiRoot.layout(viewport);

    // Scrolling (Phase 7) is a two-pass affair: ids only exist after a
    // layout, and the stored offset is keyed by id, so the first pass exists
    // to hand out ids and measure content extents and the second re-flows
    // with last frame's offsets applied. Skipped entirely when nothing in
    // the tree scrolls.
    bool anyScroll = false;
    uiRoot.forEachScrollableMut([&](UI::Widget& w) {
        auto it = uiScrollOffsets.find(w.computedId);
        if (it != uiScrollOffsets.end() && it->second != 0.0f) {
            float viewLen = (w.layoutMode == UI::LayoutMode::Row) ? w.computedContentRect.w : w.computedContentRect.h;
            float maxOff  = w.computedContentExtent - viewLen;
            if (maxOff < 0.0f) maxOff = 0.0f;
            w.scrollOffset = it->second > maxOff ? maxOff : it->second;
            it->second = w.scrollOffset; // clamp the stored value too, so the wheel doesn't build up slack
            anyScroll  = true;
        }
    });
    if (anyScroll) uiRoot.layout(viewport);

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

        // A widget with .blockInput() swallows everything drawn before it.
        // Draw order is tree order, so "before" is just a lower visit index —
        // the blocker's own descendants come after it and stay live.
        uint32_t visitIdx = 0, blockerIdx = 0;
        uiRoot.forEachInteractive([&](const UI::Widget& w) {
            if (w.blocksInput) blockerIdx = visitIdx;
            visitIdx++;
        });

        visitIdx = 0;
        const UI::Widget* hit = nullptr;
        uiRoot.forEachInteractive([&](const UI::Widget& w) {
            uint32_t myIdx = visitIdx++;
            if (myIdx < blockerIdx) return;
            const UI::Rect& r = w.computedRect;
            const UI::Rect& c = w.computedClipRect;
            // Clipped-away pixels aren't visible, so they aren't clickable
            // either — a scrolled-out list row shouldn't swallow clicks.
            if (mouseX < c.x || mouseX > c.x + c.w || mouseY < c.y || mouseY > c.y + c.h) return;
            if (mouseX >= r.x && mouseX <= r.x + r.w && mouseY >= r.y && mouseY <= r.y + r.h)
                hit = &w; // keep overwriting — later in the walk means drawn on top (child over parent)
        });

        uiHoveredId = hit ? hit->computedId : 0;
        if (hit && hit->onHoverFn) hit->onHoverFn();

        // Wheel goes to the innermost scroll container under the cursor.
        // Parents are visited before children, so the last match wins.
        float wheel = (float)window->input.scrollY;
        if (wheel != 0.0f) {
            const UI::Widget* scroller = nullptr;
            uiRoot.forEachScrollable([&](const UI::Widget& w) {
                const UI::Rect& r = w.computedRect;
                if (mouseX >= r.x && mouseX <= r.x + r.w && mouseY >= r.y && mouseY <= r.y + r.h)
                    scroller = &w;
            });
            if (scroller) {
                float viewLen = (scroller->layoutMode == UI::LayoutMode::Row)
                              ? scroller->computedContentRect.w : scroller->computedContentRect.h;
                float maxOff  = scroller->computedContentExtent - viewLen;
                if (maxOff < 0.0f) maxOff = 0.0f;
                float off = scroller->scrollOffset - wheel * kUIScrollStepPx;
                if (off < 0.0f)     off = 0.0f;
                if (off > maxOff)   off = maxOff;
                // Lands next frame — this frame's layout already happened.
                // One frame of latency on a wheel event is invisible, and it
                // keeps layout a single pass over already-known offsets.
                uiScrollOffsets[scroller->computedId] = off;
            }
        }

        bool leftPressed  = window->input.mousePressed[GLFW_MOUSE_BUTTON_LEFT];
        bool leftReleased = window->input.mouseReleased[GLFW_MOUSE_BUTTON_LEFT];

        if (leftPressed) {
            uiPressedId = uiHoveredId;
            uiFocusedId = (hit && hit->isFocusable()) ? uiHoveredId : 0; // click elsewhere (or a non-focusable widget) blurs
            if (hit && hit->onPressFn) hit->onPressFn();
        }
        // Drag keeps firing on the widget that was pressed even once the
        // cursor outruns it — a drag that dies the moment the mouse leaves
        // the handle is worse than useless.
        if (uiPressedId != 0 && window->input.mouseDown[GLFW_MOUSE_BUTTON_LEFT]) {
            float dx = (float)window->input.mouseDeltaX;
            float dy = (float)window->input.mouseDeltaY;
            if (dx != 0.0f || dy != 0.0f) {
                uiRoot.forEachInteractive([&](const UI::Widget& w) {
                    if (w.computedId == uiPressedId && w.onDragFn) w.onDragFn(dx, dy);
                });
            }
        }

        if (leftReleased) {
            if (hit && hit->onReleaseFn) hit->onReleaseFn();
            // A click is press *and* release over the same widget; releasing
            // somewhere else still fires onRelease, just not onClick.
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

    // ── Flatten the tree into one depth-sorted draw list ──────────────────
    // One walk emits both widget quads and glyph quads, each tagged with the
    // depth key its widget inherited. The list is then stably sorted by that
    // key, so equal keys keep tree order (children over parents) and a
    // higher layer lifts *everything* a widget draws — including its text.
    // Sorting rects and glyphs together is what makes a modal scrim actually
    // cover the UI behind it; with two separate passes all text drew last and
    // showed straight through the scrim.
    uiRects.clear();
    uiGlyphs.clear();
    uiDrawItems.clear();

    uiRoot.forEachWidget([&](const UI::Widget& w) {
        if (w.isVisibleQuad()) {
            UI::RectInstance inst;
            inst.x = w.computedRect.x; inst.y = w.computedRect.y;
            inst.w = w.computedRect.w; inst.h = w.computedRect.h;
            // A sprite with no .background() draws untinted; ui.frag multiplies
            // fill by the texture, and the fallback texture is 1x1 white, so one
            // code path covers plain fills, plain sprites, and tinted sprites.
            inst.fill   = w.hasBackground ? w.backgroundColor
                        : (w.spriteSet != VK_NULL_HANDLE ? Colors::White : Colors::Transparent);
            inst.border = w.borderColor;
            inst.borderWidth  = w.computedBorderWidth;
            inst.borderRadius = w.computedBorderRadius;
            inst.opacity      = w.opacityValue;
            inst.radTL = w.computedRadii[0]; inst.radTR = w.computedRadii[1];
            inst.radBR = w.computedRadii[2]; inst.radBL = w.computedRadii[3];
            // Gradient ends default to their start colour, which makes the
            // shader's mix a no-op — no branch for the flat-fill common case.
            inst.fill2           = w.hasGradient ? w.gradientColor : inst.fill;
            inst.gradAngle       = w.gradientAngle;
            inst.border2         = w.hasBorderGradient ? w.borderGradientColor : inst.border;
            inst.borderGradAngle = w.borderGradientAngle;
            inst.clipMinX = w.computedClipRect.x;
            inst.clipMinY = w.computedClipRect.y;
            inst.clipMaxX = w.computedClipRect.x + w.computedClipRect.w;
            inst.clipMaxY = w.computedClipRect.y + w.computedClipRect.h;
            inst.uvMinX = w.spriteUVMinX; inst.uvMinY = w.spriteUVMinY;
            inst.uvMaxX = w.spriteUVMaxX; inst.uvMaxY = w.spriteUVMaxY;
            inst.texSet   = w.spriteSet;
            inst.pipeline = w.shaderPipeline;
            inst.p0 = w.shaderParams[0]; inst.p1 = w.shaderParams[1];
            inst.p2 = w.shaderParams[2]; inst.p3 = w.shaderParams[3];
            inst.p4 = w.shaderParams[4]; inst.p5 = w.shaderParams[5];
            inst.p6 = w.shaderParams[6]; inst.p7 = w.shaderParams[7];

            // Shadow is its own instance, emitted first so it lands behind the
            // widget — same silhouette and radii, offset and softened. Costs a
            // slot in the buffer, not a draw call.
            if (w.hasShadow) {
                UI::RectInstance sh = inst;
                sh.x += w.shadowOffX;
                sh.y += w.shadowOffY;
                sh.fill        = w.shadowColor;
                sh.fill2       = w.shadowColor;
                sh.border      = Colors::Transparent;
                sh.border2     = Colors::Transparent;
                sh.borderWidth = 0.0f;
                sh.shadowBlur  = w.shadowBlurPx > 0.0f ? w.shadowBlurPx : 1.0f;
                sh.texSet      = VK_NULL_HANDLE; // never tint a shadow with the widget's sprite
                sh.pipeline    = VK_NULL_HANDLE; // and never run it through a custom shader
                uiRects.push_back(sh);
            }
            uiRects.push_back(inst);

            uint32_t count = w.hasShadow ? 2u : 1u;
            uiDrawItems.push_back({ w.computedDepthKey, false,
                                    (uint32_t)(uiRects.size() - count), count });
        }

        if (uiFontLoaded && w.hasDrawableText()) {
            float sizePx = w.textSize.resolve(w.computedRect.h, (float)screenSize.height);
            size_t runStart = uiGlyphs.size();

            // Wrapping, alignment, and multi-line placement all live in
            // layoutTextBox() — see Core/UI/Font.cpp.
            UI::layoutTextBox(uiFont, w.textContent, sizePx, w.textColor,
                              w.computedContentRect, w.textHAlign, w.textVAlign,
                              w.textWrap ? w.computedContentRect.w : 0.0f,
                              uiGlyphs);

            float outline = w.textOutlineWidth.resolve(sizePx, (float)screenSize.height);
            const UI::Rect& box = w.computedContentRect;
            float invBoxW = box.w > 0.0f ? 1.0f / box.w : 0.0f;
            float invBoxH = box.h > 0.0f ? 1.0f / box.h : 0.0f;

            for (size_t i = runStart; i < uiGlyphs.size(); i++) {
                // Phase 6 — glyphs inherit their widget's clip rect, so text in
                // a clipped/scrolling container is cut at the container edge.
                uiGlyphs[i].clipMinX = w.computedClipRect.x;
                uiGlyphs[i].clipMinY = w.computedClipRect.y;
                uiGlyphs[i].clipMaxX = w.computedClipRect.x + w.computedClipRect.w;
                uiGlyphs[i].clipMaxY = w.computedClipRect.y + w.computedClipRect.h;
                uiGlyphs[i].outlineWidth = outline;
                uiGlyphs[i].outlineColor = w.textOutlineColor;
                // Each glyph carries its own rect expressed in the widget's
                // content box, so the gradient ramp is continuous across the
                // whole block instead of restarting inside every letter.
                uiGlyphs[i].color2    = w.hasTextGradient ? w.textGradientColor : w.textColor;
                uiGlyphs[i].gradAngle = w.textGradientAngle;
                uiGlyphs[i].gradMinX  = (uiGlyphs[i].x - box.x) * invBoxW;
                uiGlyphs[i].gradMinY  = (uiGlyphs[i].y - box.y) * invBoxH;
                uiGlyphs[i].gradMaxX  = (uiGlyphs[i].x + uiGlyphs[i].w - box.x) * invBoxW;
                uiGlyphs[i].gradMaxY  = (uiGlyphs[i].y + uiGlyphs[i].h - box.y) * invBoxH;
            }

            if (uiGlyphs.size() > runStart) {
                uiDrawItems.push_back({ w.computedDepthKey, true, (uint32_t)runStart,
                                        (uint32_t)(uiGlyphs.size() - runStart) });
            }
        }
    });

    std::stable_sort(uiDrawItems.begin(), uiDrawItems.end(),
        [](const UIDrawItem& a, const UIDrawItem& b) { return a.depthKey < b.depthKey; });

    // Re-pack into draw order. The instance buffers have to be contiguous per
    // draw call, so the sort result is materialised rather than indexed into.
    uiSortedRects.clear();
    uiSortedGlyphs.clear();
    for (const UIDrawItem& item : uiDrawItems) {
        if (item.isText) {
            uiSortedGlyphs.insert(uiSortedGlyphs.end(),
                                  uiGlyphs.begin() + item.first,
                                  uiGlyphs.begin() + item.first + item.count);
        } else {
            uiSortedRects.insert(uiSortedRects.end(),
                                 uiRects.begin() + item.first,
                                 uiRects.begin() + item.first + item.count);
        }
    }

    // Diff against last frame — an unchanged UI (the common case for a HUD
    // that only moves when the game state does) skips the memcpy into the
    // mapped instance buffer entirely. The draw calls still have to be
    // re-recorded, since the command buffer is rebuilt every frame.
    bool changed = uiSortedRects.size() != uiPrevRects.size();
    for (size_t i = 0; !changed && i < uiSortedRects.size(); i++)
        changed = !(uiSortedRects[i] == uiPrevRects[i]);

    // Walk the sorted items, merging neighbours that need the same pipeline
    // and texture into one instanced draw. Consecutive text items merge the
    // same way — they all share the one font atlas.
    uint32_t rectCursor = 0, glyphCursor = 0;
    for (size_t i = 0; i < uiDrawItems.size(); ) {
        const UIDrawItem& head = uiDrawItems[i];
        size_t runEnd = i + 1;
        uint32_t total = head.count;

        if (head.isText) {
            while (runEnd < uiDrawItems.size() && uiDrawItems[runEnd].isText) {
                total += uiDrawItems[runEnd].count;
                runEnd++;
            }
            window->renderer.drawTextInstances(window->renderer.cmd(), uiFont,
                                               &uiSortedGlyphs[glyphCursor], total, screenSize);
            glyphCursor += total;
        } else {
            const UI::RectInstance& key = uiSortedRects[rectCursor];
            while (runEnd < uiDrawItems.size() && !uiDrawItems[runEnd].isText &&
                   uiSortedRects[rectCursor + total].texSet   == key.texSet &&
                   uiSortedRects[rectCursor + total].pipeline == key.pipeline) {
                total += uiDrawItems[runEnd].count;
                runEnd++;
            }
            window->renderer.drawUIRects(window->renderer.cmd(), &uiSortedRects[rectCursor],
                                         total, key.texSet, screenSize, changed, key.pipeline);
            rectCursor += total;
        }
        i = runEnd;
    }

    if (changed) uiPrevRects = uiSortedRects;
}

void DustEngine::editFocusedText(std::string& buffer, size_t maxLength) {
    if (!window || !uiWantsKeyboard()) return;
    // ignoreUICapture: the capture-aware wrappers deliberately report nothing
    // while a text widget has focus — this *is* the focused widget reading.
    if (isKeyPressed(GLFW_KEY_BACKSPACE, /*ignoreUICapture=*/true) && !buffer.empty())
        buffer.pop_back();
    const std::string& typed = typedTextThisFrame();
    if (maxLength == 0 || buffer.size() + typed.size() <= maxLength)
        buffer += typed;
}

VkPipeline DustEngine::loadUIShader(const uint32_t* fragSpv, size_t fragSpvLen) {
    if (!window) {
        fprintf(stderr, "dust: loadUIShader() needs a window (the pipeline belongs to its renderer)\n");
        return VK_NULL_HANDLE;
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (!window->renderer.buildUIShaderPipeline(vulkan, window->swapchain, fragSpv, fragSpvLen, pipeline)) {
        fprintf(stderr, "dust: failed to build custom UI shader pipeline\n");
        return VK_NULL_HANDLE;
    }
    window->renderer.uiShaderPipelines.push_back(pipeline);
    return pipeline;
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
