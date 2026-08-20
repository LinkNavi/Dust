#version 450

// Always bound — Renderer falls back to a 1x1 white texture when a draw has
// no real material, so this sampler is never left unbound. Keeps this one
// shader path serving both the plain vertex-color Mesh API and textured
// multi-material models loaded via loadModelAsset().
layout(set=0, binding=0) uniform sampler2D baseColorTex;

layout(push_constant) uniform Push {
    mat4 transform;
    vec4 baseColorFactor;
    vec4 fogColor;  // rgb = fog color, a > 0.5 = enabled (default off, zero cost when so)
    vec4 fogParams; // x = start distance, y = end distance, zw unused
} push;

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;
layout(location=2) in float inViewDist;

layout(location=0) out vec4 outColor;

void main() {
    vec4 color = inColor * push.baseColorFactor * texture(baseColorTex, inUV);
    if (push.fogColor.a > 0.5) {
        float t = clamp((inViewDist - push.fogParams.x) / max(push.fogParams.y - push.fogParams.x, 0.0001), 0.0, 1.0);
        color.rgb = mix(color.rgb, push.fogColor.rgb, t);
    }
    outColor = color;
}
