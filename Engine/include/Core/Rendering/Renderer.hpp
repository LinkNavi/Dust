#pragma once

#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Swapchain.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Texture.hpp"
#include "Core/Rendering/FrameData.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
#include "Core/UI/UIShaders.hpp"
#include "Core/UI/Font.hpp"
#include "Core/UI/RectInstance.hpp"
#include "Core/Rendering/PipelineBuilder.hpp"
#include <array>
#include <vector>
#include <glm/glm.hpp>

namespace Dust {

struct Window;
class ParticleSystem;

// Push constants layout matching particle.vert
struct ParticlePushConstants {
    glm::mat4 viewProj;
    glm::vec4 camRight; // xyz used
    glm::vec4 camUp;    // xyz used
};

struct Renderer {
    std::array<FrameData, FRAMES_IN_FLIGHT> frames;
    uint32_t                                currentFrame = 0;
    uint32_t                                imageIndex   = 0;
    VkPipelineLayout defaultLayout   = VK_NULL_HANDLE;
    VkPipeline       defaultPipeline = VK_NULL_HANDLE;

    // ── Materials (set=0, binding=0: combined image sampler) ──
    // One shared layout so any pipeline built with
    // `pb.userSetLayout = renderer.materialSetLayout` can bind sets created
    // by createMaterialSet() below — defaultPipeline and every Model
    // material both draw from this same pool.
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      materialPool      = VK_NULL_HANDLE;
    Texture               defaultWhiteTexture;   // "no texture bound" fallback
    VkDescriptorSet       defaultMaterialSet = VK_NULL_HANDLE;

    // Allocates+writes a one-texture material descriptor set from materialPool.
    // Caller owns the returned handle's lifetime via vkFreeDescriptorSets
    // (pool is created with FREE_DESCRIPTOR_SET_BIT) — Model::destroy does
    // this when a model is unloaded.
    VkDescriptorSet createMaterialSet(VulkanContext& ctx, const Texture& tex);

    // ── DustUI ── screen-space rounded-rect/border/sprite quads (see
    // Core/UI/). One shared unit quad, instanced once per widget, so the
    // whole tree costs one draw call per texture switch rather than one per
    // widget (UITimeline.md Phase 4).
    VkPipelineLayout uiLayout   = VK_NULL_HANDLE;
    VkPipeline       uiPipeline = VK_NULL_HANDLE;
    Mesh             uiQuad;

    static constexpr uint32_t kUIInstanceCapacity = 4096;
    VkBuffer      uiInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation uiInstanceAlloc  = VK_NULL_HANDLE;
    void*         uiInstanceMapped = nullptr; // persistently mapped, see Renderer::init
    // Write cursor into the buffer above, reset each beginFrame so several
    // batches within one frame don't overwrite each other.
    uint32_t      uiInstanceCursor = 0;

    // Draws `count` widget quads in a single instanced call. `texSet` is
    // bound for the whole batch — the caller groups instances by texture and
    // calls this once per run (defaultMaterialSet's 1x1 white for untextured
    // widgets, so ui.frag can multiply unconditionally). `upload=false`
    // reuses whatever is already at the same buffer offset, which is how
    // Phase 4's diff skips re-uploading an unchanged UI.
    void drawUIRects(VkCommandBuffer cmd,
                     const UI::RectInstance* instances, uint32_t count,
                     VkDescriptorSet texSet, VkExtent2D screenSize,
                     bool upload = true, VkPipeline pipelineOverride = VK_NULL_HANDLE);

    // Shared by the built-in UI pipeline and every custom shader widget
    // pipeline — one instance layout, so a custom shader can be swapped in
    // without touching the buffer or the batcher. Mirrors UI::RectInstance.
    static std::vector<InstanceAttrib> uiInstanceAttribs();

    // Builds a pipeline pairing the stock ui.vert with a caller-supplied
    // fragment shader (UITimeline.md Phase 9). The resulting pipeline is
    // bound through uiLayout, so it shares push constants and the material
    // set; Renderer owns it and destroys it at shutdown.
    bool buildUIShaderPipeline(VulkanContext& ctx, Swapchain& swapchain,
                               const uint32_t* fragSpv, size_t fragSpvLen,
                               VkPipeline& outPipeline);
    std::vector<VkPipeline>       uiShaderPipelines;
    std::vector<VkPipelineLayout> uiShaderPipelineLayouts;

