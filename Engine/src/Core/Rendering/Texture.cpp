#include "Core/Rendering/Texture.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include <cstdio>
#include <cstring>
#include <functional>

namespace Dust {

namespace {

// Load-time-only helper — creates a throwaway pool/buffer, records `fn`,
// submits, and blocks until done. Not meant for the per-frame hot path
// (texture uploads happen once when a model loads, not every frame).
void oneShotCommand(VulkanContext& ctx, const std::function<void(VkCommandBuffer)>& fn) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.graphicsFamily;

    VkCommandPool pool;
    vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = pool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    fn(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &cmd;
    vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphicsQueue);

    vkFreeCommandBuffers(ctx.device, pool, 1, &cmd);
    vkDestroyCommandPool(ctx.device, pool, nullptr);
}

void transitionLayout(VkCommandBuffer cmd, VkImage image,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.image               = image;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = srcAccess;
    barrier.dstAccessMask       = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace

bool Texture::upload(VulkanContext& ctx, const uint8_t* rgba8, uint32_t w, uint32_t h, bool srgb) {
    if (!rgba8 || w == 0 || h == 0) return false;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;

    // Staging buffer (CPU-visible)
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = size;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo stagingAllocInfo{};
    stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer      staging;
    VmaAllocation stagingAlloc;
    if (vmaCreateBuffer(ctx.allocator, &bufInfo, &stagingAllocInfo, &staging, &stagingAlloc, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create texture staging buffer\n");
        return false;
    }
    void* mapped;
    vmaMapMemory(ctx.allocator, stagingAlloc, &mapped);
    memcpy(mapped, rgba8, (size_t)size);
    vmaUnmapMemory(ctx.allocator, stagingAlloc);

    // GPU image
    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent        = { w, h, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator, &imgInfo, &imgAllocInfo, &image, &alloc, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create texture image\n");
        vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);
        return false;
    }

    oneShotCommand(ctx, [&](VkCommandBuffer cmd) {
        transitionLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          0, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { w, h, 1 };
        vkCmdCopyBufferToImage(cmd, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        transitionLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });

    vmaDestroyBuffer(ctx.allocator, staging, stagingAlloc);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = image;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = imgInfo.format;
    viewInfo.subresourceRange                = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create texture image view\n");
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter    = VK_FILTER_LINEAR;
    samplerInfo.minFilter    = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod       = 1.0f; // no mips yet
    if (vkCreateSampler(ctx.device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create texture sampler\n");
        return false;
    }

    width  = w;
    height = h;
    return true;
}

void Texture::destroy(VulkanContext& ctx) {
    if (sampler) { vkDestroySampler(ctx.device, sampler, nullptr); sampler = VK_NULL_HANDLE; }
    if (view)    { vkDestroyImageView(ctx.device, view, nullptr);  view    = VK_NULL_HANDLE; }
    if (image)   { vmaDestroyImage(ctx.allocator, image, alloc);   image   = VK_NULL_HANDLE; alloc = VK_NULL_HANDLE; }
}

Texture Texture::makeSolid(VulkanContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Texture tex;
    uint8_t px[4] = { r, g, b, a };
    tex.upload(ctx, px, 1, 1);
    return tex;
}

} // namespace Dust
