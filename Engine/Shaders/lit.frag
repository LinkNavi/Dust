// lit.frag — Blinn-Phong (not Cook-Torrance/PBR — cheap enough for low-end
// GPUs) ubershader. One fragment shader source, several VkPipeline variants
// built from it via specialization constants (see Renderer::getLitPipeline),
// keyed by a bitmask of which optional material textures are present:
//
//   bit 0 (id 0) — HAS_NORMAL_MAP
//   bit 1 (id 1) — HAS_METALLIC_ROUGHNESS_MAP
//   bit 2 (id 2) — HAS_EMISSIVE_MAP
//   bit 3 (id 3) — HAS_OCCLUSION_MAP
//
// FOG_ENABLED is deliberately NOT a spec constant — it's already a
// zero-cost-when-off runtime branch on push.fogColor.a (matching
// default.frag), so making it a pipeline variant too would just multiply
// the cache for no benefit.
#version 450

layout(constant_id = 0) const bool HAS_NORMAL_MAP             = false;
layout(constant_id = 1) const bool HAS_METALLIC_ROUGHNESS_MAP = false;
layout(constant_id = 2) const bool HAS_EMISSIVE_MAP           = false;
layout(constant_id = 3) const bool HAS_OCCLUSION_MAP          = false;
layout(constant_id = 4) const bool HAS_SHADOWS                = false;

// set=0 — lit-specific material set (5 combined image samplers). Distinct
// from Renderer::materialSetLayout (the 1-sampler layout every other
// pipeline shares) so those pipelines' descriptor sets don't have to carry
// four unused bindings just because the lit pipeline exists — see
// Renderer::createLitMaterialSet.
layout(set=0, binding=0) uniform sampler2D baseColorTex;
layout(set=0, binding=1) uniform sampler2D normalTex;
layout(set=0, binding=2) uniform sampler2D metallicRoughnessTex;
layout(set=0, binding=3) uniform sampler2D emissiveTex;
layout(set=0, binding=4) uniform sampler2D occlusionTex;

// Directional shadow map — bound at set=1 (see LightsUBOData/lightsSet)
// rather than a new set, since lightsSetLayout is already bound once for
// every lit variant regardless of HAS_SHADOWS (see Renderer::drawLit); a
// second descriptor set here would mean a second vkCmdBindDescriptorSets
// call every lit draw just for the (usually-off) shadow case.
layout(set=1, binding=1) uniform sampler2D shadowMap;

#define MAX_POINT_LIGHTS 8
#define MAX_SPOT_LIGHTS  8

struct PointLight {
    vec4 posRadius;       // xyz = world position, w = falloff radius
    vec4 colorIntensity;  // rgb = color, a = intensity
};

struct SpotLight {
    vec4 posRange;        // xyz = world position, w = falloff range
    vec4 dirInnerCos;     // xyz = normalized direction (light -> scene), w = cos(innerAngle)
    vec4 colorIntensity;  // rgb = color, a = intensity
    vec4 outerCos;        // x = cos(outerAngle), yzw unused (std140 padding)
};

// set=1 — bound once per frame (Renderer::updateLights / lightsSet), not
// per draw — the camera and every light are the same for every lit object
// this frame, so re-binding per draw would just be wasted descriptor traffic.
layout(set=1, binding=0) uniform Scene {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 dirLightDir;    // xyz = direction light travels, w unused
    vec4 dirLightColor;  // rgb = color, a = intensity (0 = sun off)
    vec4 ambient;        // rgb = color, a = intensity
    vec4 lightCounts;    // x = active point count, y = active spot count
    mat4 lightSpaceViewProj; // directional light's ortho viewProj — see shadow.vert
    PointLight pointLights[MAX_POINT_LIGHTS];
    SpotLight  spotLights[MAX_SPOT_LIGHTS];
} scene;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 baseColorFactor;
    vec4 fogColor;        // rgb = fog color, a > 0.5 = enabled — matches default.frag exactly
    vec4 fogParams;       // x = start distance, y = end distance, zw unused
    vec4 materialParams;  // x = metallic, y = roughness, z = emissive strength, w unused
} push;

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;
layout(location=2) in float inViewDist;
layout(location=3) in vec3 inWorldPos;
layout(location=4) in vec3 inWorldNormal;

layout(location=0) out vec4 outColor;

// One light's Blinn-Phong contribution. roughness drives specular power
// (glossier = tighter highlight); metallic tints specular toward albedo and
// removes diffuse — the standard cheap metalness approximation, not a real
// Fresnel term.
vec3 shadeLight(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float roughness) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(L + V);
    float shininess = mix(4.0, 128.0, 1.0 - clamp(roughness, 0.0, 1.0));
    float spec = pow(max(dot(N, H), 0.0), shininess);
    vec3 specColor = mix(vec3(1.0), albedo, metallic);
    vec3 diffuse   = albedo * (1.0 - metallic);
    return (diffuse + specColor * spec) * radiance * NdotL;
}

