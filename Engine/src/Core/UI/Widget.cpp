#include "Core/UI/Widget.hpp"

namespace Dust::UI {

namespace {

// Implicit per-frame identity for a widget, since Widget itself is rebuilt
// from scratch every frame (immediate mode) and has no persistent handle a
// caller could hand back. Derived from tree position (parent's id + this
// widget's index among its siblings) rather than anything about the widget
// itself, so it stays stable frame-to-frame as long as the tree *shape*
// does — reordering/conditionally-inserting siblings earlier in the same
// parent will shift indices and reassign ids. Fine for the fixed-layout
// HUD-style UI this is built for today; a list that reorders itself would
// want an explicit .id() override, which doesn't exist yet (see
// UITimeline.md Phase 3).
uint64_t hashCombine(uint64_t seed, uint64_t v) {
    seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
    return seed;
}

// A px(0) size means "unset" — the widget sizes itself to its children
// (sum along a Row/Column's main axis, max on the cross axis). Without this
// an un-sized Row anchored to an edge gets a 0x0 box, so the anchor lands
// its *origin* at the edge and every child flows off-screen past it.
inline bool isAutoUnit(const Unit& u) { return u.kind == UnitKind::Px && u.value == 0.0f; }

void measureSize(Widget& w, float basisW, float basisH, const Rect& viewport,
                 float& outW, float& outH) {
    outW = w.width.resolve(basisW, viewport.w);
    outH = w.height.resolve(basisH, viewport.h);

    bool autoW = isAutoUnit(w.width);
    bool autoH = isAutoUnit(w.height);
    if ((!autoW && !autoH) || w.children.empty()) return;

    float padT = w.paddingTop.resolve(outH, viewport.h);
    float padR = w.paddingRight.resolve(outW, viewport.w);
    float padB = w.paddingBottom.resolve(outH, viewport.h);
    float padL = w.paddingLeft.resolve(outW, viewport.w);

    float innerW = autoW ? basisW : outW - padL - padR;
    float innerH = autoH ? basisH : outH - padT - padB;

    float gap = (w.layoutMode == LayoutMode::Column)
              ? w.gapUnit.resolve(innerH, viewport.h)
              : w.gapUnit.resolve(innerW, viewport.w);

    float sumW = 0.0f, sumH = 0.0f, maxW = 0.0f, maxH = 0.0f;
    for (uint32_t i = 0; i < w.children.size(); i++) {
        float cw, ch;
        measureSize(w.children[i], innerW, innerH, viewport, cw, ch);
        sumW += cw; sumH += ch;
        if (cw > maxW) maxW = cw;
        if (ch > maxH) maxH = ch;
    }
    float gaps = gap * (float)(w.children.size() - 1);

    if (autoW) outW = padL + padR + (w.layoutMode == LayoutMode::Row ? sumW + gaps : maxW);
    if (autoH) outH = padT + padB + (w.layoutMode == LayoutMode::Column ? sumH + gaps : maxH);
}

// isRoot toggles whether "no anchor" falls back to (parentContentRect.x/y)
// (true for the invisible root DustEngine::beginUI() hands you, and for
// Stack/None children) or to the caller-supplied flow cursor (true for
// un-anchored Row/Column children).
void layoutRecursive(Widget& w, const Rect& parentContentRect, const Rect& viewport,
                      float flowX, float flowY, bool useFlowPos,
                      uint64_t parentId, uint32_t indexInParent) {
    w.computedId = hashCombine(parentId, (uint64_t)indexInParent + 1);

    float width, height;
    measureSize(w, parentContentRect.w, parentContentRect.h, viewport, width, height);

    float x, y;
    if (w.hasAnchor) {
        float ox = w.anchorOffsetX.resolve(parentContentRect.w, viewport.w);
        float oy = w.anchorOffsetY.resolve(parentContentRect.h, viewport.h);
        resolveAnchor(w.anchorType, parentContentRect, width, height, ox, oy, x, y);
    } else if (useFlowPos) {
        x = flowX;
        y = flowY;
    } else {
        x = parentContentRect.x;
        y = parentContentRect.y;
    }

    w.computedRect = { x, y, width, height };
    w.computedBorderWidth  = w.borderWidth.resolve(width, viewport.w);
    w.computedBorderRadius = w.borderRadius.resolve(width < height ? width : height, viewport.w);

    float padTop    = w.paddingTop.resolve(height, viewport.h);
    float padRight  = w.paddingRight.resolve(width, viewport.w);
    float padBottom = w.paddingBottom.resolve(height, viewport.h);
    float padLeft   = w.paddingLeft.resolve(width, viewport.w);

    Rect contentRect{
        x + padLeft, y + padTop,
        width  - padLeft - padRight,
        height - padTop  - padBottom
    };
    w.computedContentRect = contentRect;

    if (w.layoutMode == LayoutMode::Row) {
        float gap    = w.gapUnit.resolve(contentRect.w, viewport.w);
        float cursor = contentRect.x;
        for (uint32_t i = 0; i < w.children.size(); i++) {
            Widget& child = w.children[i];
            layoutRecursive(child, contentRect, viewport, cursor, contentRect.y, !child.hasAnchor, w.computedId, i);
            cursor += child.computedRect.w + gap;
        }
    } else if (w.layoutMode == LayoutMode::Column) {
        float gap    = w.gapUnit.resolve(contentRect.h, viewport.h);
        float cursor = contentRect.y;
        for (uint32_t i = 0; i < w.children.size(); i++) {
            Widget& child = w.children[i];
            layoutRecursive(child, contentRect, viewport, contentRect.x, cursor, !child.hasAnchor, w.computedId, i);
            cursor += child.computedRect.h + gap;
        }
    } else {
        // Stack (or a plain Widget used as a container) — children overlay,
        // each anchored independently within contentRect. Per DustUI-API.md:
        // "Children in a Stack anchor relative to the Stack widget."
        for (uint32_t i = 0; i < w.children.size(); i++)
            layoutRecursive(w.children[i], contentRect, viewport, contentRect.x, contentRect.y, false, w.computedId, i);
    }
}

} // namespace

void Widget::layout(Rect viewport) {
    layoutRecursive(*this, viewport, viewport, viewport.x, viewport.y, !hasAnchor, 0xD057ull, 0);
}

} // namespace Dust::UI
