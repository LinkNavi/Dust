#pragma once

#include "Core/Rendering/Texture.hpp"
#include "Core/UI/Color.hpp"
#include "Core/UI/Anchor.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dust {
struct VulkanContext;
struct Renderer;
}

namespace Dust::UI {

// One glyph's placement data, resolved from em units to whatever size the
// text was actually requested at. planeLeft/Bottom/Right/Top mirror
// AssetManager/FontFormat.hpp's GlyphRecord (same Y-up, font-natural em
// space) — see Font.cpp's layoutText() for the Y-flip into screen space.
struct Glyph {
    float advance    = 0.0f;
    float planeLeft = 0.0f, planeBottom = 0.0f, planeRight = 0.0f, planeTop = 0.0f;
    // 0..1 UV rect into the atlas, top-down (uvMin = top-left corner).
    float uvMinX = 0.0f, uvMinY = 0.0f, uvMaxX = 0.0f, uvMaxY = 0.0f;
    bool  hasInk = false; // false for space and other advance-only glyphs
};

// A loaded MSDF font — one atlas texture covers every size crisply (see
// Shaders/text.frag), unlike a bitmap font baked for one size. Produced by
// DustPacker's msdfgen importer (AssetManager/FontFormat.hpp), loaded via
// DustEngine::loadFont().
struct Font {
    Texture atlas;
    VkDescriptorSet atlasSet = VK_NULL_HANDLE; // set=0 — bound at draw time, same pool as Model materials

    float lineHeight  = 0.0f; // em units
    float ascender    = 0.0f; // em units
    float descender   = 0.0f; // em units
    float distanceRangePx  = 0.0f; // MSDF falloff width in *atlas* pixels — text.frag needs this to convert distance to coverage
    float atlasPixelsPerEm = 0.0f; // scale the atlas was baked at

    std::unordered_map<uint32_t, Glyph> glyphs; // keyed by Unicode codepoint

    void destroy(VulkanContext& ctx, Renderer& renderer);
};

// One glyph quad ready to draw — screen-space pixel rect + atlas UV rect +
// tint. What layoutText() produces and what the text instance buffer holds
// verbatim (see Renderer::drawTextInstances).
struct GlyphInstance {
    float x = 0, y = 0, w = 0, h = 0;      // screen-space rect, top-left origin, pixels
    float uvMinX = 0, uvMinY = 0, uvMaxX = 0, uvMaxY = 0;
    Color color;
    // How many *screen* pixels the MSDF falloff should span for this
    // specific glyph, given the size it's actually being drawn at vs the
    // size the atlas was baked at (distanceRangePx * sizePx / atlasPixelsPerEm).
    // Computed per-instance (not as a draw-wide push constant) so a batch
    // mixing multiple text sizes off the same font still thresholds
    // correctly for every glyph in it — see text.frag.
    float screenPxRange = 0.0f;
    // Clip rect in px (minX, minY, maxX, maxY) — fragments outside it get
    // discarded (UITimeline.md Phase 6). layoutText() leaves it wide open;
    // DustEngine::endUI() narrows it to the owning widget's clip rect after
    // the run is appended, so clipping costs nothing during layout.
    float clipMinX = -1e9f, clipMinY = -1e9f, clipMaxX = 1e9f, clipMaxY = 1e9f;
    // Per-glyph outline (px, 0 = off) and its colour. Per-instance rather
    // than per-draw so one batch can mix outlined and plain text — see
    // text.frag, which pays for the extra threshold only when width > 0.
    float outlineWidth = 0.0f;
    Color outlineColor;
    // Gradient across the *text block*, not the individual glyph: gradRect is
    // this glyph's rect expressed in 0..1 of the owning widget's content box,
    // so the ramp runs continuously across a whole line (or several) instead
    // of restarting per letter. color2 == color means flat, same no-branch
    // trick the rect shader uses.
    Color color2;
    float gradMinX = 0.0f, gradMinY = 0.0f, gradMaxX = 1.0f, gradMaxY = 1.0f;
    float gradAngle = 0.0f;
};

Font loadFontFromMemory(VulkanContext& ctx, Renderer& renderer, const uint8_t* data, size_t size);

// Appends one GlyphInstance per drawable character in `text` to `out`,
// starting at pen position (penX, penY) — penY is the *baseline*, matching
// how font metrics/plane bounds are defined. Screen space is Y-down (UI
// convention throughout Dust); em space is Y-up (font-natural), so this is
// the one place that flip happens. Returns the pen X position after the
// last glyph, so callers could chain runs (e.g. multiple colors in one
// line) — Phase 2 doesn't need that yet, just the return value ignored.
float layoutText(const Font& font, const std::string& text, float sizePx, Color color,
                 float penX, float penY, std::vector<GlyphInstance>& out);

// Width in px of `text` laid out on one line (ignores '\n' — feed it a
// single line). Used for alignment and word wrapping.
float measureText(const Font& font, const std::string& text, float sizePx);

// Splits `text` into display lines: always at '\n', and additionally at word
// boundaries when wrapWidth > 0. A single word longer than wrapWidth is left
// overlong rather than broken mid-word — clipping handles the overflow, and
// breaking identifiers/numbers mid-glyph reads worse than a ragged edge.
void wrapText(const Font& font, const std::string& text, float sizePx, float wrapWidth,
              std::vector<std::string>& outLines);

// The full path a text widget takes: wrap, then place every line inside
// `box` per hAlign/vAlign, then emit glyph quads. Vertical placement uses
// the font's em box (ascender/descender) rather than the specific string's
// ink, so mixed runs don't jitter. wrapWidth = 0 disables wrapping.
void layoutTextBox(const Font& font, const std::string& text, float sizePx, Color color,
                   const Rect& box, HAlign hAlign, VAlign vAlign, float wrapWidth,
                   std::vector<GlyphInstance>& out);


} // namespace Dust::UI
