#include "Core/Rendering/Renderer.hpp"
#include "Core/Rendering/PipelineBuilder.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
#include "Core/Rendering/ParticleShaders.hpp"
#include "Core/Rendering/BillboardShaders.hpp"
#include "Core/Rendering/ParticleSystem.hpp"
#include "Core/UI/TextShaders.hpp"
#include "Core/Window.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <glm/glm.hpp>
namespace Dust {
    bool Renderer::init(VulkanContext& ctx, Swapchain& swapchain) {
        dispatch = ctx.vkbDevice.make_table();

        // ── Material descriptor layout/pool ──
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding         = 0;
        samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = 1;
        setLayoutInfo.pBindings    = &samplerBinding;
        if (vkCreateDescriptorSetLayout(ctx.device, &setLayoutInfo, nullptr, &materialSetLayout) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create material descriptor set layout\n");
            return false;
        }

        static constexpr uint32_t kMaxMaterials = 1024; // models loaded over a game's lifetime
        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxMaterials };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets       = kMaxMaterials;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes    = &poolSize;
        if (vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &materialPool) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create material descriptor pool\n");
            return false;
        }

        defaultWhiteTexture = Texture::makeSolid(ctx, 255, 255, 255, 255);
        defaultMaterialSet  = createMaterialSet(ctx, defaultWhiteTexture);

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
        pb.shader        = shader;
        pb.userSetLayout = materialSetLayout;
        // mat4 transform + vec4 baseColorFactor, readable from both stages —
        // see default.vert/default.frag, which share this exact layout.
        pb.pushConstant  = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80 };
        pb.depthTest     = true; // swapchain now always carries a depth buffer
        pb.build(ctx, swapchain, defaultLayout, defaultPipeline);
        shader.destroy(ctx.device);

        // ── DustUI pipeline ──
        auto uiShader = ShaderModule::fromBytes(
            ctx.device,
            (uint32_t*)ui_vert_spv, ui_vert_spv_len,
            (uint32_t*)ui_frag_spv, ui_frag_spv_len
        );
        if (!uiShader.valid()) {
            fprintf(stderr, "dust: failed to load UI shaders\n");
            return false;
        }

        PipelineBuilder uiPb;
        uiPb.shader        = uiShader;
        uiPb.userSetLayout = materialSetLayout; // sprite texture, or the 1x1 white fallback
        uiPb.cullMode      = VK_CULL_MODE_NONE; // screen-space quads, no notion of facing
        uiPb.depthTest     = false;             // UI always draws last, over everything
        uiPb.blendEnable   = true;              // border/opacity both rely on alpha blending
        // screenSize only — everything else is per-instance now (see ui.vert)
        uiPb.pushConstant   = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 16 };
        uiPb.instanceStride  = sizeof(UI::RectInstance);
        uiPb.instanceAttribs = uiInstanceAttribs();
        if (!uiPb.build(ctx, swapchain, uiLayout, uiPipeline)) {
            fprintf(stderr, "dust: failed to build UI pipeline\n");
            return false;
        }
        uiShader.destroy(ctx.device);

        VkBufferCreateInfo uiBufInfo{};
        uiBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        uiBufInfo.size  = sizeof(UI::RectInstance) * kUIInstanceCapacity;
        uiBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo uiAllocInfo{};
        uiAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        uiAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo uiAllocResult{};
        if (vmaCreateBuffer(ctx.allocator, &uiBufInfo, &uiAllocInfo, &uiInstanceBuffer, &uiInstanceAlloc, &uiAllocResult) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create UI instance buffer\n");
            return false;
        }
        uiInstanceMapped = uiAllocResult.pMappedData;

        uiQuad = Mesh::makeQuad();
        if (!uiQuad.upload(ctx)) {
            fprintf(stderr, "dust: failed to upload UI quad\n");
            return false;
        }

        // ── Text (MSDF) pipeline ──
        auto textShader = ShaderModule::fromBytes(
            ctx.device,
            (uint32_t*)text_vert_spv, text_vert_spv_len,
            (uint32_t*)text_frag_spv, text_frag_spv_len
        );
        if (!textShader.valid()) {
            fprintf(stderr, "dust: failed to load text shaders\n");
            return false;
        }

        PipelineBuilder textPb;
        textPb.shader        = textShader;
        textPb.userSetLayout = materialSetLayout; // atlas bound the same way a Model's baseColor texture is
        textPb.cullMode      = VK_CULL_MODE_NONE;
        textPb.depthTest     = false;
        textPb.blendEnable   = true;
        // screenSize + outlineColor + outlineParams — see text.vert/text.frag
        textPb.pushConstant   = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 32 }; // screenSize + atlasTexel (outline is per-instance)
        textPb.instanceStride = sizeof(UI::GlyphInstance);
        textPb.instanceAttribs = {
            { 4, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, x) },       // rectPx: x,y,w,h
            { 5, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, uvMinX) },  // uv rect
            { 6, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, color) },   // Color is 4 contiguous floats
            { 7, VK_FORMAT_R32_SFLOAT,          offsetof(UI::GlyphInstance, screenPxRange) },
            { 8, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, clipMinX) },   // Phase 6 clip rect
            { 9, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, outlineColor) },
            { 10, VK_FORMAT_R32_SFLOAT,         offsetof(UI::GlyphInstance, outlineWidth) },
            { 11, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, color2) },   // text gradient end
            { 12, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::GlyphInstance, gradMinX) }, // glyph rect within the text box
            { 13, VK_FORMAT_R32_SFLOAT,          offsetof(UI::GlyphInstance, gradAngle) },
        };
        if (!textPb.build(ctx, swapchain, textLayout, textPipeline)) {
            fprintf(stderr, "dust: failed to build text pipeline\n");
            return false;
        }
        textShader.destroy(ctx.device);

        VkBufferCreateInfo textBufInfo{};
        textBufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        textBufInfo.size  = sizeof(UI::GlyphInstance) * kTextInstanceCapacity;
        textBufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo textAllocInfo{};
        textAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        textAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; // persistent map, see textInstanceMapped

        VmaAllocationInfo textAllocResult{};
        if (vmaCreateBuffer(ctx.allocator, &textBufInfo, &textAllocInfo, &textInstanceBuffer, &textInstanceAlloc, &textAllocResult) != VK_SUCCESS) {
            fprintf(stderr, "dust: failed to create text instance buffer\n");
            return false;
        }
        textInstanceMapped = textAllocResult.pMappedData;

        // ── Particle pipeline ──
        auto particleShader = ShaderModule::fromBytes(
            ctx.device,
            (uint32_t*)particle_vert_spv, particle_vert_spv_len,
            (uint32_t*)particle_frag_spv, particle_frag_spv_len
        );
        if (!particleShader.valid()) {
            fprintf(stderr, "dust: failed to load particle shaders\n");
            return false;
        }

        PipelineBuilder particlePb;
        particlePb.shader        = particleShader;
        particlePb.userSetLayout = materialSetLayout; // texture at set=0, binding=0
        particlePb.cullMode      = VK_CULL_MODE_NONE;
        particlePb.depthTest     = true;
        particlePb.blendEnable   = true;
        // viewProj(mat4=64) + camRight(vec4=16) + camUp(vec4=16) = 96 bytes
        particlePb.pushConstant  = { VK_SHADER_STAGE_VERTEX_BIT, 96 };
        particlePb.instanceStride  = ParticleSystem::instanceStride();
        particlePb.instanceAttribs = ParticleSystem::getInstanceAttribs();
        if (!particlePb.build(ctx, swapchain, particleLayout, particlePipeline)) {
            fprintf(stderr, "dust: failed to build particle pipeline\n");
            return false;
        }
        particleShader.destroy(ctx.device);

        // ── Billboard pipeline ──
        auto bbShader = ShaderModule::fromBytes(
            ctx.device,
            (uint32_t*)billboard_vert_spv, billboard_vert_spv_len,
            (uint32_t*)billboard_frag_spv, billboard_frag_spv_len
        );
        if (!bbShader.valid()) {
            fprintf(stderr, "dust: failed to load billboard shaders\n");
            return false;
        }

        PipelineBuilder bbPb;
        bbPb.shader        = bbShader;
        bbPb.userSetLayout = materialSetLayout;
        bbPb.cullMode      = VK_CULL_MODE_NONE;
        bbPb.depthTest     = true;
        bbPb.blendEnable   = true;
        // viewProj(64) + camRight(16) + camUp(16) + position(16) + color(16) = 128 bytes
        bbPb.pushConstant  = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 128 };
        if (!bbPb.build(ctx, swapchain, billboardLayout, billboardPipeline)) {
            fprintf(stderr, "dust: failed to build billboard pipeline\n");
            return false;
        }
        bbShader.destroy(ctx.device);

        return createFrameData(ctx);
    }

