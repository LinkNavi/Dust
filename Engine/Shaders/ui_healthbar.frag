// ui_healthbar.frag — custom shader widget (UITimeline.md Phase 9) drawing a
// dog-leg health bar: a straight run, a 45° drop, then another straight run
// (------\________). The whole thing is one quad; the shape is a signed
// distance field against a 3-segment polyline, so it antialiases cleanly and
// the fill boundary can sit anywhere along it without extra geometry.
//
// Interface is the stock one (pairs with ui.vert). Params:
//   params0.x = fill fraction, 0..1
//   params0.y = where the bend starts, 0..1 across the widget width
//   params0.z = bar thickness in px
//   params0.w = drop height in px (the 45° run is this wide too, by definition)
// inFill  = filled colour, inBorder = empty-track colour.
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
layout(location=8) in vec4 inParams0;
layout(location=9) in vec4 inParams1;

layout(location=0) out vec4 outColor;

// Distance from p to segment ab.
float sdSegment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    if (inPixelPos.x < inClip.x || inPixelPos.y < inClip.y ||
        inPixelPos.x > inClip.z || inPixelPos.y > inClip.w) discard;

    float thickness = max(inParams0.z, 1.0);
    float half_     = thickness * 0.5;
    float drop      = max(inParams0.w, 0.0);

    // Keep the whole bar inside the widget: the centre line starts half a
    // thickness down and ends half a thickness up from the bottom, so `drop`
    // is clamped to whatever vertical room is actually left.
    float yTop = half_;
    float room = inSizePx.y - thickness;
    drop = min(drop, max(room, 0.0));
    float yBot = yTop + drop;

    // Horizontal run of the diagonal equals its drop — that's what makes the
    // slope exactly 45°, rather than "whatever this widget's aspect implies".
    float x0 = clamp(inParams0.y, 0.0, 1.0) * inSizePx.x;
    float x1 = min(x0 + drop, inSizePx.x);
    x0 = min(x0, x1);

    vec2 a = vec2(0.0,          yTop);
    vec2 b = vec2(x0,           yTop);
    vec2 c = vec2(x1,           yBot);
    vec2 d = vec2(inSizePx.x,   yBot);

    vec2 p = inLocalPx;
    float dist = min(min(sdSegment(p, a, b), sdSegment(p, b, c)), sdSegment(p, c, d));
    float sd   = dist - half_; // negative inside the band

    float aa    = 1.0;
    float shape = 1.0 - smoothstep(-aa, aa, sd);
    if (shape <= 0.003) discard;

    // Fill runs along the widget's width rather than along the polyline's
    // arc length — the horizontal reading is what a player actually parses,
    // and it keeps "half health" at the visual midpoint of the bar.
    float filled = clamp(inParams0.x, 0.0, 1.0) * inSizePx.x;
    float fillMask = 1.0 - smoothstep(filled - 1.0, filled + 1.0, p.x);

    vec4 col = mix(inBorder, inFill, fillMask);
    col.a *= shape * inParams.z;
    if (col.a <= 0.003) discard;
    outColor = col;
}
