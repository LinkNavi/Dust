#pragma once

#include <VkBootstrap.h>
#include <vulkan/vulkan.h>
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
};

} // namespace Dust
