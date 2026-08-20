#include "Core/Rendering/PipelineBuilder.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Swapchain.hpp"
#include <cstdio>

namespace Dust {

// Vertex layout matching default.vert inputs
static VkVertexInputBindingDescription vertexBinding() {
    return { 0, sizeof(float) * 12, VK_VERTEX_INPUT_RATE_VERTEX };
    // 3 pos + 3 normal + 2 uv + 4 color = 12 floats
}

static std::vector<VkVertexInputAttributeDescription> vertexAttribs() {
    return {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0  }, // position
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT,    12 }, // normal
        { 2, 0, VK_FORMAT_R32G32_SFLOAT,       24 }, // uv
        { 3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32 }, // color
    };
}

bool PipelineBuilder::build(VulkanContext& ctx,
                            Swapchain& swapchain,
                            VkPipelineLayout& outLayout,
                            VkPipeline&       outPipeline) {
    // ── Layout ──
    std::vector<VkDescriptorSetLayout> setLayouts;
    // set=0 is always the engine scene UBO — caller must pass sceneSetLayout
    // via userSetLayout slot for now until SceneUBO is wired up
    if (userSetLayout != VK_NULL_HANDLE) {
        setLayouts.push_back(userSetLayout);
        if (userSetLayout2 != VK_NULL_HANDLE)
            setLayouts.push_back(userSetLayout2);
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = (uint32_t)setLayouts.size();
    layoutInfo.pSetLayouts    = setLayouts.data();

    VkPushConstantRange pcRange{};
    if (pushConstant.size > 0) {
        pcRange.stageFlags = pushConstant.stages;
        pcRange.offset     = pushConstant.offset;
        pcRange.size       = pushConstant.size;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges    = &pcRange;
    }

    if (vkCreatePipelineLayout(ctx.device, &layoutInfo, nullptr, &outLayout) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create pipeline layout\n");
        return false;
    }

    // ── Shader stages ──
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = shader.vert;
    stages[0].pName  = "main";

    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = shader.frag;
    stages[1].pName  = "main";

    // ── Specialization constants (fragment stage only, see fragSpecConstants) ──
    std::vector<VkSpecializationMapEntry> specEntries;
    VkSpecializationInfo specInfo{};
    if (!fragSpecConstants.empty()) {
        specEntries.reserve(fragSpecConstants.size());
        for (uint32_t i = 0; i < (uint32_t)fragSpecConstants.size(); i++)
            specEntries.push_back({ i, i * (uint32_t)sizeof(uint32_t), sizeof(uint32_t) });
        specInfo.mapEntryCount = (uint32_t)specEntries.size();
        specInfo.pMapEntries   = specEntries.data();
        specInfo.dataSize      = fragSpecConstants.size() * sizeof(uint32_t);
        specInfo.pData         = fragSpecConstants.data();
        stages[1].pSpecializationInfo = &specInfo;
    }

    // ── Vertex input ──
    std::vector<VkVertexInputBindingDescription> bindings = { vertexBinding() };
    std::vector<VkVertexInputAttributeDescription> attribs = vertexAttribs();

    if (!instanceAttribs.empty()) {
        bindings.push_back({ 1, instanceStride, VK_VERTEX_INPUT_RATE_INSTANCE });
        for (auto& a : instanceAttribs)
            attribs.push_back({ a.location, 1, a.format, a.offset });
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = (uint32_t)bindings.size();
    vertexInput.pVertexBindingDescriptions      = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = (uint32_t)attribs.size();
    vertexInput.pVertexAttributeDescriptions    = attribs.data();

    // ── Input assembly ──
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = topology;

    // ── Viewport (dynamic) ──
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynStates;

    // ── Rasterizer ──
    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = polygonMode;
    raster.cullMode    = cullMode;
    raster.frontFace   = frontFace;
    raster.lineWidth   = 1.0f;
    raster.depthBiasEnable         = depthBiasEnable ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = depthBiasConstant;
    raster.depthBiasSlopeFactor    = depthBiasSlope;

    // ── Multisampling ──
    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // ── Color blend ──
    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttach.blendEnable    = blendEnable ? VK_TRUE : VK_FALSE;
    if (blendEnable) {
        blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttach.colorBlendOp        = VK_BLEND_OP_ADD;
        blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttach.alphaBlendOp        = VK_BLEND_OP_ADD;
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = depthOnly ? 0 : 1;
    colorBlend.pAttachments    = depthOnly ? nullptr : &blendAttach;

    // ── Depth/stencil ──
    // depthWriteEnable follows depthTest — draws that opt out of depth
    // testing (UI, overlays) shouldn't poke holes in the depth buffer for
    // whatever draws after them either.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

    // ── Dynamic rendering (Vulkan 1.3+) ──
    VkPipelineRenderingCreateInfoKHR renderingInfo{};
    renderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    renderingInfo.colorAttachmentCount    = depthOnly ? 0 : 1;
    renderingInfo.pColorAttachmentFormats = depthOnly ? nullptr : &swapchain.imageFormat;
    // Unconditional, unlike depthTestEnable/depthWriteEnable below — every
    // pipeline used inside Renderer::beginRendering()'s pass must declare
    // the bound depth attachment's format, whether or not it actually tests
    // or writes depth (VUID-vkCmdDrawIndexed-dynamicRenderingUnusedAttachments-08914).
    // depthTest=false just means "don't test/write", not "no depth attachment exists".
    renderingInfo.depthAttachmentFormat = depthAttachmentFormat != VK_FORMAT_UNDEFINED
                                         ? depthAttachmentFormat : swapchain.depthFormat;

    // ── Create ──
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &renderingInfo;
    pipelineInfo.stageCount          = 2;
    pipelineInfo.pStages             = stages;
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState   = &msaa;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = outLayout;
    pipelineInfo.renderPass          = VK_NULL_HANDLE; // dynamic rendering

    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1,
                                  &pipelineInfo, nullptr, &outPipeline) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create graphics pipeline\n");
        return false;
    }

    return true;
}

} // namespace Dust
