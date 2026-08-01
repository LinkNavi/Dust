#version 450

// Always bound — Renderer falls back to a 1x1 white texture when a draw has
// no real material, so this sampler is never left unbound. Keeps this one
// shader path serving both the plain vertex-color Mesh API and textured
// multi-material models loaded via loadModelAsset().
layout(set=0, binding=0) uniform sampler2D baseColorTex;

layout(push_constant) uniform Push {
    mat4 transform;
    vec4 baseColorFactor;
} push;

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;

layout(location=0) out vec4 outColor;

void main() {
    outColor = inColor * push.baseColorFactor * texture(baseColorTex, inUV);
}
