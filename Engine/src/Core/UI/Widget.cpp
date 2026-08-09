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

// min/max clamps. A max of px(0) means "unbounded" — a zero-size maximum
// isn't a thing anyone wants, so it's free to use as the sentinel.
void applyConstraints(const Widget& w, float basisW, float basisH, const Rect& viewport,
                      float& outW, float& outH) {
    float minW = w.minWidth.resolve(basisW, viewport.w);
    float minH = w.minHeight.resolve(basisH, viewport.h);
    float maxW = w.maxWidth.resolve(basisW, viewport.w);
    float maxH = w.maxHeight.resolve(basisH, viewport.h);
    if (outW < minW) outW = minW;
    if (outH < minH) outH = minH;
    if (maxW > 0.0f && outW > maxW) outW = maxW;
    if (maxH > 0.0f && outH > maxH) outH = maxH;
}

// Outer size — what a Row/Column actually has to make room for.
void marginsOf(const Widget& w, float basisW, float basisH, const Rect& viewport,
               float& top, float& right, float& bottom, float& left) {
    top    = w.marginTop.resolve(basisH, viewport.h);
    right  = w.marginRight.resolve(basisW, viewport.w);
    bottom = w.marginBottom.resolve(basisH, viewport.h);
    left   = w.marginLeft.resolve(basisW, viewport.w);
}

void measureSize(Widget& w, float basisW, float basisH, const Rect& viewport,
                 float& outW, float& outH) {
    outW = w.width.resolve(basisW, viewport.w);
    outH = w.height.resolve(basisH, viewport.h);

    bool autoW = isAutoUnit(w.width);
    bool autoH = isAutoUnit(w.height);
    if ((!autoW && !autoH) || w.children.empty()) {
        applyConstraints(w, basisW, basisH, viewport, outW, outH);
        return;
    }

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
        // Children are measured at their *outer* size — margins take up room
        // in the parent exactly like the widget itself does.
        float mt, mr, mb, ml;
        marginsOf(w.children[i], innerW, innerH, viewport, mt, mr, mb, ml);
        cw += ml + mr; ch += mt + mb;
        sumW += cw; sumH += ch;
        if (cw > maxW) maxW = cw;
        if (ch > maxH) maxH = ch;
    }
    float gaps = gap * (float)(w.children.size() - 1);

    if (autoW) outW = padL + padR + (w.layoutMode == LayoutMode::Row ? sumW + gaps : maxW);
    if (autoH) outH = padT + padB + (w.layoutMode == LayoutMode::Column ? sumH + gaps : maxH);
    applyConstraints(w, basisW, basisH, viewport, outW, outH);
}

// Leading offset and inter-child spacing for a Justify mode, given how much
// room is left over on the main axis after the children and gaps.
void justifyMetrics(Justify j, float leftover, uint32_t count, float& outLead, float& outExtraGap) {
    outLead = 0.0f;
    outExtraGap = 0.0f;
    if (leftover <= 0.0f || count == 0) return;
    switch (j) {
        case Justify::Start:  break;
        case Justify::Center: outLead = leftover * 0.5f; break;
        case Justify::End:    outLead = leftover; break;
        case Justify::SpaceBetween:
            if (count > 1) outExtraGap = leftover / (float)(count - 1);
            else           outLead = leftover * 0.5f; // one child: nothing to space between, so centre it
            break;
        case Justify::SpaceAround:
            outExtraGap = leftover / (float)count;
            outLead     = outExtraGap * 0.5f;
            break;
    }
}

// Cross-axis offset for one child, and (for Stretch) the size it takes.
float alignOffset(AlignItems a, float containerCross, float childCross) {
    switch (a) {
        case AlignItems::Center: return (containerCross - childCross) * 0.5f;
        case AlignItems::End:    return containerCross - childCross;
        default:                 return 0.0f;
    }
}

Rect intersect(const Rect& a, const Rect& b) {
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return { x0, y0, x1 > x0 ? x1 - x0 : 0.0f, y1 > y0 ? y1 - y0 : 0.0f };
}

