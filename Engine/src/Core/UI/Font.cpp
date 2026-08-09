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

float measureText(const Font& font, const std::string& text, float sizePx) {
    float w = 0.0f;
    for (unsigned char c : text) {
        if (c == '\n') continue;
        auto it = font.glyphs.find((uint32_t)c);
        if (it != font.glyphs.end()) w += it->second.advance * sizePx;
    }
    return w;
}

void wrapText(const Font& font, const std::string& text, float sizePx, float wrapWidth,
              std::vector<std::string>& outLines) {
    outLines.clear();

    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string paragraph = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);

        if (wrapWidth <= 0.0f) {
            outLines.push_back(paragraph);
        } else {
            // Greedy word wrap: keep adding words while they fit, break when
            // one doesn't. Words wider than the box on their own get a line
            // to themselves and overflow it rather than being split.
            std::string line;
            size_t wordStart = 0;
            while (wordStart <= paragraph.size()) {
                size_t sp = paragraph.find(' ', wordStart);
                std::string word = paragraph.substr(wordStart, sp == std::string::npos ? std::string::npos : sp - wordStart);

                std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && measureText(font, candidate, sizePx) > wrapWidth) {
                    outLines.push_back(line);
                    line = word;
                } else {
                    line = candidate;
                }

                if (sp == std::string::npos) break;
                wordStart = sp + 1;
            }
            outLines.push_back(line);
        }

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

void layoutTextBox(const Font& font, const std::string& text, float sizePx, Color color,
                   const Rect& box, HAlign hAlign, VAlign vAlign, float wrapWidth,
                   std::vector<GlyphInstance>& out) {
    std::vector<std::string> lines;
    wrapText(font, text, sizePx, wrapWidth, lines);
    if (lines.empty()) return;

    float lineH     = font.lineHeight * sizePx;
    float emHeight  = (font.ascender - font.descender) * sizePx;
    // Block height is the em box of the first line plus a full line advance
    // for each one after it — using lineHeight for all of them would leave
    // the block bottom-heavy by the leading of the last line.
    float blockH    = emHeight + lineH * (float)(lines.size() - 1);

    float topY = box.y;
    if (vAlign == VAlign::Middle) topY = box.y + (box.h - blockH) * 0.5f;
    else if (vAlign == VAlign::Bottom) topY = box.y + box.h - blockH;

    float baselineY = topY + font.ascender * sizePx;

    for (const std::string& line : lines) {
        float x = box.x;
        if (hAlign != HAlign::Left) {
            float lineW = measureText(font, line, sizePx);
            x = (hAlign == HAlign::Center) ? box.x + (box.w - lineW) * 0.5f
                                           : box.x + box.w - lineW;
        }
        layoutText(font, line, sizePx, color, x, baselineY, out);
        baselineY += lineH;
    }
}

} // namespace Dust::UI
