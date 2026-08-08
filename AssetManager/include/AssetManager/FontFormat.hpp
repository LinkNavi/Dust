#pragma once

// On-disk format for a converted font (DustFont) — produced once, offline,
// by DustPacker's msdfgen importer, and consumed at runtime by Engine's
// font loader. Lives here (not in Engine) for the same reason as
// ModelFormat.hpp: DustPacker doesn't need Vulkan just to write one of
// these, and both sides sharing the exact byte layout means they can never
// drift apart.
//
// The atlas is a multichannel signed distance field (MSDF) — one atlas
// texture renders crisply at any text size, unlike a bitmap font atlas
// baked for one size. See Core/UI/Font.hpp for the runtime side and
// Shaders/text.frag for how the distance field gets turned into coverage.
//
// Layout (little-endian, no padding):
//   [FontHeader]
//   glyphCount x [GlyphRecord]
//   atlasWidth*atlasHeight*4 bytes of RGBA8 atlas pixels (R/G/B = MSDF
//   channels, A unused/255 — kept RGBA so it reuses Engine's existing
//   Texture::upload() path instead of needing a 3-channel format)

#include <cstdint>
#include <cstring>
#include <vector>

namespace Dust::FontFmt {

inline constexpr char     kMagic[4] = { 'D', 'F', 'N', 'T' };
inline constexpr uint32_t kVersion  = 1;

#pragma pack(push, 1)
struct FontHeader {
    char     magic[4];
    uint32_t version;
    uint32_t atlasWidth;
    uint32_t atlasHeight;
    uint32_t glyphCount;
    // All of the below are in em units (1.0 = the font's em square) except
    // distanceRangePx, which is a pixel width at the atlas's baked scale —
    // see Core/UI/Font.hpp for how these combine at draw time.
    float lineHeight;
    float ascender;
    float descender;
    float distanceRangePx;   // MSDF falloff width, in atlas pixels
    float atlasPixelsPerEm;  // scale used when generating the atlas
};

// One glyph's metrics — everything needed to place its quad relative to the
// pen position and sample the right region of the atlas. planeLeft/Bottom/
// Right/Top are in em units (Y-up, font-natural — relative to the glyph
// origin at the baseline, pen x=0) and describe where the quad's corners
// land. atlasX/Y/W/H are a plain top-down pixel rect (Y-down, row 0 = top
// of the atlas image, matching every other texture in Dust) — deliberately
// NOT named left/bottom/right/top like the plane bounds, since that pairing
// previously implied the same Y-up convention for both and they don't share
// one; converted to 0..1 UV at load time once atlas dimensions are known.
struct GlyphRecord {
    uint32_t codepoint = 0;
    float    advance   = 0.0f;
    float    planeLeft = 0.0f, planeBottom = 0.0f, planeRight = 0.0f, planeTop = 0.0f;
    float    atlasX = 0.0f, atlasY = 0.0f, atlasW = 0.0f, atlasH = 0.0f;
};
#pragma pack(pop)

// ── In-memory, owns its data ───────────────────

struct Font {
    uint32_t atlasWidth  = 0;
    uint32_t atlasHeight = 0;
    float    lineHeight  = 0.0f;
    float    ascender    = 0.0f;
    float    descender   = 0.0f;
    float    distanceRangePx  = 0.0f;
    float    atlasPixelsPerEm = 0.0f;

    std::vector<GlyphRecord> glyphs;
    std::vector<uint8_t>     atlasPixels; // RGBA8, atlasWidth*atlasHeight*4 bytes
};

inline std::vector<uint8_t> serialize(const Font& f) {
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        out.insert(out.end(), b, b + n);
    };

    FontHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version         = kVersion;
    header.atlasWidth      = f.atlasWidth;
    header.atlasHeight     = f.atlasHeight;
    header.glyphCount      = (uint32_t)f.glyphs.size();
    header.lineHeight      = f.lineHeight;
    header.ascender        = f.ascender;
    header.descender       = f.descender;
    header.distanceRangePx = f.distanceRangePx;
    header.atlasPixelsPerEm = f.atlasPixelsPerEm;
    put(&header, sizeof(header));

    put(f.glyphs.data(), f.glyphs.size() * sizeof(GlyphRecord));

    size_t expectedPixels = (size_t)f.atlasWidth * f.atlasHeight * 4;
    put(f.atlasPixels.data(), expectedPixels < f.atlasPixels.size() ? expectedPixels : f.atlasPixels.size());

    return out;
}

// Returns false on any malformed/truncated input — always check before using `out`.
inline bool deserialize(const uint8_t* data, size_t size, Font& out) {
    size_t off = 0;
    auto get = [&](void* p, size_t n) -> bool {
        if (off + n > size) return false;
        memcpy(p, data + off, n);
        off += n;
        return true;
    };

    FontHeader header{};
    if (!get(&header, sizeof(header))) return false;
    if (memcmp(header.magic, kMagic, 4) != 0) return false;
    if (header.version != kVersion) return false;

    out = Font{};
    out.atlasWidth       = header.atlasWidth;
    out.atlasHeight      = header.atlasHeight;
    out.lineHeight        = header.lineHeight;
    out.ascender          = header.ascender;
    out.descender         = header.descender;
    out.distanceRangePx   = header.distanceRangePx;
    out.atlasPixelsPerEm  = header.atlasPixelsPerEm;

    out.glyphs.resize(header.glyphCount);
    size_t glyphBytes = (size_t)header.glyphCount * sizeof(GlyphRecord);
    if (glyphBytes && !get(out.glyphs.data(), glyphBytes)) return false;

    size_t pixelBytes = (size_t)out.atlasWidth * out.atlasHeight * 4;
    out.atlasPixels.resize(pixelBytes);
    if (pixelBytes && !get(out.atlasPixels.data(), pixelBytes)) return false;

    return true;
}

} // namespace Dust::FontFmt
