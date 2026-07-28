#pragma once

#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Swapchain.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/FrameData.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
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

    std::vector<VkSemaphore> renderFinishedSemaphores;
VkExtent2D currentExtent = {};
    vkb::DispatchTable dispatch;
    void draw(VkCommandBuffer cmd, Mesh& mesh,
              VkPipeline pipeline, VkPipelineLayout layout,
              const float transform[16] = nullptr);

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
