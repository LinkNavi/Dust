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

    // Depth-only pipeline (shadow pass) — no color attachment at all, not
    // just an unused one. See PipelineBuilder::build's dynamic-rendering
    // setup; depthAttachmentFormat overrides swapchain.depthFormat for
    // render targets that aren't the swapchain's own depth buffer (e.g.
    // Renderer::shadowDepthFormat).
    bool     depthOnly            = false;
    VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED; // VK_FORMAT_UNDEFINED = use swapchain.depthFormat

    // Rasterizer depth bias (shadow pass acne mitigation) — constant +
    // slope-scaled, applied on top of lit.frag's own manual N·L bias.
    bool  depthBiasEnable = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope    = 0.0f;

    // set=0 layout — null means no descriptors at all (and userSetLayout2 is
    // ignored, since Vulkan set numbers can't have gaps).
    VkDescriptorSetLayout userSetLayout = VK_NULL_HANDLE;
    // set=1 layout — e.g. Renderer::lightsSetLayout for the lit pipeline.
    // Null means the pipeline only has set=0.
    VkDescriptorSetLayout userSetLayout2 = VK_NULL_HANDLE;

    // Push constant — set size=0 to disable
    PushConstantRange pushConstant;

    // Optional second vertex binding (binding=1, VK_VERTEX_INPUT_RATE_INSTANCE)
    // on top of the always-present binding=0 (Mesh::Vertex, per-vertex) —
    // used by the text pipeline to feed one GlyphInstance per instance
    // alongside the shared unit quad. Leave instanceAttribs empty for a
    // normal (non-instanced) pipeline; nothing else changes.
    std::vector<InstanceAttrib> instanceAttribs;
    uint32_t                    instanceStride = 0;

    // Fragment-stage specialization constants — one uint32 per constant id
    // (id = index into this vector, matching `layout(constant_id = N)` in
    // the shader; bools/floats reinterpret the same 4 bytes). Empty = no
    // VkSpecializationInfo attached, which is the common case for every
    // pipeline except the lit ubershader's feature-flag variants (see
    // Renderer::getLitPipeline).
    std::vector<uint32_t> fragSpecConstants;

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
