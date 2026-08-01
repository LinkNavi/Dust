// ui.frag — rounded-rect fill + border via signed distance field, 1px
// analytic antialiasing. Covers `.background()`/`.border()` for every
// DustUI widget; text/sprites/custom shaders are later phases (see
// UITimeline.md) and don't go through this shader.
#version 450

layout(push_constant) uniform Push {
    vec4 rectPx;
    vec4 screenSize;
    vec4 fillColor;
    vec4 borderColor;
    vec4 params; // x = border width (px), y = corner radius (px), z = opacity
} push;

layout(location = 0) in vec2 inLocalPx;
layout(location = 0) out vec4 outColor;

// Signed distance from p to the edge of a w/h box centered at origin with
// corner radius r — negative inside, positive outside, 0 exactly on edge.
float sdRoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 size = push.rectPx.zw;
    vec2 halfSize = size * 0.5;
    vec2 p = inLocalPx - halfSize; // widget-center-relative

    float r = min(push.params.y, min(halfSize.x, halfSize.y));
    float dist = sdRoundedBox(p, halfSize, r);

    float aa = 1.0; // ~1px softness, keeps edges/corners from aliasing
    float outerAlpha = 1.0 - smoothstep(-aa, aa, dist);

    float borderWidth = push.params.x;
    float borderMask = 0.0;
    if (borderWidth > 0.0) {
        float innerAlpha = 1.0 - smoothstep(-aa, aa, dist + borderWidth);
        borderMask = outerAlpha - innerAlpha; // ring between the outer edge and the inset edge
    }

    vec4 col = mix(push.fillColor, push.borderColor, borderMask);
    col.a *= outerAlpha * push.params.z; // z = opacity
    if (col.a <= 0.003) discard;
    outColor = col;
}
