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

    if (!createDepthResources(ctx)) return false;

    return true;
}

void Swapchain::rebuild(VulkanContext& ctx, Window& window) {
    vkDeviceWaitIdle(ctx.device);
    shutdown(ctx);
    init(ctx, window);
}

void Swapchain::shutdown(VulkanContext& ctx) {
    VkDevice dev = ctx.device;

    destroyDepthResources(ctx);
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

// ─── DEPTH BUFFER ─────────────────────────────

namespace {
VkFormat pickDepthFormat(VkPhysicalDevice physicalDevice) {
    // D32_SFLOAT is supported on essentially every Vulkan device (desktop and
    // mobile alike); the S8 variants are fallbacks for the rare device that
    // doesn't expose it. No stencil ops are used, so a stencil-less format is
    // preferred when available (smaller, one less thing to think about).
    static const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (VkFormat fmt : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    return VK_FORMAT_UNDEFINED;
}
}

bool Swapchain::createDepthResources(VulkanContext& ctx) {
    depthFormat = pickDepthFormat(ctx.physicalDevice);
    if (depthFormat == VK_FORMAT_UNDEFINED) {
        fprintf(stderr, "dust: no supported depth format found\n");
        return false;
    }

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = depthFormat;
    imgInfo.extent        = { extent.width, extent.height, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(ctx.allocator, &imgInfo, &allocInfo, &depthImage, &depthAlloc, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create depth image\n");
        return false;
    }

    bool hasStencil = depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                       depthFormat == VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT |
                                                (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(ctx.device, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create depth image view\n");
        return false;
    }

    return true;
}

void Swapchain::destroyDepthResources(VulkanContext& ctx) {
    if (depthImageView) { vkDestroyImageView(ctx.device, depthImageView, nullptr); depthImageView = VK_NULL_HANDLE; }
    if (depthImage)     { vmaDestroyImage(ctx.allocator, depthImage, depthAlloc);  depthImage     = VK_NULL_HANDLE; depthAlloc = VK_NULL_HANDLE; }
}

} // namespace Dust
