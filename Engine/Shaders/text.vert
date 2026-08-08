// text.vert — one shared unit quad (binding 0, per-vertex) instanced once
// per glyph (binding 1, per-instance). Screen-space pixel math, same as
// ui.vert — no camera/projection involved.
#version 450

layout(push_constant) uniform Push {
    vec4 screenSize; // xy = viewport size (px)
} push;

// Binding 0 — shared unit quad, reused from Renderer::uiQuad
layout(location=0) in vec3 inPosition; // local corner, xy in [0,1]
layout(location=1) in vec3 inNormal;   // unused
layout(location=2) in vec2 inUV;       // unused
layout(location=3) in vec4 inColor;    // unused

// Binding 1 — per-glyph instance data (Dust::UI::GlyphInstance)
layout(location=4) in vec4  instRectPx;      // xy = pos (px), zw = size (px)
layout(location=5) in vec4  instUVRect;      // xy = uvMin, zw = uvMax
layout(location=6) in vec4  instColor;
layout(location=7) in float instScreenPxRange;

layout(location=0) out vec2  outUV;
layout(location=1) out vec4  outColor;
layout(location=2) out float outScreenPxRange;

void main() {
    vec2 local01  = inPosition.xy;
    vec2 pixelPos = instRectPx.xy + local01 * instRectPx.zw;

    vec2 ndc = (pixelPos / push.screenSize.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outUV             = mix(instUVRect.xy, instUVRect.zw, local01);
    outColor          = instColor;
    outScreenPxRange  = instScreenPxRange;
}
