#pragma once

namespace Dust::UI {

enum class Anchor {
    TopLeft,    TopCenter,    TopRight,
    CenterLeft, Center,       CenterRight,
    BottomLeft, BottomCenter, BottomRight,
};

// Where text sits inside its widget's content box. Lives here rather than in
// Font.hpp so Widget.hpp can name it without pulling in the texture/atlas
// machinery.
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

// Pixel rect — used both for layout basis (parent content box / viewport)
// and for a widget's own computed position+size.
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

// Resolves the top-left position of a `w`x`h` box anchored to `anchor`
// within `basis` (the parent's content rect, or the viewport for a
// top-level widget), then applies offsetX/offsetY. Offset sign is uniform
// across every anchor — positive x pushes right, positive y pushes down —
// see DustUI-API.md's anchor reference map: pulling a right/bottom-anchored
// widget inward is just a negative offset, not a different formula.
inline void resolveAnchor(Anchor anchor, const Rect& basis, float w, float h,
                           float offsetX, float offsetY, float& outX, float& outY) {
    switch (anchor) {
        case Anchor::TopLeft:      outX = basis.x;                        outY = basis.y;                        break;
        case Anchor::TopCenter:    outX = basis.x + (basis.w - w) * 0.5f; outY = basis.y;                        break;
        case Anchor::TopRight:     outX = basis.x + basis.w - w;          outY = basis.y;                        break;
        case Anchor::CenterLeft:   outX = basis.x;                        outY = basis.y + (basis.h - h) * 0.5f; break;
        case Anchor::Center:       outX = basis.x + (basis.w - w) * 0.5f; outY = basis.y + (basis.h - h) * 0.5f; break;
        case Anchor::CenterRight:  outX = basis.x + basis.w - w;          outY = basis.y + (basis.h - h) * 0.5f; break;
        case Anchor::BottomLeft:   outX = basis.x;                        outY = basis.y + basis.h - h;          break;
        case Anchor::BottomCenter: outX = basis.x + (basis.w - w) * 0.5f; outY = basis.y + basis.h - h;          break;
        case Anchor::BottomRight:  outX = basis.x + basis.w - w;          outY = basis.y + basis.h - h;          break;
    }
    outX += offsetX;
    outY += offsetY;
}

} // namespace Dust::UI