// 3x3 PCF — 9 taps averaged, one texel apart. Outside the light's ortho
// frustum (or beyond its far plane) is treated as fully lit, not shadowed —
// an object that's simply outside the shadow map's coverage shouldn't go
// dark, and a hard shadow-map edge would look far worse than no shadow.
float sampleShadow(vec3 worldPos, vec3 N, vec3 L) {
    vec4 clip = scene.lightSpaceViewProj * vec4(worldPos, 1.0);
    vec3 proj = clip.xyz / clip.w;
    vec2 uv   = proj.xy * 0.5 + 0.5; // NDC xy -> [0,1] (Vulkan NDC z is already [0,1])
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0 || proj.z < 0.0)
        return 1.0;

    // Slope-scaled bias on top of the pipeline's constant depth bias
    // (Renderer::init's shadowPipeline) — cheap acne fix, tuned by hand.
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0006);

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            float d = texture(shadowMap, uv + vec2(x, y) * texel).r;
            shadow += (proj.z - bias > d) ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}

void main() {
    vec4 base = inColor * push.baseColorFactor * texture(baseColorTex, inUV);

    vec3 N = normalize(inWorldNormal);
    if (HAS_NORMAL_MAP) {
        // No tangent basis (Vertex has no tangent attribute yet), so this
        // isn't a real TBN transform — just nudges the geometric normal
        // toward the map's tangent-space XY. Fine for gentle surface detail,
        // visibly wrong for steep bump maps; a proper TBN is future work.
        vec3 mapN = texture(normalTex, inUV).xyz * 2.0 - 1.0;
        N = normalize(N + mapN * 0.5);
    }

    float metallic  = push.materialParams.x;
    float roughness = push.materialParams.y;
    if (HAS_METALLIC_ROUGHNESS_MAP) {
        // glTF convention: G = roughness, B = metallic.
        vec2 mr = texture(metallicRoughnessTex, inUV).gb;
        roughness *= mr.x;
        metallic  *= mr.y;
    }

    float occlusion = HAS_OCCLUSION_MAP ? texture(occlusionTex, inUV).r : 1.0;

    vec3 albedo = base.rgb;
    vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);

    vec3 lit = scene.ambient.rgb * scene.ambient.a * albedo * occlusion;

    // Directional "sun" — intensity 0 (default, unset) costs one branch, no
    // texture/array work, same zero-cost-when-off convention as fog below.
    if (scene.dirLightColor.a > 0.0) {
        vec3 L = normalize(-scene.dirLightDir.xyz);
        float shadow = HAS_SHADOWS ? sampleShadow(inWorldPos, N, L) : 1.0;
        lit += shadeLight(N, V, L, scene.dirLightColor.rgb * scene.dirLightColor.a, albedo, metallic, roughness) * occlusion * shadow;
    }

    // Fixed-size arrays, early-break on the live count — avoids unbounded
    // dynamic loop bounds (cheap on low-end GPUs which hate divergent loops)
    // while still skipping unused slots instead of paying for all 8 always.
    int pointCount = int(scene.lightCounts.x);
    for (int i = 0; i < MAX_POINT_LIGHTS; i++) {
        if (i >= pointCount) break;
        vec3 toLight = scene.pointLights[i].posRadius.xyz - inWorldPos;
        float dist   = length(toLight);
        float radius = scene.pointLights[i].posRadius.w;
        if (dist >= radius) continue;
        vec3 L = toLight / max(dist, 0.0001);
        float atten = clamp(1.0 - (dist / radius), 0.0, 1.0);
        atten *= atten; // smoother falloff than linear, still one multiply
        vec3 radiance = scene.pointLights[i].colorIntensity.rgb * scene.pointLights[i].colorIntensity.a * atten;
        lit += shadeLight(N, V, L, radiance, albedo, metallic, roughness) * occlusion;
    }

    int spotCount = int(scene.lightCounts.y);
    for (int i = 0; i < MAX_SPOT_LIGHTS; i++) {
        if (i >= spotCount) break;
        vec3 toLight = scene.spotLights[i].posRange.xyz - inWorldPos;
        float dist  = length(toLight);
        float range = scene.spotLights[i].posRange.w;
        if (dist >= range) continue;
        vec3 L = toLight / max(dist, 0.0001);
        vec3  spotDir  = normalize(scene.spotLights[i].dirInnerCos.xyz);
        float innerCos = scene.spotLights[i].dirInnerCos.w;
        float outerCos = scene.spotLights[i].outerCos.x;
        float cosAngle  = dot(-L, spotDir);
        float coneAtten = clamp((cosAngle - outerCos) / max(innerCos - outerCos, 0.0001), 0.0, 1.0);
        float distAtten = clamp(1.0 - (dist / range), 0.0, 1.0);
        distAtten *= distAtten;
        vec3 radiance = scene.spotLights[i].colorIntensity.rgb * scene.spotLights[i].colorIntensity.a * coneAtten * distAtten;
        lit += shadeLight(N, V, L, radiance, albedo, metallic, roughness) * occlusion;
    }

    // Emissive tint is collapsed to a single strength scalar (materialParams.z)
    // rather than carrying Material::emissive's full RGB through the push
    // constant — that block is already at the 128-byte budget (see
    // Renderer::drawLit). The emissive texture (or white, untextured) still
    // supplies color; only its overall brightness is tunable per-material.
    vec3 emissive = push.materialParams.z * (HAS_EMISSIVE_MAP ? texture(emissiveTex, inUV).rgb : vec3(1.0));
    lit += emissive;

    vec4 color = vec4(lit, base.a);
    if (push.fogColor.a > 0.5) {
        float t = clamp((inViewDist - push.fogParams.x) / max(push.fogParams.y - push.fogParams.x, 0.0001), 0.0, 1.0);
        color.rgb = mix(color.rgb, push.fogColor.rgb, t);
    }
    outColor = color;
}