VkDescriptorSet Renderer::createMaterialSet(VulkanContext& ctx, const Texture& tex) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = materialPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &materialSetLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(ctx.device, &allocInfo, &set) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to allocate material descriptor set (pool exhausted?)\n");
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView   = tex.view;
    imageInfo.sampler     = tex.sampler;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(ctx.device, 1, &write, 0, nullptr);

    return set;
}

void Renderer::shutdown(VulkanContext& ctx) {
    vkDeviceWaitIdle(ctx.device);
    if (defaultPipeline) { vkDestroyPipeline(ctx.device, defaultPipeline, nullptr); defaultPipeline = VK_NULL_HANDLE; }
    if (defaultLayout)   { vkDestroyPipelineLayout(ctx.device, defaultLayout, nullptr); defaultLayout = VK_NULL_HANDLE; }
    if (uiPipeline)       { vkDestroyPipeline(ctx.device, uiPipeline, nullptr); uiPipeline = VK_NULL_HANDLE; }
    if (uiLayout)         { vkDestroyPipelineLayout(ctx.device, uiLayout, nullptr); uiLayout = VK_NULL_HANDLE; }
    uiQuad.destroy();
    if (particlePipeline)  { vkDestroyPipeline(ctx.device, particlePipeline, nullptr); particlePipeline = VK_NULL_HANDLE; }
    if (particleLayout)    { vkDestroyPipelineLayout(ctx.device, particleLayout, nullptr); particleLayout = VK_NULL_HANDLE; }
    if (billboardPipeline) { vkDestroyPipeline(ctx.device, billboardPipeline, nullptr); billboardPipeline = VK_NULL_HANDLE; }
    if (billboardLayout)   { vkDestroyPipelineLayout(ctx.device, billboardLayout, nullptr); billboardLayout = VK_NULL_HANDLE; }
    if (textPipeline)     { vkDestroyPipeline(ctx.device, textPipeline, nullptr); textPipeline = VK_NULL_HANDLE; }
    if (textLayout)       { vkDestroyPipelineLayout(ctx.device, textLayout, nullptr); textLayout = VK_NULL_HANDLE; }
    if (textInstanceBuffer) { vmaDestroyBuffer(ctx.allocator, textInstanceBuffer, textInstanceAlloc); textInstanceBuffer = VK_NULL_HANDLE; textInstanceMapped = nullptr; }
    for (auto p : uiShaderPipelines)       if (p) vkDestroyPipeline(ctx.device, p, nullptr);
    for (auto l : uiShaderPipelineLayouts) if (l) vkDestroyPipelineLayout(ctx.device, l, nullptr);
    uiShaderPipelines.clear();
    uiShaderPipelineLayouts.clear();
    if (uiInstanceBuffer)   { vmaDestroyBuffer(ctx.allocator, uiInstanceBuffer, uiInstanceAlloc); uiInstanceBuffer = VK_NULL_HANDLE; uiInstanceMapped = nullptr; }
    defaultWhiteTexture.destroy(ctx);
    if (materialPool)       { vkDestroyDescriptorPool(ctx.device, materialPool, nullptr); materialPool = VK_NULL_HANDLE; }
    if (materialSetLayout)  { vkDestroyDescriptorSetLayout(ctx.device, materialSetLayout, nullptr); materialSetLayout = VK_NULL_HANDLE; }
    destroyFrameData(ctx);
}


