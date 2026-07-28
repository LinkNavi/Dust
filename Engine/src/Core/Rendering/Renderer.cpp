#include "Core/Rendering/Renderer.hpp"
#include "Core/Rendering/PipelineBuilder.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
#include "Core/Window.hpp"
#include <cstdio>
#include <glm/glm.hpp>
namespace Dust {
    bool Renderer::init(VulkanContext& ctx, Swapchain& swapchain) {
        dispatch = ctx.vkbDevice.make_table();

        auto shader = ShaderModule::fromBytes(
            ctx.device,
            (uint32_t*)default_vert_spv, default_vert_spv_len,
            (uint32_t*)default_frag_spv, default_frag_spv_len
        );

        if (!shader.valid()) {
            fprintf(stderr, "dust: failed to load default shaders\n");
            return false;
        }

        PipelineBuilder pb;
        pb.shader       = shader;
        pb.pushConstant = { VK_SHADER_STAGE_VERTEX_BIT, 64 }; // sizeof(mat4)
        pb.build(ctx, swapchain, defaultLayout, defaultPipeline);
        shader.destroy(ctx.device);

        return createFrameData(ctx);
    }

void Renderer::shutdown(VulkanContext& ctx) {
    vkDeviceWaitIdle(ctx.device);
    destroyFrameData(ctx);
}

bool Renderer::beginFrame(VulkanContext& ctx, Window& window) {
    FrameData& frame = frames[currentFrame];

    // Wait for this frame slot to be free
    vkWaitForFences(ctx.device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

    // Lazily size to the swapchain's image count (see the field comment in
    // Renderer.hpp for why this can't just live in FrameData).
    if (renderFinishedSemaphores.size() != window.swapchain.images.size()) {
        for (auto sem : renderFinishedSemaphores)
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(ctx.device, sem, nullptr);
        renderFinishedSemaphores.assign(window.swapchain.images.size(), VK_NULL_HANDLE);
        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (auto& sem : renderFinishedSemaphores)
            vkCreateSemaphore(ctx.device, &semInfo, nullptr, &sem);
    }

    // Acquire next swapchain image
    VkResult result = vkAcquireNextImageKHR(
        ctx.device,
        window.swapchain.swapchain,
        UINT64_MAX,
        frame.presentSemaphore,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        window.swapchain.rebuild(ctx, window);
        return false;
    }

    vkResetFences(ctx.device, 1, &frame.renderFence);

    // Reset + begin command buffer
    vkResetCommandPool(ctx.device, frame.commandPool, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);

    return true;
}

void Renderer::beginRendering(VulkanContext& ctx, Window& window) {
    VkCommandBuffer cmd = frames[currentFrame].commandBuffer;

    // Transition image to color attachment
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image               = window.swapchain.images[imageIndex];
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkRenderingAttachmentInfoKHR colorAttachment{};
    colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachment.imageView   = window.swapchain.imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue  = window.clearColor;

    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea           = { {0, 0}, window.swapchain.extent };
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments    = &colorAttachment;

    // Core (non-KHR) entry point — guaranteed since VulkanContext now
    // requires Vulkan 1.3, where dynamic rendering is core, not an extension.
    dispatch.cmdBeginRendering(cmd, &renderingInfo);
}

void Renderer::endRendering() {
    dispatch.cmdEndRendering(frames[currentFrame].commandBuffer);
}

bool Renderer::endFrame(VulkanContext& ctx, Window& window) {
    VkCommandBuffer cmd = frames[currentFrame].commandBuffer;

    // Transition image to present layout
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.image               = window.swapchain.images[imageIndex];
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask       = 0;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSemaphore renderFinished = renderFinishedSemaphores[imageIndex];

    // Submit
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &frames[currentFrame].presentSemaphore;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &renderFinished;

    vkQueueSubmit(ctx.graphicsQueue, 1, &submitInfo, frames[currentFrame].renderFence);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &window.swapchain.swapchain;
    presentInfo.pImageIndices      = &imageIndex;

    VkResult result = vkQueuePresentKHR(ctx.presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        window.swapchain.rebuild(ctx, window);
        return false;
    }

    currentFrame = (currentFrame + 1) % FRAMES_IN_FLIGHT;
    return true;
}

// ─── PRIVATE ──────────────────────────────────

bool Renderer::createFrameData(VulkanContext& ctx) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = ctx.graphicsFamily;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so first frame doesn't wait forever

    for (auto& frame : frames) {
        if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create command pool\n");
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = frame.commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(ctx.device, &allocInfo, &frame.commandBuffer) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to allocate command buffer\n");
            return false;
        }

        if (vkCreateSemaphore(ctx.device, &semInfo, nullptr, &frame.presentSemaphore) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create semaphores\n");
            return false;
        }

        if (vkCreateFence(ctx.device, &fenceInfo, nullptr, &frame.renderFence) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create fence\n");
            return false;
        }
    }
    return true;
}

void Renderer::destroyFrameData(VulkanContext& ctx) {
    for (auto& frame : frames) {
        if (frame.renderFence)      vkDestroyFence(ctx.device, frame.renderFence, nullptr);
        if (frame.presentSemaphore) vkDestroySemaphore(ctx.device, frame.presentSemaphore, nullptr);
        if (frame.commandPool)      vkDestroyCommandPool(ctx.device, frame.commandPool, nullptr);
    }
    for (auto sem : renderFinishedSemaphores)
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(ctx.device, sem, nullptr);
    renderFinishedSemaphores.clear();
}

} // namespace Dust
