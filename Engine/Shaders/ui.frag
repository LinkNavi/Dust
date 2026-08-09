// ui.frag — everything a stock DustUI widget can look like, on one quad:
// rounded rect (per-corner radii) with a border, an optional sprite texture,
// an optional linear gradient fill, an optional drop shadow, and a clip rect.
// All per-widget state arrives as instance attributes, not push constants,
// so the whole UI draws in one instanced call per texture/pipeline — see
// UITimeline.md Phases 4-6.
#version 450

// Untextured widgets bind the renderer's 1x1 white fallback, so this is an
// unconditional multiply rather than a branch.
layout(set=0, binding=0) uniform sampler2D tex;

layout(location=0) in vec2 inLocalPx;
layout(location=1) in vec2 inSizePx;
layout(location=2) in vec4 inFill;
layout(location=3) in vec4 inBorder;
layout(location=4) in vec4 inParams;   // x = border width, y = corner radius, z = opacity
layout(location=5) in vec4 inClip;     // minX, minY, maxX, maxY (px)
layout(location=6) in vec2 inPixelPos;
layout(location=7) in vec2 inSpriteUV;
// Declared but unused here — a custom shader widget's fragment shader reads
// these instead. Keeping the interface identical means one vertex shader and
// one instance layout serve both.
layout(location=8)  in vec4 inParams0;
layout(location=9)  in vec4 inParams1;
layout(location=10) in vec4 inFill2;   // gradient end colour
layout(location=11) in vec4 inRadii;   // TL, TR, BR, BL (px)
layout(location=12) in vec4 inExtra;   // x = fill gradient angle, y = shadow blur px, z = border gradient angle
layout(location=13) in vec2 inLocal01;
layout(location=14) in vec4 inBorder2;

layout(location=0) out vec4 outColor;

// Signed distance to a box centred at the origin with a *different* radius
// per corner — negative inside. Picking the radius by quadrant before the
// usual rounded-box formula is all it takes; the corner being evaluated is
// the only one that can possibly be the nearest.
float sdRoundedBox(vec2 p, vec2 halfSize, vec4 radii) {
    // radii = TL, TR, BR, BL
    float r = (p.x > 0.0)
        ? ((p.y > 0.0) ? radii.z : radii.y)   // right: bottom-right / top-right
        : ((p.y > 0.0) ? radii.w : radii.x);  // left:  bottom-left  / top-left
    r = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Position along a gradient axis, 0..1, for a widget-local 0..1 point.
// Normalising by the axis' projected extent keeps the full colour ramp inside
// the widget at every angle instead of clipping the corners off a diagonal.
float gradientT(vec2 local01, float angle) {
    vec2 dir = vec2(cos(angle), sin(angle));
    float extent = abs(dir.x) + abs(dir.y); // projection of the unit square onto dir
    return clamp(dot(local01 - 0.5, dir) / max(extent, 0.0001) + 0.5, 0.0, 1.0);
}

void main() {
    // Clip first — cheapest possible rejection for anything scrolled or
    // overflowing out of its container.
    if (inPixelPos.x < inClip.x || inPixelPos.y < inClip.y ||
        inPixelPos.x > inClip.z || inPixelPos.y > inClip.w) discard;

    vec2 halfSize = inSizePx * 0.5;
    vec2 p = inLocalPx - halfSize; // widget-center-relative
    float dist = sdRoundedBox(p, halfSize, inRadii);

    float blur = inExtra.y;
    if (blur > 0.0) {
        // Shadow pass: the same silhouette, faded out over `blur` px instead
        // of the usual 1px edge. It's a separate instance emitted just before
        // the widget itself, so it costs no extra draw call — only a slot.
        float a = (1.0 - smoothstep(-blur, blur, dist)) * inFill.a * inParams.z;
        if (a <= 0.003) discard;
        outColor = vec4(inFill.rgb, a);
        return;
    }

    float aa = 1.0; // ~1px softness, keeps edges/corners from aliasing
    float outerAlpha = 1.0 - smoothstep(-aa, aa, dist);

    float borderWidth = inParams.x;
    float borderMask = 0.0;
    if (borderWidth > 0.0) {
        float innerAlpha = 1.0 - smoothstep(-aa, aa, dist + borderWidth);
        borderMask = outerAlpha - innerAlpha; // ring between the outer edge and the inset edge
    }

    // Linear gradient: project the widget-local 0..1 position onto the
    // gradient axis. The end colour defaults to the start colour, so a flat
    // fill is the same code path with a no-op mix — no branch.
    float tFill   = gradientT(inLocal01, inExtra.x);
    float tBorder = gradientT(inLocal01, inExtra.z);
    vec4  fill    = mix(inFill, inFill2, tFill) * texture(tex, inSpriteUV);
    vec4  border  = mix(inBorder, inBorder2, tBorder);

    vec4 col = mix(fill, border, borderMask);
    col.a *= outerAlpha * inParams.z; // z = opacity
    if (col.a <= 0.003) discard;
    outColor = col;
}