void Renderer::draw(VkCommandBuffer cmd, Mesh& mesh,
                    VkPipeline pipeline, VkPipelineLayout layout,
                    const float transform[16],
                    VkDescriptorSet materialSet,
                    const float baseColorFactor[4]) {
    if (mesh.dirty) return; // must upload first

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // Only pipelines built with a set=0 layout (defaultPipeline, or a custom
    // one built with pb.userSetLayout = renderer.materialSetLayout) expect a
    // descriptor set here — leave layouts with no set 0 untouched.
    if (materialSet != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &materialSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // identity/white if none provided
    float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    float white[4] = { 1,1,1,1 };

    // materialSet != VK_NULL_HANDLE means this pipeline was built with
    // renderer.materialSetLayout (defaultPipeline, or a matching custom one)
    // — those declare the full 80-byte `Push { mat4; vec4; }` range covering
    // both stages (see Renderer::init). Anything else (a custom pipeline
    // built the old way, e.g. the PipelineBuilder doc example) only ever
    // declared the original 64-byte vertex-only range, so pushing 80 bytes
    // or a fragment stage flag there would violate its pipeline layout —
    // fall back to the original push for those.
    if (materialSet != VK_NULL_HANDLE) {
        // Matches the `Push { mat4 transform; vec4 baseColorFactor; }` block
        // shared by default.vert/default.frag — mat4 is naturally 64 bytes
        // so the vec4 lands at offset 64 with no padding either side.
        struct { float transform[16]; float baseColorFactor[4]; } pc;
        memcpy(pc.transform, transform ? transform : identity, sizeof(pc.transform));
        memcpy(pc.baseColorFactor, baseColorFactor ? baseColorFactor : white, sizeof(pc.baseColorFactor));
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    } else {
        vkCmdPushConstants(cmd, layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, 64,
            transform ? transform : identity);
    }

    // set dynamic viewport + scissor
    VkViewport vp{ 0,0,(float)currentExtent.width,(float)currentExtent.height,0,1 };
    VkRect2D   sc{ {0,0}, currentExtent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

std::vector<InstanceAttrib> Renderer::uiInstanceAttribs() {
    return {
        {  4, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, x) },           // rect: x,y,w,h
        {  5, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, fill) },        // Color is 4 contiguous floats
        {  6, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, border) },
        {  7, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, borderWidth) }, // params
        {  8, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, clipMinX) },
        {  9, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, uvMinX) },
        { 10, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, p0) },          // custom shader params
        { 11, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, p4) },
        { 12, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, fill2) },      // gradient end
        { 13, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, radTL) },      // per-corner radii
        { 14, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, gradAngle) },  // gradient angle + shadow blur
        { 15, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UI::RectInstance, border2) },    // border gradient end
    };
}

