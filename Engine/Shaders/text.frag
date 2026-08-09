// text.frag — multichannel signed distance field (MSDF) text rendering.
// One atlas, baked at one fixed size, renders crisply at any size: instead
// of sampling coverage directly, we sample a distance value and threshold
// it in screen space, so the edge stays sharp under scaling the same way a
// vector shape would. See DustPacker's FontImport.cpp for how the atlas
// itself gets built, and Core/UI/Font.cpp for screenPxRange.
#version 450

layout(set=0, binding=0) uniform sampler2D atlas;

layout(push_constant) uniform Push {
    vec4 screenSize; // xy = viewport size (px) — used by text.vert, shared block
    vec4 atlasTexel; // xy = 1/atlasWidth, 1/atlasHeight
} push;

layout(location=0) in vec2  inUV;
layout(location=1) in vec4  inColor;
layout(location=2) in float inScreenPxRange;
layout(location=3) in vec4  inClip;
layout(location=4) in vec2  inPixelPos;
// Outline is per-instance, not per-draw, so one batch can mix outlined and
// plain text — the extra threshold below is skipped when width is 0.
layout(location=5) in vec4  inOutlineCol;
layout(location=6) in float inOutlineWidth;
layout(location=7)  in vec4  inUVRect;
layout(location=8)  in vec4  inColor2;
layout(location=9)  in vec2  inGradPos;   // 0..1 within the owning widget's content box
layout(location=10) in float inGradAngle;

layout(location=0) out vec4 outColor;

// The standard msdfgen reconstruction: the true shape boundary is wherever
// the median of the three channels crosses 0.5 — taking the median instead
// of any single channel is what cancels out the artificial per-channel
// distortion introduced by multichannel encoding in the first place.
float median(vec3 c) {
    return max(min(c.r, c.g), min(max(c.r, c.g), c.b));
}

void main() {
    if (inPixelPos.x < inClip.x || inPixelPos.y < inClip.y ||
        inPixelPos.x > inClip.z || inPixelPos.y > inClip.w) discard;

    // Clamp to half a texel inside this glyph's own cell. The shelf packer
    // puts unrelated glyphs immediately next door, so a bilinear tap right on
    // the cell boundary blends in a neighbour's distance values — which shows
    // up as a hairline down the side of the glyph, most visibly once an
    // outline widens the thresholded band.
    vec2 half_ = push.atlasTexel.xy * 0.5;
    vec2 uv = clamp(inUV, inUVRect.xy + half_, inUVRect.zw - half_);
    vec3 msdf = texture(atlas, uv).rgb;
    float sigDist = median(msdf) - 0.5;

    // Convert the normalized signed distance back into screen pixels so the
    // edge softness is ~1 screen pixel wide regardless of how big the text
    // is drawn relative to the atlas's baked size.
    float dist = sigDist * inScreenPxRange;

    float fillAlpha  = clamp(dist + 0.5, 0.0, 1.0);
    // An MSDF only stores valid distances out to half its range; past that
    // the median saturates and thresholding it paints the glyph's whole
    // padding cell — a grey box instead of an outline. The usable limit is
    // half the range minus the 1px AA the fill already spends, so clamp
    // there. A thicker outline than this needs a wider distance range baked
    // into the atlas (kPxRange in DustPacker's FontImport.cpp), not a bigger
    // number at the call site.
    // Full range/2 lands the threshold exactly on the saturated cell edge,
    // which shows up as a hairline down the side of the glyph's padding. Back
    // off by the AA width on both sides of that boundary.
    float outlineMax = max(inScreenPxRange * 0.5 - 1.0, 0.0);
    float outlinePx  = min(inOutlineWidth, outlineMax);
    float outerAlpha = outlinePx > 0.0
        ? clamp(dist + outlinePx + 0.5, 0.0, 1.0)
        : fillAlpha;

    // Gradient across the text block. inGradPos is the fragment's position in
    // the *widget's* content box, so the ramp is continuous across glyphs and
    // lines rather than restarting inside each letter.
    vec2  gdir   = vec2(cos(inGradAngle), sin(inGradAngle));
    float extent = abs(gdir.x) + abs(gdir.y);
    float tGrad  = clamp(dot(inGradPos - 0.5, gdir) / max(extent, 0.0001) + 0.5, 0.0, 1.0);
    vec4  textCol = mix(inColor, inColor2, tGrad);

    vec4 col = mix(inOutlineCol, textCol, fillAlpha);
    col.a *= outerAlpha;
    if (col.a <= 0.003) discard;
    outColor = col;
}
