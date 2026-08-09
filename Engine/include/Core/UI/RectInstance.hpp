#pragma once

#include "Core/UI/Color.hpp"
#include <vulkan/vulkan.h>

namespace Dust::UI {

// One widget quad, ready to draw. The whole visible tree is flattened into
// an array of these once per frame and drawn with a handful of instanced
// draw calls (one per texture switch) instead of one draw call per widget —
// UITimeline.md Phase 4. Field order/packing matters: this struct IS the
// per-instance vertex layout in ui.vert (see Renderer::init's
// uiPb.instanceAttribs), so anything added here needs a matching attribute.
struct RectInstance {
    float x = 0, y = 0, w = 0, h = 0;              // loc 4 — screen-space px, top-left origin
    Color fill;                                     // loc 5
    Color border;                                   // loc 6
    // loc 7 — x: border width (px), y: corner radius (px), z: opacity,
    // w: unused (kept for vec4 alignment / future flags)
    float borderWidth = 0, borderRadius = 0, opacity = 1.0f, pad = 0;
    // loc 8 — clip rect in px (minX, minY, maxX, maxY). Fragments outside
    // it are discarded, which is how Phase 6 clipping stays inside a single
    // batched draw instead of splitting it with scissor push/pop.
    float clipMinX = 0, clipMinY = 0, clipMaxX = 0, clipMaxY = 0;
    // loc 9 — sprite sub-rect UVs. Untextured widgets sample the 1x1 white
    // fallback texture, so this is a no-op for them rather than a branch.
    float uvMinX = 0, uvMinY = 0, uvMaxX = 1, uvMaxY = 1;
    // loc 10/11 — free-form params for custom shader widgets (Phase 9).
    // Two vec4s rather than a per-shader UBO+descriptor: it costs 32 bytes on
    // every instance, but a shader widget stays in the same instance buffer
    // and the same batching path as everything else, with no per-widget
    // descriptor set to allocate or free.
    float p0 = 0, p1 = 0, p2 = 0, p3 = 0;
    float p4 = 0, p5 = 0, p6 = 0, p7 = 0;
    // loc 12 — gradient end colour. Equal to `fill` means a flat fill, which
    // is what the shader's cheap path detects.
    Color fill2;
    // loc 13 — per-corner radii in px (TL, TR, BR, BL). Widget::border()
    // sets all four; Widget::corners() varies them.
    float radTL = 0, radTR = 0, radBR = 0, radBL = 0;
    // loc 14 — x: gradient angle in radians (0 = left→right), y: shadow blur
    // px (>0 means this instance IS the shadow, drawn as a soft silhouette),
    // z/w: unused.
    float gradAngle = 0, shadowBlur = 0, borderGradAngle = 0, unused1 = 0;
    // loc 15 — border gradient end colour, same "equal means flat" trick as
    // fill2. Borders get their own angle (in `borderGradAngle` above) since a
    // ring reads better with a different sweep than the fill behind it.
    Color border2;

    // Batch key. Neither is a vertex attribute — the batcher groups
    // consecutive instances by the pair and rebinds between runs.
    VkDescriptorSet texSet   = VK_NULL_HANDLE;
    VkPipeline      pipeline = VK_NULL_HANDLE; // null = the built-in UI pipeline

    bool operator==(const RectInstance& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h &&
               fill.r == o.fill.r && fill.g == o.fill.g && fill.b == o.fill.b && fill.a == o.fill.a &&
               border.r == o.border.r && border.g == o.border.g && border.b == o.border.b && border.a == o.border.a &&
               borderWidth == o.borderWidth && borderRadius == o.borderRadius && opacity == o.opacity &&
               clipMinX == o.clipMinX && clipMinY == o.clipMinY &&
               clipMaxX == o.clipMaxX && clipMaxY == o.clipMaxY &&
               uvMinX == o.uvMinX && uvMinY == o.uvMinY &&
               uvMaxX == o.uvMaxX && uvMaxY == o.uvMaxY &&
               p0 == o.p0 && p1 == o.p1 && p2 == o.p2 && p3 == o.p3 &&
               p4 == o.p4 && p5 == o.p5 && p6 == o.p6 && p7 == o.p7 &&
               fill2.r == o.fill2.r && fill2.g == o.fill2.g && fill2.b == o.fill2.b && fill2.a == o.fill2.a &&
               radTL == o.radTL && radTR == o.radTR && radBR == o.radBR && radBL == o.radBL &&
               gradAngle == o.gradAngle && shadowBlur == o.shadowBlur &&
               borderGradAngle == o.borderGradAngle &&
               border2.r == o.border2.r && border2.g == o.border2.g &&
               border2.b == o.border2.b && border2.a == o.border2.a &&
               texSet == o.texSet && pipeline == o.pipeline;
    }
};

} // namespace Dust::UI
