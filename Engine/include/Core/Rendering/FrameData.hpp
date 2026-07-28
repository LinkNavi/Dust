#pragma once

#include "vulkan/vulkan.hpp"

namespace Dust {

// Per-frame-in-flight data — double buffered by default
struct FrameData {
    VkCommandPool   commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    // CPU waits on this before reusing the frame
    VkFence         renderFence      = VK_NULL_HANDLE;
    // GPU signals this when swapchain image is ready
    VkSemaphore     presentSemaphore = VK_NULL_HANDLE;
    // (the "rendering done" signal semaphore that present() waits on lives
    // in Renderer::renderFinishedSemaphores instead — indexed by swapchain
    // image, not frame-in-flight slot; see the comment on that field)
};

constexpr uint32_t FRAMES_IN_FLIGHT = 2;

} // namespace Dust
