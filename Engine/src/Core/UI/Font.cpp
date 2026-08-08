#include "Core/UI/Font.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Renderer.hpp"
#include "AssetManager/FontFormat.hpp"
#include <cstdio>

namespace Dust::UI {

void Font::destroy(VulkanContext& ctx, Renderer& renderer) {
    if (atlasSet != VK_NULL_HANDLE)
        vkFreeDescriptorSets(ctx.device, renderer.materialPool, 1, &atlasSet);
    atlas.destroy(ctx);
}

Font loadFontFromMemory(VulkanContext& ctx, Renderer& renderer, const uint8_t* data, size_t size) {
    Font out;

    FontFmt::Font parsed;
    if (!FontFmt::deserialize(data, size, parsed)) {
        fprintf(stderr, "dust: failed to parse DustFont binary (corrupt, or packed by an older DustPacker?)\n");
        return out;
    }

    out.lineHeight        = parsed.lineHeight;
    out.ascender           = parsed.ascender;
    out.descender           = parsed.descender;
    out.distanceRangePx     = parsed.distanceRangePx;
    out.atlasPixelsPerEm    = parsed.atlasPixelsPerEm;

    // MSDF channels are encoded distance values, not color — must stay
    // linear. Uploading as sRGB would gamma-decode them into nonsense
    // before text.frag ever sees them.
    if (!out.atlas.upload(ctx, parsed.atlasPixels.data(), parsed.atlasWidth, parsed.atlasHeight, /*srgb=*/false))
        fprintf(stderr, "dust: font atlas failed to upload\n");
    out.atlasSet = renderer.createMaterialSet(ctx, out.atlas);

    float invW = parsed.atlasWidth  > 0 ? 1.0f / (float)parsed.atlasWidth  : 0.0f;
    float invH = parsed.atlasHeight > 0 ? 1.0f / (float)parsed.atlasHeight : 0.0f;

    out.glyphs.reserve(parsed.glyphs.size());
    for (auto& g : parsed.glyphs) {
        Glyph glyph;
        glyph.advance     = g.advance;
        glyph.planeLeft   = g.planeLeft;
        glyph.planeBottom = g.planeBottom;
        glyph.planeRight  = g.planeRight;
        glyph.planeTop    = g.planeTop;
        glyph.hasInk      = g.atlasW > 0.0f && g.atlasH > 0.0f;
        if (glyph.hasInk) {
            glyph.uvMinX = g.atlasX * invW;
            glyph.uvMinY = g.atlasY * invH;
            glyph.uvMaxX = (g.atlasX + g.atlasW) * invW;
            glyph.uvMaxY = (g.atlasY + g.atlasH) * invH;
        }
        out.glyphs[g.codepoint] = glyph;
    }

    return out;
}

float layoutText(const Font& font, const std::string& text, float sizePx, Color color,
                 float penX, float penY, std::vector<GlyphInstance>& out) {
    float cursorX = penX;
    float cursorY = penY;

    for (unsigned char c : text) {
        if (c == '\n') {
            cursorX = penX;
            cursorY += font.lineHeight * sizePx;
            continue;
        }

        auto it = font.glyphs.find((uint32_t)c);
        if (it == font.glyphs.end()) continue; // not in the baked glyph set — skip, matches DustPacker's gap behavior
        const Glyph& g = it->second;

        if (g.hasInk) {
            GlyphInstance inst;
            // planeLeft/Right/Top/Bottom are em-space, Y-up (font-natural).
            // Screen space is Y-down, so the sign flips for the vertical
            // axis only: a higher em-Y (further above the baseline) has to
            // land at a *smaller* screen Y (further up the screen).
            inst.x = cursorX + g.planeLeft * sizePx;
            inst.w = (g.planeRight - g.planeLeft) * sizePx;
            inst.y = cursorY - g.planeTop * sizePx;
            inst.h = (g.planeTop - g.planeBottom) * sizePx;
            inst.uvMinX = g.uvMinX; inst.uvMinY = g.uvMinY;
            inst.uvMaxX = g.uvMaxX; inst.uvMaxY = g.uvMaxY;
            inst.color  = color;
            inst.screenPxRange = font.atlasPixelsPerEm > 0.0f
                ? font.distanceRangePx * (sizePx / font.atlasPixelsPerEm)
                : font.distanceRangePx;
            out.push_back(inst);
        }

        cursorX += g.advance * sizePx;
    }

    return cursorX;
}

} // namespace Dust::UI