bool Renderer::buildUIShaderPipeline(VulkanContext& ctx, Swapchain& swapchain,
                                     const uint32_t* fragSpv, size_t fragSpvLen,
                                     VkPipeline& outPipeline) {
    // Vertex stage is always the stock ui.vert — a custom UI shader is a
    // fragment shader against a fixed interface (see Shaders/ui_pulse.frag),
    // which is what lets these widgets keep using the same instance buffer
    // and the same layout as every other widget.
    auto shader = ShaderModule::fromBytes(ctx.device,
        (uint32_t*)ui_vert_spv, ui_vert_spv_len, fragSpv, fragSpvLen);
    if (!shader.valid()) return false;

    PipelineBuilder pb;
    pb.shader          = shader;
    pb.userSetLayout   = materialSetLayout;
    pb.cullMode        = VK_CULL_MODE_NONE;
    pb.depthTest       = false;
    pb.blendEnable     = true;
    pb.pushConstant    = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 16 };
    pb.instanceStride  = sizeof(UI::RectInstance);
    pb.instanceAttribs = uiInstanceAttribs();

    // The pipeline layout is identical to uiLayout's, so the caller gets
    // back only the pipeline and keeps binding through uiLayout — one less
    // handle to track, and push constants/descriptors stay compatible.
    VkPipelineLayout throwaway = VK_NULL_HANDLE;
    bool ok = pb.build(ctx, swapchain, throwaway, outPipeline);
    shader.destroy(ctx.device);
    if (ok) uiShaderPipelineLayouts.push_back(throwaway);
    else if (throwaway) vkDestroyPipelineLayout(ctx.device, throwaway, nullptr);
    return ok;
}

