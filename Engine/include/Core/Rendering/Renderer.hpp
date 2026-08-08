#pragma once

#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Swapchain.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Texture.hpp"
#include "Core/Rendering/FrameData.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
#include "Core/UI/UIShaders.hpp"
#include "Core/UI/Font.hpp"
#include <array>
#include <vector>

namespace Dust {

struct Window;

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
    VkDescriptorPool       materialPool      = VK_NULL_HANDLE;
    Texture                defaultWhiteTexture;   // "no texture bound" fallback
    VkDescriptorSet         defaultMaterialSet = VK_NULL_HANDLE;

    // Allocates+writes a one-texture material descriptor set from materialPool.
    // Caller owns the returned handle's lifetime via vkFreeDescriptorSets
    // (pool is created with FREE_DESCRIPTOR_SET_BIT) — Model::destroy does
    // this when a model is unloaded.
    VkDescriptorSet createMaterialSet(VulkanContext& ctx, const Texture& tex);

    // ── DustUI ── screen-space rounded-rect/border quads (see Core/UI/).
    // No descriptor set — Phase 1 has no textures/text, just push-constant
    // driven fills. One shared unit quad reused for every widget.
    VkPipelineLayout uiLayout   = VK_NULL_HANDLE;
    VkPipeline       uiPipeline = VK_NULL_HANDLE;
    Mesh             uiQuad;

    // Deliberately takes plain geometry/color, not a UI::Widget — Renderer
    // is a Rendering-layer type and shouldn't need to know the UI layer's
    // types exist. DustEngine::endUI() extracts these from a laid-out widget.
    void drawUIRect(VkCommandBuffer cmd,
                    float x, float y, float w, float h,
                    float borderWidthPx, float borderRadiusPx,
                    const float fillColor[4], const float borderColor[4],
                    float opacity, VkExtent2D screenSize);

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
    VkBuffer      textInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation textInstanceAlloc  = VK_NULL_HANDLE;
    void*         textInstanceMapped = nullptr; // persistently mapped, see Renderer::init

    // Uploads `instances` into the shared instance buffer and issues one
    // instanced draw call against `font`'s atlas. outlineWidthPx=0 (default)
    // disables the outline entirely at no extra cost — see text.frag.
    void drawTextInstances(VkCommandBuffer cmd, const UI::Font& font,
                           const std::vector<UI::GlyphInstance>& instances,
                           VkExtent2D screenSize,
                           float outlineWidthPx = 0.0f, Color outlineColor = Colors::Transparent);

    std::vector<VkSemaphore> renderFinishedSemaphores;
VkExtent2D currentExtent = {};
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
