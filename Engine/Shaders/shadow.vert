// shadow.vert — depth-only pass for the directional light's shadow map (see
// Renderer::beginShadowPass/drawShadow and lit.frag's HAS_SHADOWS sampling).
// Same Mesh::Vertex layout as every other pipeline (PipelineBuilder hardcodes
// it), but only position is actually read here.
#version 450

layout(push_constant) uniform Push {
    mat4 lightViewProj;
    mat4 model;
} push;

layout(location=0) in vec3 inPosition;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec2 inUV;
layout(location=3) in vec4 inColor;

void main() {
    gl_Position = push.lightViewProj * push.model * vec4(inPosition, 1.0);
}
