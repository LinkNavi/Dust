#pragma once

#include "Core/UI/Units.hpp"
#include "Core/UI/Anchor.hpp"
#include "Core/UI/Color.hpp"
#include <string>
#include <vector>
#include <functional>

namespace Dust::UI {

enum class LayoutMode { None, Row, Column, Stack };

// Value-type fluent builder — everything is a Widget, layouts (Row/Column/
// Stack) are just Widgets with layoutMode set, components are C++ functions
// that build and return one. See DustUI-API.md for the target API this
// implements, and UITimeline.md for what's built vs. still to come (no
// text rendering, images, or custom shaders yet — this is Phase 1: layout
// + solid rounded-rect/border rendering only).
struct Widget {
    // ── size ──
    Unit width  = px(0);
    Unit height = px(0);

    // ── fill ──
    bool  hasBackground = false;
    Color backgroundColor = Colors::Transparent;

    // ── border ──
    Unit  borderWidth  = px(0);
    Color borderColor  = Colors::Transparent;
    Unit  borderRadius = px(0);

    // ── padding (resolved against this widget's own content box) ──
    Unit paddingTop = px(0), paddingRight = px(0), paddingBottom = px(0), paddingLeft = px(0);

    float opacityValue = 1.0f;

    // ── text — stored, not yet rendered (Phase 2: font system) ──
    bool        hasText = false;
    std::string textContent;
    Unit        textSize = px(16);
    Color       textColor = Colors::White;

    // ── anchor — position relative to parent's content box (or viewport
    // for a top-level widget). Un-anchored Row/Column children fall back to
    // sequential flow instead; un-anchored Stack/None children default to
    // the parent's content-box origin. See UITimeline.md for why every
    // top-level widget needs one of these (no implicit self-registration).
    bool   hasAnchor     = false;
    Anchor anchorType    = Anchor::TopLeft;
    Unit   anchorOffsetX = px(0);
    Unit   anchorOffsetY = px(0);

    // ── layout ──
    LayoutMode        layoutMode = LayoutMode::None;
    Unit              gapUnit    = px(0);
    std::vector<Widget> children;

    // ── computed by layout() — valid only after it's been called ──
    Rect  computedRect;
    Rect  computedContentRect; // computedRect inset by padding — where children/text actually sit
    float computedBorderWidth  = 0.0f;
    float computedBorderRadius = 0.0f;

    // ── builder methods ──
    Widget& size(Unit w, Unit h) { width = w; height = h; return *this; }

    Widget& background(Color c) { hasBackground = true; backgroundColor = c; return *this; }

    Widget& border(Unit width_, Color color_, Unit radius_) {
        borderWidth = width_; borderColor = color_; borderRadius = radius_;
        return *this;
    }

    Widget& padding(Unit all) {
        paddingTop = paddingRight = paddingBottom = paddingLeft = all;
        return *this;
    }
    Widget& padding(Unit vertical, Unit horizontal) {
        paddingTop = paddingBottom = vertical;
        paddingLeft = paddingRight = horizontal;
        return *this;
    }
    Widget& padding(Unit top, Unit right, Unit bottom, Unit left) {
        paddingTop = top; paddingRight = right; paddingBottom = bottom; paddingLeft = left;
        return *this;
    }

    Widget& anchor(Anchor a, Unit offsetX = px(0), Unit offsetY = px(0)) {
        hasAnchor = true; anchorType = a; anchorOffsetX = offsetX; anchorOffsetY = offsetY;
        return *this;
    }

    // Stored for when Phase 2 (font system) lands — no glyphs drawn yet.
    Widget& text(const char* content, Unit size_, Color color_) {
        hasText = true; textContent = content; textSize = size_; textColor = color_;
        return *this;
    }

    Widget& opacity(float o) { opacityValue = o; return *this; }

    Widget& gap(Unit g) { gapUnit = g; return *this; }

    Widget& child(Widget w) { children.push_back(std::move(w)); return *this; }

    // Computes computedRect (+ computedBorder*) for this widget and every
    // descendant, given the viewport in pixels. Call once per frame on the
    // root widget (DustEngine::endUI() does this for you).
    void layout(Rect viewport);

    // Visits every widget in the tree (this one included) that actually
    // needs to be drawn (has a background or a visible border) — used by
    // the UI renderer to walk the tree after layout().
    void forEachVisible(const std::function<void(const Widget&)>& fn) const {
        if (hasBackground || computedBorderWidth > 0.0f)
            fn(*this);
        for (auto& c : children)
            c.forEachVisible(fn);
    }

    // Same idea, for widgets with .text() set — kept separate from
    // forEachVisible since text goes through a different draw path
    // (font/glyph batching, not a rounded-rect quad).
    void forEachText(const std::function<void(const Widget&)>& fn) const {
        if (hasText && !textContent.empty())
            fn(*this);
        for (auto& c : children)
            c.forEachText(fn);
    }
};

inline Widget Row()    { Widget w; w.layoutMode = LayoutMode::Row;    return w; }
inline Widget Column() { Widget w; w.layoutMode = LayoutMode::Column; return w; }
inline Widget Stack()  { Widget w; w.layoutMode = LayoutMode::Stack;  return w; }

} // namespace Dust::UI
