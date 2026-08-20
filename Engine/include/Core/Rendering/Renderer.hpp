#pragma once

#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Swapchain.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Texture.hpp"
#include "Core/Rendering/FrameData.hpp"
#include "Core/Rendering/DefaultShaders.hpp"
#include "Core/UI/UIShaders.hpp"
#include "Core/UI/Font.hpp"
#include "Core/UI/RectInstance.hpp"
#include "Core/Rendering/PipelineBuilder.hpp"
#include <array>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Dust {

struct Window;
class ParticleSystem;

// ── Lit pipeline light data (see Shaders/lit.frag) ──
// Fixed-size arrays, not dynamic — bounded loops with an early break on the
// live count are cheap on low-end GPUs; unbounded/dynamically-sized arrays
// would force worse codegen. Bump these if a game genuinely needs more
// simultaneous lights; every lit VkPipeline variant and this UBO's layout
// depend on the exact values, so changing them means rebuilding shaders.
static constexpr uint32_t kMaxPointLights = 8;
static constexpr uint32_t kMaxSpotLights  = 8;

// std140-compatible layouts — every field is a vec4 (or a mat4) so there's
// no manual padding to get wrong. Mirrors the `PointLight`/`SpotLight`
// structs in lit.frag field-for-field.
struct GPUPointLight {
    glm::vec4 posRadius;      // xyz = world position, w = falloff radius
    glm::vec4 colorIntensity; // rgb = color, a = intensity
};
struct GPUSpotLight {
    glm::vec4 posRange;       // xyz = world position, w = falloff range
    glm::vec4 dirInnerCos;    // xyz = normalized direction, w = cos(innerAngle)
    glm::vec4 colorIntensity; // rgb = color, a = intensity
    glm::vec4 outerCos;       // x = cos(outerAngle), yzw padding
};

// The lit pipeline's set=1 UBO contents (Renderer::lightsBuffer) — bound
// once per frame (see Renderer::updateLights), not per draw, since the
// camera and every light are the same for every lit object in a frame.
// viewProj/cameraPos live here (not in the per-draw push constant) so the
// lit push constant block only has to carry the model matrix — see
// Renderer::drawLit and lit.vert/lit.frag.
struct LightsUBOData {
    glm::mat4 viewProj;
    glm::vec4 cameraPos;      // xyz, w unused
    glm::vec4 dirLightDir;    // xyz = direction the sun travels, w unused
    glm::vec4 dirLightColor;  // rgb = color, a = intensity (0 = off)
    glm::vec4 ambient;        // rgb = color, a = intensity
    glm::vec4 lightCounts;    // x = active point count, y = active spot count
    // Directional light's orthographic viewProj — only meaningful (and only
    // sampled, see lit.frag's HAS_SHADOWS branch) when shadows are enabled
    // for the frame that wrote this; see DustEngine::setShadowsEnabled.
    glm::mat4 lightSpaceViewProj{ 1.0f };
    GPUPointLight pointLights[kMaxPointLights];
    GPUSpotLight  spotLights[kMaxSpotLights];
};

// Lit ubershader feature-flag bitmask (Shaders/lit.frag specialization
// constants, ids 0-3) — one VkPipeline is lazily built and cached per
// distinct mask value a Material actually needs (Renderer::getLitPipeline).
//   bit 0 — HAS_NORMAL_MAP
//   bit 1 — HAS_METALLIC_ROUGHNESS_MAP
//   bit 2 — HAS_EMISSIVE_MAP
//   bit 3 — HAS_OCCLUSION_MAP
//   bit 4 — HAS_SHADOWS (samples Renderer::shadowImageView via lightsSet
//           binding 1 — see DustEngine::setShadowsEnabled)
enum LitFeatureFlags : uint32_t {
    LitFeature_NormalMap            = 1u << 0,
    LitFeature_MetallicRoughnessMap = 1u << 1,
    LitFeature_EmissiveMap          = 1u << 2,
    LitFeature_OcclusionMap         = 1u << 3,
    LitFeature_Shadows              = 1u << 4,
};

// Push constants layout matching particle.vert/particle.frag.
// 64 + 16 + 16 + 16 + 16 = 128 bytes — exactly Vulkan's guaranteed minimum
// push-constant size, so this can't grow further without repacking (e.g.
// dropping camRight/camUp's unused w, or packing fog into camRight.w/camUp.w).
struct ParticlePushConstants {
    glm::mat4 viewProj;
    glm::vec4 camRight;  // xyz used
    glm::vec4 camUp;     // xyz used
    glm::vec4 fogColor;  // rgb = fog color, a > 0.5 = enabled — matches default.frag's Push block
    glm::vec4 fogParams; // x = start distance, y = end distance, zw unused
};

