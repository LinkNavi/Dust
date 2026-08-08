// text.frag — multichannel signed distance field (MSDF) text rendering.
// One atlas, baked at one fixed size, renders crisply at any size: instead
// of sampling coverage directly, we sample a distance value and threshold
// it in screen space, so the edge stays sharp under scaling the same way a
// vector shape would. See DustPacker's FontImport.cpp for how the atlas
// itself gets built, and Core/UI/Font.cpp for screenPxRange.
#version 450

layout(set=0, binding=0) uniform sampler2D atlas;

layout(push_constant) uniform Push {
    vec4 screenSize;    // xy = viewport size (px) — used by text.vert, shared block
    vec4 outlineColor;  // rgb + a
    vec4 outlineParams; // x = outline width, in *screen* pixels (0 = no outline)
} push;

layout(location=0) in vec2  inUV;
layout(location=1) in vec4  inColor;
layout(location=2) in float inScreenPxRange;

layout(location=0) out vec4 outColor;

// The standard msdfgen reconstruction: the true shape boundary is wherever
// the median of the three channels crosses 0.5 — taking the median instead
// of any single channel is what cancels out the artificial per-channel
// distortion introduced by multichannel encoding in the first place.
float median(vec3 c) {
    return max(min(c.r, c.g), min(max(c.r, c.g), c.b));
}

void main() {
    vec3 msdf = texture(atlas, inUV).rgb;
    float sigDist = median(msdf) - 0.5;

    // Convert the normalized signed distance back into screen pixels so the
    // edge softness is ~1 screen pixel wide regardless of how big the text
    // is drawn relative to the atlas's baked size.
    float dist = sigDist * inScreenPxRange;

    float fillAlpha  = clamp(dist + 0.5, 0.0, 1.0);
    float outerAlpha = push.outlineParams.x > 0.0
        ? clamp(dist + push.outlineParams.x + 0.5, 0.0, 1.0)
        : fillAlpha;

    vec4 col = mix(push.outlineColor, inColor, fillAlpha);
    col.a *= outerAlpha;
    if (col.a <= 0.003) discard;
    outColor = col;
}
