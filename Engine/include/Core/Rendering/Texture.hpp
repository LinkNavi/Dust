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
static Texture loadFromFile(VulkanContext& ctx, const char* path, bool srgb = true);
    // 1x1 solid-color texture. Used as the engine-wide "no texture bound"
    // fallback (Renderer::defaultMaterialSet) so the default shader can
    // always unconditionally sample set=0/binding=0, textured or not.
    // srgb=false is for data fallbacks (e.g. the lit pipeline's flat normal
    // map, (128,128,255) meaning (0,0,1) tangent-space up) — same reasoning
    // as upload()'s srgb param.
    static Texture makeSolid(VulkanContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool srgb = true);
};

// GPU-resident cubemap (6-layer image, VK_IMAGE_VIEW_TYPE_CUBE) + sampler.
// The one texture type the skybox pipeline samples — see Renderer::drawSkybox
// and DustEngine::loadCubemap(). Not a Texture: a Texture is a 2D image with
// a 2D view, and the skybox shader's `samplerCube` needs a cube view over 6
// array layers, which is a different image entirely.
struct Cubemap {
    VkImage       image   = VK_NULL_HANDLE;
    VmaAllocation alloc   = VK_NULL_HANDLE;
    VkImageView   view    = VK_NULL_HANDLE;
    VkSampler     sampler = VK_NULL_HANDLE;
    uint32_t      size    = 0; // edge length (faces are square, all the same size)

    bool valid() const { return image != VK_NULL_HANDLE; }
    void destroy(VulkanContext& ctx);

    // Six RGBA8 faces in the standard cubemap order: +X, -X, +Y, -Y, +Z, -Z.
    // Every face must be `size`x`size`. srgb=true for a color skybox (the
    // usual case); false only if the faces encode data. Returns an invalid
    // Cubemap on any failure.
    static Cubemap fromFacePixels(VulkanContext& ctx, const uint8_t* faces[6],
                                  uint32_t size, bool srgb = true);

    // Same six-face order, loaded from image files (PNG/JPG/BMP/TGA via
    // stb_image). All six must decode to the same square dimensions.
    static Cubemap loadFromFiles(VulkanContext& ctx, const char* facePaths[6], bool srgb = true);
};

} // namespace Dust