// useFlowPos toggles whether "no anchor" falls back to (parentContentRect.x/y)
// (true for the invisible root DustEngine::beginUI() hands you, and for
// Stack/None children) or to the caller-supplied flow cursor (true for
// un-anchored Row/Column children). clipRect is the region this widget is
// allowed to draw in — the viewport at the root, narrowed by every
// .clip()ing ancestor on the way down.
void layoutRecursive(Widget& w, const Rect& parentContentRect, const Rect& viewport,
                      float flowX, float flowY, bool useFlowPos,
                      uint64_t parentId, uint32_t indexInParent,
                      const Rect& clipRect, uint64_t parentDepthKey) {
    // Depth key: layer in the high half, z in the low half (biased so
    // negative z still orders correctly as unsigned). Inherited as a max so a
    // child can be lifted above its parent but never sink below it.
    uint64_t ownDepthKey = ((uint64_t)(uint32_t)w.layer << 32) |
                           (uint32_t)((int64_t)w.zIndex + 0x80000000LL);
    w.computedDepthKey = ownDepthKey > parentDepthKey ? ownDepthKey : parentDepthKey;

    // Explicit id wins when set — the whole point is that it survives
    // reordering, which the path hash by construction cannot.
    w.computedId = w.explicitId != 0 ? w.explicitId
                                     : hashCombine(parentId, (uint64_t)indexInParent + 1);

    float width, height;
    measureSize(w, parentContentRect.w, parentContentRect.h, viewport, width, height);

    float mTop, mRight, mBottom, mLeft;
    marginsOf(w, parentContentRect.w, parentContentRect.h, viewport, mTop, mRight, mBottom, mLeft);

    float x, y;
    if (w.hasAnchor) {
        float ox = w.anchorOffsetX.resolve(parentContentRect.w, viewport.w);
        float oy = w.anchorOffsetY.resolve(parentContentRect.h, viewport.h);
        // Anchor against the box the margins leave behind, so a margin on an
        // anchored widget reads as "keep this far off the edge".
        Rect inset{ parentContentRect.x + mLeft, parentContentRect.y + mTop,
                    parentContentRect.w - mLeft - mRight, parentContentRect.h - mTop - mBottom };
        resolveAnchor(w.anchorType, inset, width, height, ox, oy, x, y);
    } else if (useFlowPos) {
        // In flow, the cursor points at the widget's outer box; the margin
        // is the gap between that and the widget proper.
        x = flowX + mLeft;
        y = flowY + mTop;
    } else {
        x = parentContentRect.x + mLeft;
        y = parentContentRect.y + mTop;
    }

    w.computedRect = { x, y, width, height };
    w.computedBorderWidth  = w.borderWidth.resolve(width, viewport.w);
    w.computedBorderRadius = w.borderRadius.resolve(width < height ? width : height, viewport.w);
    float shortSide = width < height ? width : height;
    if (w.hasCornerRadii) {
        w.computedRadii[0] = w.radiusTL.resolve(shortSide, viewport.w);
        w.computedRadii[1] = w.radiusTR.resolve(shortSide, viewport.w);
        w.computedRadii[2] = w.radiusBR.resolve(shortSide, viewport.w);
        w.computedRadii[3] = w.radiusBL.resolve(shortSide, viewport.w);
    } else {
        w.computedRadii[0] = w.computedRadii[1] = w.computedRadii[2] = w.computedRadii[3] = w.computedBorderRadius;
    }

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
    w.computedClipRect    = clipRect;

    // A clipping widget narrows the region its descendants may draw in to
    // its own content box; anything below inherits the intersection.
    Rect childClip = (w.clipsChildren || w.scrollable) ? intersect(clipRect, contentRect) : clipRect;

    // Scrolling just biases where the flow starts; everything downstream
    // (clipping, hit-testing) then falls out for free.
    float scroll = w.scrollable ? w.scrollOffset : 0.0f;

    if (w.layoutMode == LayoutMode::Row || w.layoutMode == LayoutMode::Column) {
        bool  row = w.layoutMode == LayoutMode::Row;
        float gap = row ? w.gapUnit.resolve(contentRect.w, viewport.w)
                        : w.gapUnit.resolve(contentRect.h, viewport.h);

        // Measure pass — justify needs the total up front, and the sizes are
        // cheap to compute twice compared to laying the subtree out twice.
        float total = 0.0f;
        for (uint32_t i = 0; i < w.children.size(); i++) {
            float cw, ch, mt, mr, mb, ml;
            measureSize(w.children[i], contentRect.w, contentRect.h, viewport, cw, ch);
            marginsOf(w.children[i], contentRect.w, contentRect.h, viewport, mt, mr, mb, ml);
            total += row ? (cw + ml + mr) : (ch + mt + mb);
        }
        if (!w.children.empty()) total += gap * (float)(w.children.size() - 1);

        float axisLen  = row ? contentRect.w : contentRect.h;
        float lead = 0.0f, extraGap = 0.0f;
        justifyMetrics(w.justify, axisLen - total, (uint32_t)w.children.size(), lead, extraGap);

        float start  = (row ? contentRect.x : contentRect.y) - scroll + lead;
        float cursor = start;
        float crossLen = row ? contentRect.h : contentRect.w;

        for (uint32_t i = 0; i < w.children.size(); i++) {
            Widget& child = w.children[i];

            // Cross-axis alignment has to know the child's size before it's
            // positioned, hence the extra measure. Stretch instead overwrites
            // the child's cross size and lets layout do the rest.
            float cw, ch, mt, mr, mb, ml;
            measureSize(child, contentRect.w, contentRect.h, viewport, cw, ch);
            marginsOf(child, contentRect.w, contentRect.h, viewport, mt, mr, mb, ml);

            if (w.alignItems == AlignItems::Stretch && !child.hasAnchor) {
                if (row) child.height = px(crossLen - mt - mb);
                else     child.width  = px(crossLen - ml - mr);
                measureSize(child, contentRect.w, contentRect.h, viewport, cw, ch);
            }

            float crossOff = child.hasAnchor ? 0.0f
                           : alignOffset(w.alignItems, crossLen, row ? (ch + mt + mb) : (cw + ml + mr));

            float flowMain  = cursor;
            float flowCross = (row ? contentRect.y : contentRect.x) + crossOff;

            layoutRecursive(child, contentRect, viewport,
                            row ? flowMain : flowCross,
                            row ? flowCross : flowMain,
                            !child.hasAnchor, w.computedId, i, childClip, w.computedDepthKey);

            cursor += (row ? (child.computedRect.w + ml + mr) : (child.computedRect.h + mt + mb)) + gap + extraGap;
        }
        w.computedContentExtent = w.children.empty() ? 0.0f : (cursor - start - gap - extraGap);
    } else {
        // Stack (or a plain Widget used as a container) — children overlay,
        // each anchored independently within contentRect. Per DustUI-API.md:
        // "Children in a Stack anchor relative to the Stack widget."
        // Scrolling still applies here: a scroll container is usually a plain
        // widget wrapping one tall Column, so it's the *wrapper* that gets
        // .scroll() and it has no layout axis of its own. Vertical only —
        // there's no meaningful main axis to pick otherwise.
        Rect childBasis = contentRect;
        childBasis.y -= scroll;
        float maxBottom = childBasis.y;
        for (uint32_t i = 0; i < w.children.size(); i++) {
            Widget& child = w.children[i];
            layoutRecursive(child, childBasis, viewport, childBasis.x, childBasis.y, false, w.computedId, i, childClip, w.computedDepthKey);
            float bottom = child.computedRect.y + child.computedRect.h;
            if (bottom > maxBottom) maxBottom = bottom;
        }
        w.computedContentExtent = maxBottom - childBasis.y;
    }
}

} // namespace

void Widget::layout(Rect viewport) {
    layoutRecursive(*this, viewport, viewport, viewport.x, viewport.y, !hasAnchor, 0xD057ull, 0, viewport, 0);
}

} // namespace Dust::UI
