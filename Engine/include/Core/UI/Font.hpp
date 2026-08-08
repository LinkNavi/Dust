#pragma once

#include "Core/Rendering/Texture.hpp"
#include "Core/UI/Color.hpp"
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

} // namespace Dust::UI
