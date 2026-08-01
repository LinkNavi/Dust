#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace Dust {

struct VulkanContext;

// GPU-resident RGBA8 image + sampler. One Texture per decoded image —
// materials reference these by pointer/index, never own the GPU resources
// twice (Model::textures is the one place these live for imported models).
struct Texture {
    VkImage       image   = VK_NULL_HANDLE;
    VmaAllocation alloc   = VK_NULL_HANDLE;
    VkImageView   view    = VK_NULL_HANDLE;
    VkSampler     sampler = VK_NULL_HANDLE;
    uint32_t      width   = 0;
    uint32_t      height  = 0;

    // Uploads RGBA8 pixel data (width*height*4 bytes) via a one-shot staging
    // buffer + command buffer — meant for load-time use, not per-frame.
    // srgb=true for color textures (base color, emissive); false for data
    // textures (normal maps, metallic/roughness, occlusion) that must stay
    // linear — sampling those as sRGB would silently wreck their values.
    bool upload(VulkanContext& ctx, const uint8_t* rgba8, uint32_t w, uint32_t h, bool srgb = true);
    void destroy(VulkanContext& ctx);
    bool valid() const { return image != VK_NULL_HANDLE; }

    // 1x1 solid-color texture. Used as the engine-wide "no texture bound"
    // fallback (Renderer::defaultMaterialSet) so the default shader can
    // always unconditionally sample set=0/binding=0, textured or not.
    static Texture makeSolid(VulkanContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

} // namespace Dust
