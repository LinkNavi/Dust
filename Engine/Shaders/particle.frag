#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
    vec4 fogColor;  // rgb = fog color, a > 0.5 = enabled — matches default.frag's Push block
    vec4 fogParams; // x = start distance, y = end distance, zw unused
} push;

layout(location=0) in vec4 inColor;
layout(location=1) in vec2 inUV;
layout(location=2) in float inViewDist;

layout(location=0) out vec4 fragColor;

void main() {
    vec4 c = texture(tex, inUV) * inColor;
    if (c.a < 0.01) discard;
    if (push.fogColor.a > 0.5) {
        float t = clamp((inViewDist - push.fogParams.x) / max(push.fogParams.y - push.fogParams.x, 0.0001), 0.0, 1.0);
        c.rgb = mix(c.rgb, push.fogColor.rgb, t);
    }
    fragColor = c;
}
