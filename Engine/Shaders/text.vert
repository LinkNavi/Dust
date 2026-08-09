// text.vert — one shared unit quad (binding 0, per-vertex) instanced once
// per glyph (binding 1, per-instance). Screen-space pixel math, same as
// ui.vert — no camera/projection involved.
#version 450

layout(push_constant) uniform Push {
    vec4 screenSize; // xy = viewport size (px)
    vec4 atlasTexel; // xy = 1/atlasWidth, 1/atlasHeight
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
layout(location=8)  in vec4  instClip;       // minX, minY, maxX, maxY (px) — Phase 6 clipping
layout(location=9)  in vec4  instOutlineCol;
layout(location=10) in float instOutlineWidth; // px, 0 = no outline
layout(location=11) in vec4  instColor2;       // gradient end colour
layout(location=12) in vec4  instGradRect;     // this glyph's rect in 0..1 of the text box
layout(location=13) in float instGradAngle;

layout(location=0) out vec2  outUV;
layout(location=1) out vec4  outColor;
layout(location=2) out float outScreenPxRange;
layout(location=3) out vec4  outClip;
layout(location=4)  out vec2  outPixelPos;
layout(location=5)  out vec4  outOutlineCol;
layout(location=6)  out float outOutlineWidth;
layout(location=7)  out vec4  outUVRect;   // this glyph's cell, for edge clamping in the frag
layout(location=8)  out vec4  outColor2;
layout(location=9)  out vec2  outGradPos;  // position within the text box, 0..1
layout(location=10) out float outGradAngle;

void main() {
    vec2 local01 = inPosition.xy;

    // The quad stays tight to the glyph's baked cell — that cell already
    // includes the padding the outline lives in (DustPacker bakes kPadPx of
    // margin around every glyph). Growing the quad and its UVs instead would
    // sample whatever neighbour the shelf packer put next door.
    vec2 pixelPos = instRectPx.xy + local01 * instRectPx.zw;

    vec2 ndc = (pixelPos / push.screenSize.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outUV             = mix(instUVRect.xy, instUVRect.zw, local01);
    outColor          = instColor;
    outScreenPxRange  = instScreenPxRange;
    outClip           = instClip;
    outPixelPos       = pixelPos;
    outOutlineCol     = instOutlineCol;
    outOutlineWidth   = instOutlineWidth;
    outUVRect         = instUVRect;
    outColor2         = instColor2;
    outGradPos        = mix(instGradRect.xy, instGradRect.zw, local01);
    outGradAngle      = instGradAngle;
}
