#include "Core/UI/Widget.hpp"

namespace Dust::UI {

namespace {

// isRoot toggles whether "no anchor" falls back to (parentContentRect.x/y)
// (true for the invisible root DustEngine::beginUI() hands you, and for
// Stack/None children) or to the caller-supplied flow cursor (true for
// un-anchored Row/Column children).
void layoutRecursive(Widget& w, const Rect& parentContentRect, const Rect& viewport,
                      float flowX, float flowY, bool useFlowPos) {
    float width  = w.width.resolve(parentContentRect.w, viewport.w);
    float height = w.height.resolve(parentContentRect.h, viewport.h);

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
        for (auto& child : w.children) {
            layoutRecursive(child, contentRect, viewport, cursor, contentRect.y, !child.hasAnchor);
            cursor += child.computedRect.w + gap;
        }
    } else if (w.layoutMode == LayoutMode::Column) {
        float gap    = w.gapUnit.resolve(contentRect.h, viewport.h);
        float cursor = contentRect.y;
        for (auto& child : w.children) {
            layoutRecursive(child, contentRect, viewport, contentRect.x, cursor, !child.hasAnchor);
            cursor += child.computedRect.h + gap;
        }
    } else {
        // Stack (or a plain Widget used as a container) — children overlay,
        // each anchored independently within contentRect. Per DustUI-API.md:
        // "Children in a Stack anchor relative to the Stack widget."
        for (auto& child : w.children)
            layoutRecursive(child, contentRect, viewport, contentRect.x, contentRect.y, false);
    }
}

} // namespace

void Widget::layout(Rect viewport) {
    layoutRecursive(*this, viewport, viewport, viewport.x, viewport.y, !hasAnchor);
}

} // namespace Dust::UI
