// ui.vert — screen-space quads for DustUI widgets, one shared unit quad
// (binding 0, per-vertex) instanced once per widget (binding 1, per-instance,
// = Dust::UI::RectInstance). Position math happens entirely in pixels (no
// camera/projection involved, unlike default.vert).
#version 450

layout(push_constant) uniform Push {
    vec4 screenSize; // xy = viewport size (px), zw unused
} push;

// Binding 0 — shared unit quad, reused from Renderer::uiQuad
layout(location=0) in vec3 inPosition; // unit quad corner, xy in [0,1]
layout(location=1) in vec3 inNormal;   // unused
layout(location=2) in vec2 inUV;       // unused
layout(location=3) in vec4 inColor;    // unused

// Binding 1 — per-widget instance data
layout(location=4) in vec4 instRectPx;  // xy = top-left (px), zw = size (px)
layout(location=5) in vec4 instFill;
layout(location=6) in vec4 instBorder;
layout(location=7) in vec4 instParams;  // x = border width, y = corner radius, z = opacity
layout(location=8) in vec4 instClip;    // minX, minY, maxX, maxY (px)
layout(location=9)  in vec4 instUV;      // xy = uvMin, zw = uvMax
layout(location=10) in vec4 instParams0; // free-form, custom shader widgets only (Phase 9)
layout(location=11) in vec4 instParams1;
layout(location=12) in vec4 instFill2;   // gradient end colour
layout(location=13) in vec4 instRadii;   // per-corner radius px: TL, TR, BR, BL
layout(location=14) in vec4 instExtra;   // x = fill gradient angle (rad), y = shadow blur px, z = border gradient angle
layout(location=15) in vec4 instBorder2; // border gradient end colour

layout(location=0) out vec2 outLocalPx; // px offset from this widget's top-left, for the SDF in ui.frag
layout(location=1) out vec2 outSizePx;
layout(location=2) out vec4 outFill;
layout(location=3) out vec4 outBorder;
layout(location=4) out vec4 outParams;
layout(location=5) out vec4 outClip;
layout(location=6) out vec2 outPixelPos;
layout(location=7)  out vec2 outSpriteUV;
layout(location=8)  out vec4 outParams0;
layout(location=9)  out vec4 outParams1;
layout(location=10) out vec4 outFill2;
layout(location=11) out vec4 outRadii;
layout(location=12) out vec4 outExtra;
layout(location=13) out vec2 outLocal01;
layout(location=14) out vec4 outBorder2;

void main() {
    vec2 local01  = inPosition.xy;
    vec2 pixelPos = instRectPx.xy + local01 * instRectPx.zw;

    // Pixel -> NDC. No Y-flip needed (unlike the 3D camera path) — pixel Y
    // already increases downward, matching Vulkan's Y-down clip space.
    vec2 ndc = (pixelPos / push.screenSize.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outLocalPx   = local01 * instRectPx.zw;
    outSizePx    = instRectPx.zw;
    outFill      = instFill;
    outBorder    = instBorder;
    outParams    = instParams;
    outClip      = instClip;
    outPixelPos  = pixelPos;
    outSpriteUV  = mix(instUV.xy, instUV.zw, local01);
    outParams0   = instParams0;
    outParams1   = instParams1;
    outFill2     = instFill2;
    outRadii     = instRadii;
    outExtra     = instExtra;
    outLocal01   = local01;
    outBorder2   = instBorder2;
}
