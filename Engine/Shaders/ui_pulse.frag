// ui_pulse.frag — example custom shader widget (UITimeline.md Phase 9).
//
// A custom UI shader is a *fragment shader only*: it pairs with the stock
// ui.vert, so it must declare exactly this input interface (locations 0-9)
// and the same set=0 sampler, even if it ignores most of it. Everything a
// widget hands you arrives per-instance; params0/params1 are the eight free
// floats set via .shader(pipeline, p0..p7).
//
// This one draws an animated radial pulse clipped to the widget's rounded
// rect: params0.x = time, params0.y = speed, params0.z = ring count.
#version 450

layout(set=0, binding=0) uniform sampler2D tex;

layout(location=0) in vec2 inLocalPx;
layout(location=1) in vec2 inSizePx;
layout(location=2) in vec4 inFill;
layout(location=3) in vec4 inBorder;
layout(location=4) in vec4 inParams;   // x = border width, y = corner radius, z = opacity
layout(location=5) in vec4 inClip;
layout(location=6) in vec2 inPixelPos;
layout(location=7) in vec2 inSpriteUV;
layout(location=8) in vec4 inParams0;  // x = time, y = speed, z = rings
layout(location=9) in vec4 inParams1;

layout(location=0) out vec4 outColor;

float sdRoundedBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    if (inPixelPos.x < inClip.x || inPixelPos.y < inClip.y ||
        inPixelPos.x > inClip.z || inPixelPos.y > inClip.w) discard;

    vec2 halfSize = inSizePx * 0.5;
    vec2 p = inLocalPx - halfSize;

    float r = min(inParams.y, min(halfSize.x, halfSize.y));
    float dist = sdRoundedBox(p, halfSize, r);
    float outerAlpha = 1.0 - smoothstep(-1.0, 1.0, dist);

    // Normalized radial distance from the widget's center, so the pattern
    // doesn't stretch with a non-square widget.
    float rad   = length(p / halfSize);
    float rings = max(inParams0.z, 1.0);
    float wave  = 0.5 + 0.5 * sin(rad * rings * 6.2831 - inParams0.x * inParams0.y);

    vec3 col = mix(inFill.rgb, inBorder.rgb, wave);
    float a  = inFill.a * outerAlpha * inParams.z;
    if (a <= 0.003) discard;
    outColor = vec4(col, a);
}
