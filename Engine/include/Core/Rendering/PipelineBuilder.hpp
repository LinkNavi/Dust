#pragma once

#include "Core/Rendering/ShaderModule.hpp"
#include <vulkan/vulkan.h>
#include <vector>

namespace Dust {

struct VulkanContext;
struct Swapchain;

// Push constant range helper
struct PushConstantRange {
    VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    uint32_t           size   = 0;
    uint32_t           offset = 0;
};

struct PipelineBuilder {
    ShaderModule        shader;
    VkPrimitiveTopology topology    = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode       polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags     cullMode    = VK_CULL_MODE_BACK_BIT;
    VkFrontFace         frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool                depthTest   = false; // off until depth buffer added
    bool                blendEnable = false;

    // User set=1 layout — null means no user descriptors
    VkDescriptorSetLayout userSetLayout = VK_NULL_HANDLE;

    // Push constant — set size=0 to disable
    PushConstantRange pushConstant;

    // Build — creates layout + pipeline, caller owns both
    bool build(VulkanContext& ctx,
               Swapchain& swapchain,
               VkPipelineLayout& outLayout,
               VkPipeline&       outPipeline);
};

// Engine default pipeline — unlit, vertex color
// Built once at renderer init, stored on Renderer
bool buildDefaultPipeline(VulkanContext& ctx,
                          Swapchain& swapchain,
                          VkDescriptorSetLayout sceneSetLayout,
                          VkPipelineLayout& outLayout,
                          VkPipeline&       outPipeline);

} // namespace Dust