    // ── Text (MSDF) ── same shared unit quad as DustUI rects, instanced
    // once per glyph — see Shaders/text.vert/frag and Core/UI/Font.hpp.
    VkPipelineLayout textLayout   = VK_NULL_HANDLE;
    VkPipeline       textPipeline = VK_NULL_HANDLE;

    // Single CPU-mapped buffer, rewritten from offset 0 on every call — fine
    // as long as callers don't need two in-flight text draws to coexist
    // within the same frame without one overwriting the other's data before
    // the GPU reads it (same caveat Mesh::updateVertices() documents for the
    // same reason: no per-frame-in-flight duplication yet). One draw call
    // per Font per frame, which is all DustUI needs today.
    static constexpr uint32_t kTextInstanceCapacity = 8192;
    // Write cursor, reset each beginFrame — text is now drawn in several
    // depth-ordered batches per frame rather than one, so they can't all
    // start at offset 0.
    uint32_t      textInstanceCursor = 0;
    VkBuffer      textInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation textInstanceAlloc  = VK_NULL_HANDLE;
    void*         textInstanceMapped = nullptr; // persistently mapped, see Renderer::init

    // Uploads `instances` into the shared instance buffer and issues one
    // instanced draw call against `font`'s atlas. Outline width/colour ride
    // along per glyph (see UI::GlyphInstance), so a single batch can mix
    // outlined and plain runs.
    void drawTextInstances(VkCommandBuffer cmd, const UI::Font& font,
                           const UI::GlyphInstance* instances, uint32_t count,
                           VkExtent2D screenSize);

    // ── Particles ── compute-simulated billboard quads, one per particle slot.
    // Dead particles (life <= 0) are collapsed to zero size in the shader.
    VkPipelineLayout particleLayout   = VK_NULL_HANDLE;
    VkPipeline       particlePipeline = VK_NULL_HANDLE;

    void drawParticles(VkCommandBuffer cmd,
                       ParticleSystem& ps,
                       const glm::mat4& viewProj,
                       const glm::vec3& camRight,
                       const glm::vec3& camUp,
                       VkDescriptorSet materialSet = VK_NULL_HANDLE);

    // ── Billboards ── single camera-facing textured quad, no instancing.
    VkPipelineLayout billboardLayout   = VK_NULL_HANDLE;
    VkPipeline       billboardPipeline = VK_NULL_HANDLE;

    void drawBillboard(VkCommandBuffer cmd,
                       const glm::mat4& viewProj,
                       const glm::vec3& camRight,
                       const glm::vec3& camUp,
                       glm::vec3 position, float size,
                       glm::vec4 color = glm::vec4(1.0f),
                       VkDescriptorSet materialSet = VK_NULL_HANDLE);

    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkExtent2D currentExtent = {};
    // Last framebuffer size we asked the swapchain to match. The surface can
    // legitimately clamp us to something else (min/maxImageExtent), so we
    // only retry when the *request* changes — otherwise beginFrame would
    // rebuild forever and never draw a frame.
    VkExtent2D lastResizeRequest = {};
    vkb::DispatchTable dispatch;
    void draw(VkCommandBuffer cmd, Mesh& mesh,
              VkPipeline pipeline, VkPipelineLayout layout,
              const float transform[16] = nullptr,
              VkDescriptorSet materialSet = VK_NULL_HANDLE,
              const float baseColorFactor[4] = nullptr);

    bool init(VulkanContext& ctx, Swapchain& swapchain);
    void shutdown(VulkanContext& ctx);

    // Call at start of frame — acquires swapchain image, waits on fence
    // Returns false if swapchain needs rebuild
    bool beginFrame(VulkanContext& ctx, Window& window);

    // Call at end of frame — submits command buffer, presents
    // Returns false if swapchain needs rebuild
    bool endFrame(VulkanContext& ctx, Window& window);

    // The command buffer to record into this frame
    VkCommandBuffer cmd() const { return frames[currentFrame].commandBuffer; }

    // Begin/end dynamic rendering (Vulkan 1.3+)
    void beginRendering(VulkanContext& ctx, Window& window);
    void endRendering();

private:
    bool createFrameData(VulkanContext& ctx);
    void destroyFrameData(VulkanContext& ctx);
};

} // namespace Dust