void Renderer::drawUIRects(VkCommandBuffer cmd,
                           const UI::RectInstance* instances, uint32_t count,
                           VkDescriptorSet texSet, VkExtent2D screenSize,
                           bool upload, VkPipeline pipelineOverride) {
    if (uiQuad.dirty || !uiInstanceMapped || count == 0) return;
    if (uiInstanceCursor + count > kUIInstanceCapacity) return; // silently drop the overflow rather than corrupt the batch

    uint32_t first = uiInstanceCursor;
    if (upload)
        memcpy((UI::RectInstance*)uiInstanceMapped + first, instances, count * sizeof(UI::RectInstance));
    uiInstanceCursor += count;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipelineOverride != VK_NULL_HANDLE ? pipelineOverride : uiPipeline);
    VkDescriptorSet set = (texSet != VK_NULL_HANDLE) ? texSet : defaultMaterialSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLayout, 0, 1, &set, 0, nullptr);

    // Binding 0: shared unit quad (per-vertex), binding 1: widget instances.
    // The instance binding is offset to this batch's slice rather than passing
    // firstInstance, which keeps the shader's gl_InstanceIndex at 0.
    VkDeviceSize quadOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &uiQuad.vertexBuffer, &quadOffset);
    VkDeviceSize instOffset = (VkDeviceSize)first * sizeof(UI::RectInstance);
    vkCmdBindVertexBuffers(cmd, 1, 1, &uiInstanceBuffer, &instOffset);
    vkCmdBindIndexBuffer(cmd, uiQuad.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Matches `Push { vec4 screenSize; }` in ui.vert/ui.frag.
    float pc[4] = { (float)screenSize.width, (float)screenSize.height, 0.0f, 0.0f };
    vkCmdPushConstants(cmd, uiLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);

    VkViewport vp{ 0, 0, (float)screenSize.width, (float)screenSize.height, 0, 1 };
    VkRect2D   sc{ {0, 0}, screenSize };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdDrawIndexed(cmd, uiQuad.indexCount, count, 0, 0, 0);
}

void Renderer::drawTextInstances(VkCommandBuffer cmd, const UI::Font& font,
                                 const UI::GlyphInstance* instances, uint32_t count,
                                 VkExtent2D screenSize) {
    if (count == 0 || uiQuad.dirty || !textInstanceMapped || font.atlasSet == VK_NULL_HANDLE) return;
    if (textInstanceCursor + count > kTextInstanceCapacity) return; // drop the overflow rather than corrupt the batch

    uint32_t first = textInstanceCursor;
    memcpy((UI::GlyphInstance*)textInstanceMapped + first, instances, count * sizeof(UI::GlyphInstance));
    textInstanceCursor += count;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, textLayout, 0, 1, &font.atlasSet, 0, nullptr);

    VkDeviceSize quadOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &uiQuad.vertexBuffer, &quadOffset);
    VkDeviceSize instOffset = (VkDeviceSize)first * sizeof(UI::GlyphInstance);
    vkCmdBindVertexBuffers(cmd, 1, 1, &textInstanceBuffer, &instOffset);
    vkCmdBindIndexBuffer(cmd, uiQuad.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Matches `Push { vec4 screenSize; vec4 atlasTexel; }` in text.vert/frag.
    float pc[8] = {
        (float)screenSize.width, (float)screenSize.height, 0.0f, 0.0f,
        font.atlas.width  ? 1.0f / (float)font.atlas.width  : 0.0f,
        font.atlas.height ? 1.0f / (float)font.atlas.height : 0.0f,
        0.0f, 0.0f,
    };

    vkCmdPushConstants(cmd, textLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);

    VkViewport vp{ 0, 0, (float)screenSize.width, (float)screenSize.height, 0, 1 };
    VkRect2D   sc{ {0, 0}, screenSize };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdDrawIndexed(cmd, uiQuad.indexCount, count, 0, 0, 0);
}

