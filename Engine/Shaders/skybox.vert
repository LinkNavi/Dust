// skybox.vert — shared vertex stage for every skybox (cubemap or procedural).
// Draws Renderer::skyboxCube (a unit cube) centered on the camera: the view
// matrix's translation is stripped on the CPU side (see Renderer::drawSkybox),
// so the cube never moves relative to the eye. gl_Position.xyww forces the
// post-projection depth to w/w = 1.0 — the far plane — so the skybox pipeline
// can draw with depthCompareOp = LESS_OR_EQUAL and lose every pixel that any
// real geometry already wrote, whatever order they were drawn in.
#version 450

layout(push_constant) uniform Push {
    mat4 viewRotProj; // projection * mat4(mat3(view)) — rotation only, no translation
    vec4 sunDir;      // xyz = direction the sun's light travels, w unused
    vec4 params;      // x = time (seconds), yzw = free per-draw floats
    vec4 tint;        // rgb = multiply on the final color, a unused
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

// World-space view direction — the cube's local position doubles as the
// direction from the eye through this vertex. Normalize in the fragment.
layout(location = 0) out vec3 outDir;

void main() {
    outDir = inPosition;
    vec4 clip = push.viewRotProj * vec4(inPosition, 1.0);
    gl_Position = clip.xyww;
}
