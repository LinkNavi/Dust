#include "FontImport.hpp"
#include "AssetManager/FontFormat.hpp"

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace fs = std::filesystem;
namespace Fmt = Dust::FontFmt;

namespace DustPacker {

namespace {

// Baked-in resolution — the atlas is generated as if rendering at this many
// pixels per em. MSDF keeps things crisp well above and below this size;
// it's a resolution/atlas-size tradeoff, not a hard limit like a bitmap font.
constexpr double kPixelsPerEm = 48.0;
// Distance field falloff width, in atlas pixels — matches msdf-atlas-gen's
// usual default is 4; widened here so per-glyph outlines (Widget::textOutline)
// have valid distance to threshold against — the usable outline width is
// about half this, minus the fill's own 1px of antialiasing. The shader
// (text.frag) needs this exact value to convert
// sampled distance back into screen-space coverage.
constexpr double kPxRange = 12.0;
// Padding around each glyph's tight ink bounds so the falloff has room to
// breathe without bumping into a neighboring glyph in the atlas.
constexpr int kPadPx = 12;
// Fixed atlas width — height is however tall the shelf packer ends up
// needing for this glyph set, no wasted rows.
constexpr int kAtlasWidth = 512;

struct PackedGlyph {
    Fmt::GlyphRecord     record; // plane bounds + advance filled in here; atlas* filled in after packing
    int                  w = 0, h = 0;
    std::vector<uint8_t> pixels; // RGBA8, top-down (row 0 = top), w*h*4 — already Y-flipped from msdfgen's bottom-up bitmap
};

// Rejects a handful of glyphs whose ink bounds come back degenerate (NaN/
// inf, or inverted) instead of feeding garbage into the packer — happens
// occasionally with combining marks or malformed glyphs in some fonts.
bool boundsUsable(const msdfgen::Shape::Bounds& b) {
    return std::isfinite(b.l) && std::isfinite(b.b) && std::isfinite(b.r) && std::isfinite(b.t)
        && b.r > b.l && b.t > b.b;
}

// Simple shelf/row packer — glyphs are placed left to right, wrapping to a
// new row when the current one is full. Not as tight as a real bin packer,
// but simple, fast, and good enough for a few hundred glyphs.
struct ShelfPacker {
    int width;
    int cursorX = 0, cursorY = 0, shelfHeight = 0;

    explicit ShelfPacker(int w) : width(w) {}

    void place(int w, int h, int& outX, int& outY) {
        if (cursorX + w > width) {
            cursorY += shelfHeight;
            cursorX = 0;
            shelfHeight = 0;
        }
        outX = cursorX;
        outY = cursorY;
        cursorX += w;
        shelfHeight = std::max(shelfHeight, h);
    }

    int usedHeight() const { return cursorY + shelfHeight; }
};

} // namespace

std::vector<uint8_t> convertFontToDustBinary(const fs::path& path) {
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        fprintf(stderr, "dustpacker: failed to initialize FreeType\n");
        return {};
    }

    msdfgen::FontHandle* font = msdfgen::loadFont(ft, path.string().c_str());
    if (!font) {
        fprintf(stderr, "dustpacker: failed to load font '%s'\n", path.string().c_str());
        msdfgen::deinitializeFreetype(ft);
        return {};
    }

    msdfgen::FontMetrics ftMetrics{};
    msdfgen::getFontMetrics(ftMetrics, font, msdfgen::FONT_SCALING_EM_NORMALIZED);

