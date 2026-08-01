#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

namespace Dust {

struct VulkanContext; // forward decl
struct Window;

struct Swapchain {
    VkSwapchainKHR           swapchain    = VK_NULL_HANDLE;
    VkFormat                 imageFormat  = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent       = {};

    std::vector<VkImage>     images;
    std::vector<VkImageView> imageViews;

    // Dynamic rendering — no render pass needed on 1.3+
    // Render pass path for 1.0/1.2 fallback
    VkRenderPass             renderPass   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    // Depth buffer — one image shared by every frame (not per-frame-in-
    // flight; depth content never needs to survive past its own frame, so
    // there's no read-after-write hazard across frames to guard against).
    // Resized alongside the swapchain in init()/rebuild().
    VkImage       depthImage     = VK_NULL_HANDLE;
    VmaAllocation depthAlloc     = VK_NULL_HANDLE;
    VkImageView   depthImageView = VK_NULL_HANDLE;
    VkFormat      depthFormat    = VK_FORMAT_UNDEFINED;

    vkb::Swapchain           vkbSwapchain;

    bool useDynamicRendering = false;

    bool init(VulkanContext& ctx, Window& window);
    void rebuild(VulkanContext& ctx, Window& window);
    void shutdown(VulkanContext& ctx);

    uint32_t imageCount() const { return (uint32_t)images.size(); }

private:
    bool createImageViews(VkDevice device);
    bool createRenderPass(VkDevice device);
    bool createFramebuffers(VkDevice device);
    void destroyImageViews(VkDevice device);
    void destroyFramebuffers(VkDevice device);

    bool createDepthResources(VulkanContext& ctx);
    void destroyDepthResources(VulkanContext& ctx);
};

} // namespace Dust
