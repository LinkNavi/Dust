// ui.vert — screen-space quad for DustUI widgets. Position math happens
// entirely in pixels (no camera/projection involved, unlike default.vert).
#version 450

layout(push_constant) uniform Push {
    vec4 rectPx;     // xy = top-left position (px), zw = size (px)
    vec4 screenSize; // xy = viewport size (px), zw unused
    vec4 fillColor;
    vec4 borderColor;
    vec4 params;     // x = border width (px), y = corner radius (px), z = opacity
} push;

layout(location=0) in vec3 inPosition; // unit quad corner, xy in [0,1] — reused as the local UV directly
layout(location=1) in vec3 inNormal;   // unused
layout(location=2) in vec2 inUV;       // unused
layout(location=3) in vec4 inColor;    // unused

layout(location=0) out vec2 outLocalPx; // pixel offset from this widget's top-left, for the SDF in ui.frag

void main() {
    vec2 local01  = inPosition.xy;
    vec2 pixelPos = push.rectPx.xy + local01 * push.rectPx.zw;

    // Pixel -> NDC. No Y-flip needed (unlike the 3D camera path) — pixel Y
    // already increases downward, matching Vulkan's Y-down clip space.
    vec2 ndc = (pixelPos / push.screenSize.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outLocalPx = local01 * push.rectPx.zw;
}