struct Renderer {
    std::array<FrameData, FRAMES_IN_FLIGHT> frames;
    uint32_t                                currentFrame = 0;
    uint32_t                                imageIndex   = 0;
    VkPipelineLayout defaultLayout   = VK_NULL_HANDLE;
    VkPipeline       defaultPipeline = VK_NULL_HANDLE;

    // ── Materials (set=0, binding=0: combined image sampler) ──
    // One shared layout so any pipeline built with
    // `pb.userSetLayout = renderer.materialSetLayout` can bind sets created
    // by createMaterialSet() below — defaultPipeline and every Model
    // material both draw from this same pool.
    VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      materialPool      = VK_NULL_HANDLE;
    Texture               defaultWhiteTexture;   // "no texture bound" fallback
    VkDescriptorSet       defaultMaterialSet = VK_NULL_HANDLE;

    // Allocates+writes a one-texture material descriptor set from materialPool.
    // Caller owns the returned handle's lifetime via vkFreeDescriptorSets
    // (pool is created with FREE_DESCRIPTOR_SET_BIT) — Model::destroy does
    // this when a model is unloaded.
    VkDescriptorSet createMaterialSet(VulkanContext& ctx, const Texture& tex);

    // ── Lit pipeline (Blinn-Phong ubershader, see Shaders/lit.vert/lit.frag) ──
    //
    // Materials (set=0): a *separate* 5-sampler layout from materialSetLayout
    // above — every other pipeline (default/UI/text/particle/billboard)
    // shares that 1-sampler layout, and giving it four more bindings just for
    // the lit pipeline's sake would force every one of their descriptor sets
    // to carry unused image bindings too. defaultNormalTexture is the
    // "no normal map" fallback: flat (0.5,0.5,1.0) tangent-space up, linear
    // (not sRGB — it's data, like defaultWhiteTexture is for color).
    VkDescriptorSetLayout litMaterialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      litMaterialPool      = VK_NULL_HANDLE;
    Texture               defaultNormalTexture;
    VkDescriptorSet       defaultLitMaterialSet = VK_NULL_HANDLE;

    // Allocates+writes a 5-sampler lit material set from materialPool.
    // Any texture pointer may be null — falls back to defaultWhiteTexture
    // (or defaultNormalTexture for `normal`), matching createMaterialSet()'s
    // "no texture bound" convention.
    VkDescriptorSet createLitMaterialSet(VulkanContext& ctx,
                                         const Texture* baseColor,
                                         const Texture* normal,
                                         const Texture* metallicRoughness,
                                         const Texture* emissive,
                                         const Texture* occlusion);

    // Lights (set=1): one UBO, written once per frame by updateLights() —
    // see LightsUBOData. Not double-buffered per frame-in-flight (like
    // uiInstanceBuffer/textInstanceBuffer, this trades a theoretical
    // same-frame tear for one fewer buffer to manage); fine since lighting
    // that's one frame stale for a couple of in-flight frames is invisible.
    VkDescriptorSetLayout lightsSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      lightsPool      = VK_NULL_HANDLE;
    VkDescriptorSet       lightsSet       = VK_NULL_HANDLE;
    VkBuffer      lightsBuffer = VK_NULL_HANDLE;
    VmaAllocation lightsAlloc  = VK_NULL_HANDLE;
    void*         lightsMapped = nullptr; // persistently mapped, see Renderer::init

    void updateLights(const LightsUBOData& data);

    // One shared pipeline layout for every lit variant (set=0 material +
    // set=1 lights + the 128-byte push constant block — see Shaders/lit.vert).
    // Only the fragment shader module changes between variants (specialization
    // constants), so the layout itself never needs to.
    VkPipelineLayout litLayout = VK_NULL_HANDLE;
    // Cache of built variants, keyed by LitFeatureFlags bitmask — built
    // lazily on first use of a given mask, reused after (see comment on
    // LitFeatureFlags above).
    std::unordered_map<uint32_t, VkPipeline> litPipelines;

    // Builds (if not already cached) and returns the lit VkPipeline for
    // `featureMask`. VK_NULL_HANDLE on build failure.
    VkPipeline getLitPipeline(VulkanContext& ctx, Swapchain& swapchain, uint32_t featureMask);

