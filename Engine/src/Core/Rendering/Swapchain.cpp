#include "Core/Rendering/Swapchain.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Window.hpp"
#include <cstdio>

namespace Dust {

bool Swapchain::init(VulkanContext& ctx, Window& window) {
    useDynamicRendering = ctx.tier.dynamicRendering;

    vkb::SwapchainBuilder builder{ ctx.vkbDevice, window.surface };
    builder.use_default_format_selection()
           .set_desired_present_mode(
               window.vsync ? VK_PRESENT_MODE_FIFO_KHR
                            : VK_PRESENT_MODE_MAILBOX_KHR)
           .set_desired_extent(window.width, window.height)
           .set_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    auto result = builder.build();
    if (!result) {
        fprintf(stderr, "dust: failed to create swapchain: %s\n",
                result.error().message().c_str());
        return false;
    }

    vkbSwapchain = result.value();
    swapchain    = vkbSwapchain.swapchain;
    imageFormat  = vkbSwapchain.image_format;
    extent       = vkbSwapchain.extent;
    auto imagesResult = vkbSwapchain.get_images();
    if (!imagesResult) {
        fprintf(stderr, "dust: failed to get swapchain images: %s\n",
                imagesResult.error().message().c_str());
        return false;
    }
    images = imagesResult.value();

    auto imageViewsResult = vkbSwapchain.get_image_views();
    if (!imageViewsResult) {
        fprintf(stderr, "dust: failed to get swapchain image views: %s\n",
                imageViewsResult.error().message().c_str());
        return false;
    }
    imageViews = imageViewsResult.value();

    if (!useDynamicRendering) {
        if (!createRenderPass(ctx.device))  return false;
        if (!createFramebuffers(ctx.device)) return false;
    }

    return true;
}

void Swapchain::rebuild(VulkanContext& ctx, Window& window) {
    vkDeviceWaitIdle(ctx.device);
    shutdown(ctx);
    init(ctx, window);
}

void Swapchain::shutdown(VulkanContext& ctx) {
    VkDevice dev = ctx.device;

    destroyFramebuffers(dev);
    destroyImageViews(dev);

    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(dev, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

// ─── PRIVATE ──────────────────────────────────

bool Swapchain::createImageViews(VkDevice device) {
    // vkb already gives us image views — nothing to do here
    // kept for manual rebuild path if needed
    return true;
}

bool Swapchain::createRenderPass(VkDevice device) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = imageFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &colorAttachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dep;

    if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create render pass\n");
        return false;
    }
    return true;
}

bool Swapchain::createFramebuffers(VkDevice device) {
    framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &imageViews[i];
        fbInfo.width           = extent.width;
        fbInfo.height          = extent.height;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create framebuffer %zu\n", i);
            return false;
        }
    }
    return true;
}

void Swapchain::destroyImageViews(VkDevice device) {
    for (auto iv : imageViews)
        vkDestroyImageView(device, iv, nullptr);
    imageViews.clear();
}

void Swapchain::destroyFramebuffers(VkDevice device) {
    for (auto fb : framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers.clear();
}

} // namespace Dust
