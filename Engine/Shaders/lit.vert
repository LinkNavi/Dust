// lit.vert — Blinn-Phong ubershader vertex stage (see lit.frag for the
// lighting math and the feature-flag bitmask this pairs with).
//
// Unlike default.vert, the push constant carries the model matrix alone
// (not a baked MVP) — the lit fragment stage needs world-space position and
// normal for lighting, so the view/projection multiply has to stay
// separable. viewProj lives in the set=1 Scene UBO instead (bound once per
// frame, see DustEngine::beginMode3D/Renderer::updateLights), which is what
// keeps this push constant block at exactly 128 bytes despite adding a
// world matrix — see the Push block below and Renderer.hpp's LightsUBOData.
#version 450

layout(set=1, binding=0) uniform Scene {
    mat4 viewProj;
    vec4 cameraPos;
    // dirLight/ambient/lightCounts/point/spot arrays follow in the buffer —
    // irrelevant to the vertex stage, so left undeclared here. Valid GLSL:
    // this UBO block only needs to be a prefix of what's actually bound.
} scene;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 baseColorFactor;  // unused here — read by lit.frag, shared block so offsets line up
    vec4 fogColor;
    vec4 fogParams;
    vec4 materialParams;   // unused here too
} push;

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;

layout(location=0) out vec4 outColor;
layout(location=1) out vec2 outUV;
// Same free view-distance trick as default.vert — clip.w is the view-space
// depth for a standard perspective matrix.
layout(location=2) out float outViewDist;
layout(location=3) out vec3 outWorldPos;
layout(location=4) out vec3 outWorldNormal;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position   = scene.viewProj * worldPos;

    outColor       = inColor;
    outUV          = inUV;
    outViewDist    = gl_Position.w;
    outWorldPos    = worldPos.xyz;
    // mat3(model) as the normal matrix skips the inverse-transpose — wrong
    // under non-uniform scale, correct otherwise. Cheap and good enough for
    // the low-end target; revisit if models start shipping stretched scale.
    outWorldNormal = mat3(push.model) * inNormal;
}