void Renderer::drawParticles(VkCommandBuffer cmd,
                              ParticleSystem& ps,
                              const glm::mat4& viewProj,
                              const glm::vec3& camRight,
                              const glm::vec3& camUp,
                              VkDescriptorSet materialSet) {
    if (ps.maxParticles() == 0 || uiQuad.dirty) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particlePipeline);

    VkDescriptorSet tex = (materialSet != VK_NULL_HANDLE) ? materialSet : defaultMaterialSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particleLayout, 0, 1, &tex, 0, nullptr);

    // Binding 0: unit quad (per-vertex), Binding 1: particle buffer (per-instance)
    VkBuffer     bufs[2]    = { uiQuad.vertexBuffer, ps.particleBuffer() };
    VkDeviceSize offsets[2] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, bufs, offsets);
    vkCmdBindIndexBuffer(cmd, uiQuad.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    struct { glm::mat4 vp; glm::vec4 right; glm::vec4 up; } pc;
    pc.vp    = viewProj;
    pc.right = glm::vec4(camRight, 0.0f);
    pc.up    = glm::vec4(camUp,    0.0f);
    vkCmdPushConstants(cmd, particleLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    VkViewport vp{ 0, 0, (float)currentExtent.width, (float)currentExtent.height, 0, 1 };
    VkRect2D   sc{ {0, 0}, currentExtent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // One instanced draw — dead particles are zeroed out in the shader (life <= 0 → size 0)
    vkCmdDrawIndexed(cmd, uiQuad.indexCount, ps.maxParticles(), 0, 0, 0);
}

void Renderer::drawBillboard(VkCommandBuffer cmd,
                              const glm::mat4& viewProj,
                              const glm::vec3& camRight,
                              const glm::vec3& camUp,
                              glm::vec3 position, float size,
                              glm::vec4 color,
                              VkDescriptorSet materialSet) {
    if (uiQuad.dirty) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline);

    VkDescriptorSet tex = (materialSet != VK_NULL_HANDLE) ? materialSet : defaultMaterialSet;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardLayout, 0, 1, &tex, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &uiQuad.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, uiQuad.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    struct { glm::mat4 vp; glm::vec4 right; glm::vec4 up; glm::vec4 pos; glm::vec4 col; } pc;
    pc.vp    = viewProj;
    pc.right = glm::vec4(camRight, 0.0f);
    pc.up    = glm::vec4(camUp,    0.0f);
    pc.pos   = glm::vec4(position, size);
    pc.col   = color;
    vkCmdPushConstants(cmd, billboardLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    VkViewport vp{ 0, 0, (float)currentExtent.width, (float)currentExtent.height, 0, 1 };
    VkRect2D   sc{ {0, 0}, currentExtent };
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdDrawIndexed(cmd, uiQuad.indexCount, 1, 0, 0, 0);
}

bool Renderer::beginFrame(VulkanContext& ctx, Window& window) {
    FrameData& frame = frames[currentFrame];

    // Wait for this frame slot to be free
    vkWaitForFences(ctx.device, 1, &frame.renderFence, VK_TRUE, UINT64_MAX);

    uiInstanceCursor   = 0; // batches are written from the top of the shared buffers each frame
    textInstanceCursor = 0;

    // Resize check. Not every platform reports OUT_OF_DATE/SUBOPTIMAL on
    // resize (Wayland in particular happily keeps presenting a stale-sized
    // swapchain), so compare against the framebuffer size directly. Without
    // this the swapchain stays at its creation size while layout/viewport
    // use the real window size — everything draws magnified and the bottom
    // and right of the UI falls outside the render area entirely.
    if (window.width > 0 && window.height > 0 &&
        (window.swapchain.extent.width  != window.width ||
         window.swapchain.extent.height != window.height) &&
        (lastResizeRequest.width != window.width || lastResizeRequest.height != window.height)) {
        lastResizeRequest = { window.width, window.height };
        window.swapchain.rebuild(ctx, window);
        return false;
    }

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
currentExtent = window.swapchain.extent;
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
    Swapchain&      sc  = window.swapchain;

    // Transition color + depth images for this frame. Both use oldLayout =
    // UNDEFINED every frame (not their true previous layout) because loadOp
    // = CLEAR discards whatever was there anyway — a standard shortcut, not
    // just laziness: it avoids tracking per-image layout state between
    // frames for attachments that never need their prior contents.
    bool hasStencil = sc.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                       sc.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT;

    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].image            = sc.images[imageIndex];
    barriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barriers[0].srcAccessMask    = 0;
    barriers[0].dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    barriers[1].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].image            = sc.depthImage;
    barriers[1].subresourceRange = {
        (VkImageAspectFlags)(VK_IMAGE_ASPECT_DEPTH_BIT | (hasStencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)),
        0, 1, 0, 1
    };
    barriers[1].srcAccessMask    = 0;
    barriers[1].dstAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 2, barriers);

    VkRenderingAttachmentInfoKHR colorAttachment{};
    colorAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    colorAttachment.imageView   = sc.imageViews[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue  = window.clearColor;

    VkRenderingAttachmentInfoKHR depthAttachment{};
    depthAttachment.sType                    = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    depthAttachment.imageView                = sc.depthImageView;
    depthAttachment.imageLayout              = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp                   = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp                  = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient, never read back
    depthAttachment.clearValue.depthStencil  = { 1.0f, 0 };

    VkRenderingInfoKHR renderingInfo{};
    renderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea           = { {0, 0}, sc.extent };
    renderingInfo.layerCount           = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments    = &colorAttachment;
    renderingInfo.pDepthAttachment     = &depthAttachment;

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