    // Printable ASCII — 0x20 (space) through 0x7E (~). Covers everything the
    // engine's own demos/UI need today; widening the codepoint set later is
    // a packer-only change, same as adding a new model format was.
    std::vector<PackedGlyph> glyphs;
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) {
        msdfgen::Shape shape;
        double advance = 0.0;
        if (!msdfgen::loadGlyph(shape, font, (msdfgen::unicode_t)cp, msdfgen::FONT_SCALING_EM_NORMALIZED, &advance))
            continue; // not present in this font — skip, leave a gap in the glyph table

        PackedGlyph g;
        g.record.codepoint = cp;
        g.record.advance    = (float)advance;

        if (!shape.validate() || shape.contours.empty()) {
            // Whitespace (space, etc.) — advance only, nothing to rasterize.
            glyphs.push_back(std::move(g));
            continue;
        }

        shape.normalize();
        msdfgen::edgeColoringSimple(shape, 3.0);

        auto bounds = shape.getBounds();
        if (!boundsUsable(bounds)) {
            glyphs.push_back(std::move(g)); // advance-only fallback, same as whitespace
            continue;
        }

        double padEm = kPadPx / kPixelsPerEm;
        int w = std::max(1, (int)std::ceil((bounds.r - bounds.l) * kPixelsPerEm) + 2 * kPadPx);
        int h = std::max(1, (int)std::ceil((bounds.t - bounds.b) * kPixelsPerEm) + 2 * kPadPx);

        // Shape-space point that lands on pixel (0,0) of this glyph's cell —
        // see planeLeft/Bottom below, which describe exactly this offset in
        // em units for the runtime to reconstruct the same placement.
        msdfgen::Vector2 translate(-bounds.l + padEm, -bounds.b + padEm);

        msdfgen::SDFTransformation transformation(
            msdfgen::Projection(msdfgen::Vector2(kPixelsPerEm, kPixelsPerEm), translate),
            msdfgen::Range(kPxRange / kPixelsPerEm));

        msdfgen::Bitmap<float, 3> msdf(w, h);
        msdfgen::generateMSDF(msdf, shape, transformation, msdfgen::MSDFGeneratorConfig());

        g.w = w;
        g.h = h;
        g.pixels.resize((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            // msdfgen's default bitmap orientation is Y_UPWARD (row 0 =
            // bottom of the glyph); Dust's textures are top-down everywhere
            // else (stb_image, the model/UI atlases), so flip here once,
            // offline, rather than teaching the runtime a second convention.
            int srcY = h - 1 - y;
            for (int x = 0; x < w; x++) {
                const float* px = msdf(x, srcY);
                uint8_t* dst = &g.pixels[((size_t)y * w + x) * 4];
                dst[0] = msdfgen::pixelFloatToByte(px[0]);
                dst[1] = msdfgen::pixelFloatToByte(px[1]);
                dst[2] = msdfgen::pixelFloatToByte(px[2]);
                dst[3] = 255;
            }
        }

        g.record.planeLeft   = (float)(bounds.l - padEm);
        g.record.planeBottom = (float)(bounds.b - padEm);
        g.record.planeRight  = g.record.planeLeft   + (float)(w / kPixelsPerEm);
        g.record.planeTop    = g.record.planeBottom + (float)(h / kPixelsPerEm);

        glyphs.push_back(std::move(g));
    }

    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);

    if (glyphs.empty()) {
        fprintf(stderr, "dustpacker: '%s' produced no usable glyphs\n", path.string().c_str());
        return {};
    }

    // Pack tallest-first — a shelf packer's wasted space comes almost
    // entirely from short glyphs sharing a row with one much taller one, so
    // getting the tall ones settled first keeps rows tight.
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.h > b.h;
    });

    ShelfPacker packer(kAtlasWidth);
    for (auto& g : glyphs) {
        if (g.w == 0) continue; // whitespace / advance-only glyph, nothing to place
        int x, y;
        packer.place(g.w, g.h, x, y);
        g.record.atlasX = (float)x;
        g.record.atlasY = (float)y;
        g.record.atlasW = (float)g.w;
        g.record.atlasH = (float)g.h;
    }

    Fmt::Font out;
    out.atlasWidth       = kAtlasWidth;
    out.atlasHeight      = (uint32_t)std::max(1, packer.usedHeight());
    out.lineHeight        = (float)ftMetrics.lineHeight;
    out.ascender           = (float)ftMetrics.ascenderY;
    out.descender          = (float)ftMetrics.descenderY;
    out.distanceRangePx    = (float)kPxRange;
    out.atlasPixelsPerEm   = (float)kPixelsPerEm;

    out.atlasPixels.assign((size_t)out.atlasWidth * out.atlasHeight * 4, 0);
    for (auto& g : glyphs) {
        if (g.w == 0) continue;
        int x = (int)g.record.atlasX;
        int y = (int)g.record.atlasY;
        for (int row = 0; row < g.h; row++) {
            uint8_t* dst = &out.atlasPixels[((size_t)(y + row) * out.atlasWidth + x) * 4];
            const uint8_t* src = &g.pixels[(size_t)row * g.w * 4];
            memcpy(dst, src, (size_t)g.w * 4);
        }
    }

    // Restore codepoint order (packing sorted by height) so the runtime's
    // glyph lookup doesn't care either way, but a stable, predictable file
    // is easier to eyeball/diff.
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.record.codepoint < b.record.codepoint;
    });
    out.glyphs.reserve(glyphs.size());
    for (auto& g : glyphs) out.glyphs.push_back(g.record);

    printf("dustpacker:   -> %zu glyph(s), atlas %ux%u\n", out.glyphs.size(), out.atlasWidth, out.atlasHeight);
    return Fmt::serialize(out);
}

} // namespace DustPacker