    // Draws one mesh with the lit ubershader. `model` is the model matrix
    // alone (not a baked MVP — see lit.vert), and lightsSet must already be
    // up to date for this frame (Renderer::updateLights, called once from
    // DustEngine::beginMode3D). materialParams = (metallic, roughness,
    // emissiveStrength) — see lit.frag's Push block.
    void drawLit(VkCommandBuffer cmd, Mesh& mesh, uint32_t featureMask,
                VulkanContext& ctx, Swapchain& swapchain,
                const float model[16], VkDescriptorSet litMaterialSet,
                const float baseColorFactor[4], const float fogColor[4],
                const float fogParams[4], const float materialParams[4]);

    // ── Directional shadow map ── one fixed-size depth-only render target,
    // resolution capped at 2048x2048 (low-end target — see task scope, no
    // cascades). Sampled from lit.frag via lightsSet binding=1, so it's
    // always alive (even fully black/unused when shadows are off) rather
    // than a resource that comes and goes with DustEngine::setShadowsEnabled.
    static constexpr uint32_t kShadowMapSize = 2048;
    VkFormat      shadowDepthFormat = VK_FORMAT_D32_SFLOAT; // near-universal support, no fallback needed
    VkImage       shadowImage       = VK_NULL_HANDLE;
    VmaAllocation shadowAlloc       = VK_NULL_HANDLE;
    VkImageView   shadowImageView   = VK_NULL_HANDLE;
    VkSampler     shadowSampler     = VK_NULL_HANDLE;

    // Depth-only pipeline for the shadow pass — own layout (no descriptor
    // sets at all, just a 128-byte push constant: lightViewProj + model —
    // see Shaders/shadow.vert), separate from litLayout entirely.
    VkPipelineLayout shadowLayout   = VK_NULL_HANDLE;
    VkPipeline       shadowPipeline = VK_NULL_HANDLE;

    // Begin/end the shadow depth pass — transitions shadowImage to/from
    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL / SHADER_READ_ONLY_OPTIMAL around a
    // dynamic-rendering pass sized to kShadowMapSize. Call once per frame,
    // before the color pass, with every buffered draw replayed via
    // drawShadow() in between — see DustEngine::endMode3D.
    void beginShadowPass(VkCommandBuffer cmd);
    void endShadowPass(VkCommandBuffer cmd);

    // One depth-only draw into the shadow map. `model` is the same world
    // matrix drawLit() would get; lightViewProj is the directional light's
    // orthographic viewProj for this frame (DustEngine computes it from
    // setShadowBounds()).
    void drawShadow(VkCommandBuffer cmd, Mesh& mesh,
                    const float model[16], const float lightViewProj[16]);

    // ── DustUI ── screen-space rounded-rect/border/sprite quads (see
    // Core/UI/). One shared unit quad, instanced once per widget, so the
    // whole tree costs one draw call per texture switch rather than one per
    // widget (UITimeline.md Phase 4).
    VkPipelineLayout uiLayout   = VK_NULL_HANDLE;
    VkPipeline       uiPipeline = VK_NULL_HANDLE;
    Mesh             uiQuad;

    static constexpr uint32_t kUIInstanceCapacity = 4096;
    VkBuffer      uiInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation uiInstanceAlloc  = VK_NULL_HANDLE;
    void*         uiInstanceMapped = nullptr; // persistently mapped, see Renderer::init
    // Write cursor into the buffer above, reset each beginFrame so several
    // batches within one frame don't overwrite each other.
    uint32_t      uiInstanceCursor = 0;

    // Draws `count` widget quads in a single instanced call. `texSet` is
    // bound for the whole batch — the caller groups instances by texture and
    // calls this once per run (defaultMaterialSet's 1x1 white for untextured
    // widgets, so ui.frag can multiply unconditionally). `upload=false`
    // reuses whatever is already at the same buffer offset, which is how
    // Phase 4's diff skips re-uploading an unchanged UI.
    void drawUIRects(VkCommandBuffer cmd,
                     const UI::RectInstance* instances, uint32_t count,
                     VkDescriptorSet texSet, VkExtent2D screenSize,
                     bool upload = true, VkPipeline pipelineOverride = VK_NULL_HANDLE);

    // Shared by the built-in UI pipeline and every custom shader widget
    // pipeline — one instance layout, so a custom shader can be swapped in
    // without touching the buffer or the batcher. Mirrors UI::RectInstance.
    static std::vector<InstanceAttrib> uiInstanceAttribs();

