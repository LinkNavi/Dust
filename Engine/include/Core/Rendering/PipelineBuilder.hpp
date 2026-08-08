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

// One vertex attribute sourced from the optional second (instanced) vertex
// binding — see PipelineBuilder::instanceAttribs below.
struct InstanceAttrib {
    uint32_t location;
    VkFormat format;
    uint32_t offset;
};

struct PipelineBuilder {
    ShaderModule        shader;
    VkPrimitiveTopology topology    = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode       polygonMode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags     cullMode    = VK_CULL_MODE_BACK_BIT;
    // Standard authoring convention (matches OBJ export and Camera's
    // projection, which Y-flips to correct for Vulkan's Y-down clip space —
    // see Camera::projection). If you push a transform that skips Camera
    // (e.g. raw identity, no Y-flip), winding will appear reversed.
    VkFrontFace         frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool                depthTest   = false; // off until depth buffer added
    bool                blendEnable = false;

    // User set=1 layout — null means no user descriptors
    VkDescriptorSetLayout userSetLayout = VK_NULL_HANDLE;

    // Push constant — set size=0 to disable
    PushConstantRange pushConstant;

    // Optional second vertex binding (binding=1, VK_VERTEX_INPUT_RATE_INSTANCE)
    // on top of the always-present binding=0 (Mesh::Vertex, per-vertex) —
    // used by the text pipeline to feed one GlyphInstance per instance
    // alongside the shared unit quad. Leave instanceAttribs empty for a
    // normal (non-instanced) pipeline; nothing else changes.
    std::vector<InstanceAttrib> instanceAttribs;
    uint32_t                    instanceStride = 0;

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
