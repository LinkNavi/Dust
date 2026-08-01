#pragma once

// Lives directly in Dust:: (not Dust::UI::) — matches DustUI-API.md's
// `Dust::px(5)` etc. Shared vocabulary, not specific to the widget tree.
namespace Dust {

enum class UnitKind { Px, Pct, Vw, Vh };

struct Unit {
    UnitKind kind  = UnitKind::Px;
    float    value = 0.0f;

    // parentDim/viewportDim must be the dimension matching the axis this
    // Unit applies to (width when resolving a width, height when resolving
    // a height) — Vw/Vh both just multiply viewportDim; the caller picking
    // the right one is what makes them mean "width" vs "height".
    float resolve(float parentDim, float viewportDim) const {
        switch (kind) {
            case UnitKind::Px:  return value;
            case UnitKind::Pct: return value * parentDim;
            case UnitKind::Vw:
            case UnitKind::Vh:  return value * viewportDim;
        }
        return 0.0f;
    }
};

inline Unit px(float v)  { return { UnitKind::Px,  v }; }
inline Unit pct(float v) { return { UnitKind::Pct, v }; }
inline Unit vw(float v)  { return { UnitKind::Vw,  v }; }
inline Unit vh(float v)  { return { UnitKind::Vh,  v }; }

} // namespace Dust