    // Builds a pipeline pairing the stock ui.vert with a caller-supplied
    // fragment shader (UITimeline.md Phase 9). The resulting pipeline is
    // bound through uiLayout, so it shares push constants and the material
    // set; Renderer owns it and destroys it at shutdown.
    bool buildUIShaderPipeline(VulkanContext& ctx, Swapchain& swapchain,
                               const uint32_t* fragSpv, size_t fragSpvLen,
                               VkPipeline& outPipeline);
    std::vector<VkPipeline>       uiShaderPipelines;
    std::vector<VkPipelineLayout> uiShaderPipelineLayouts;

    // ── Text (MSDF) ── same shared unit quad as DustUI rects, instanced
    // once per glyph — see Shaders/text.vert/frag and Core/UI/Font.hpp.
    VkPipelineLayout textLayout   = VK_NULL_HANDLE;
    VkPipeline       textPipeline = VK_NULL_HANDLE;

    // Single CPU-mapped buffer, rewritten from offset 0 on every call — fine
    // as long as callers don't need two in-flight text draws to coexist
    // within the same frame without one overwriting the other's data before
    // the GPU reads it (same caveat Mesh::updateVertices() documents for the
    // same reason: no per-frame-in-flight duplication yet). One draw call
    // per Font per frame, which is all DustUI needs today.
    static constexpr uint32_t kTextInstanceCapacity = 8192;
    // Write cursor, reset each beginFrame — text is now drawn in several
    // depth-ordered batches per frame rather than one, so they can't all
    // start at offset 0.
    uint32_t      textInstanceCursor = 0;
    VkBuffer      textInstanceBuffer = VK_NULL_HANDLE;
    VmaAllocation textInstanceAlloc  = VK_NULL_HANDLE;
    void*         textInstanceMapped = nullptr; // persistently mapped, see Renderer::init

    // Uploads `instances` into the shared instance buffer and issues one
    // instanced draw call against `font`'s atlas. Outline width/colour ride
    // along per glyph (see UI::GlyphInstance), so a single batch can mix
    // outlined and plain runs.
    void drawTextInstances(VkCommandBuffer cmd, const UI::Font& font,
                           const UI::GlyphInstance* instances, uint32_t count,
                           VkExtent2D screenSize);

    // ── Particles ── compute-simulated billboard quads, one per particle slot.
    // Dead particles (life <= 0) are collapsed to zero size in the shader.
    VkPipelineLayout particleLayout   = VK_NULL_HANDLE;
    VkPipeline       particlePipeline = VK_NULL_HANDLE;

    // fogColor/fogParams default to nullptr = off, same convention as draw().
    void drawParticles(VkCommandBuffer cmd,
                       ParticleSystem& ps,
                       const glm::mat4& viewProj,
                       const glm::vec3& camRight,
                       const glm::vec3& camUp,
                       VkDescriptorSet materialSet = VK_NULL_HANDLE,
                       const float fogColor[4] = nullptr,
                       const float fogParams[4] = nullptr);

    // ── Billboards ── single camera-facing textured quad, no instancing.
    VkPipelineLayout billboardLayout   = VK_NULL_HANDLE;
    VkPipeline       billboardPipeline = VK_NULL_HANDLE;

    void drawBillboard(VkCommandBuffer cmd,
                       const glm::mat4& viewProj,
                       const glm::vec3& camRight,
                       const glm::vec3& camUp,
                       glm::vec3 position, float size,
                       glm::vec4 color = glm::vec4(1.0f),
                       VkDescriptorSet materialSet = VK_NULL_HANDLE);

    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkExtent2D currentExtent = {};
    // Last framebuffer size we asked the swapchain to match. The surface can
    // legitimately clamp us to something else (min/maxImageExtent), so we
    // only retry when the *request* changes — otherwise beginFrame would
    // rebuild forever and never draw a frame.
    VkExtent2D lastResizeRequest = {};
    vkb::DispatchTable dispatch;
    // fogColor/fogParams match default.frag's Push block — fogColor.a > 0.5
    // enables fog (nullptr/default = off, matching that block's zero-init).
    void draw(VkCommandBuffer cmd, Mesh& mesh,
              VkPipeline pipeline, VkPipelineLayout layout,
              const float transform[16] = nullptr,
              VkDescriptorSet materialSet = VK_NULL_HANDLE,
              const float baseColorFactor[4] = nullptr,
              const float fogColor[4] = nullptr,
              const float fogParams[4] = nullptr);

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
