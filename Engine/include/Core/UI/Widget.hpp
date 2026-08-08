#pragma once

#include "Core/UI/Units.hpp"
#include "Core/UI/Anchor.hpp"
#include "Core/UI/Color.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace Dust::UI {

enum class LayoutMode { None, Row, Column, Stack };

// Value-type fluent builder — everything is a Widget, layouts (Row/Column/
// Stack) are just Widgets with layoutMode set, components are C++ functions
// that build and return one. See DustUI-API.md for the target API this
// implements, and UITimeline.md for what's built vs. still to come (no
// sprites/custom shaders yet — layout, solid rounded-rect/border fills,
// MSDF text, and click/hover/focus input are all done).
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

    // ── input (UITimeline.md Phase 3) — level-triggered: onHover/onFocus
    // fire every frame the condition holds (not just on the transition),
    // since the widget triggering them is rebuilt fresh each frame anyway
    // and there's no stale old-widget to diff against. onClick is the one
    // genuinely edge-triggered case (a full press+release over the same
    // widget) — see DustEngine::endUI() for the dispatch logic. Setting any
    // of these is what makes a widget hit-testable at all; a plain
    // decorative Widget with none of them set is invisible to input.
    std::function<void()> onClickFn;
    std::function<void()> onHoverFn;
    std::function<void()> onFocusFn; // only onFocusFn also makes a widget *focusable* — see .onFocus()

    // ── computed — valid only after layout() has run ──
    Rect     computedRect;
    Rect     computedContentRect; // computedRect inset by padding — where children/text actually sit
    float    computedBorderWidth  = 0.0f;
    float    computedBorderRadius = 0.0f;
    uint64_t computedId = 0; // implicit path-hash identity — see Widget.cpp's layoutRecursive

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

    Widget& text(const char* content, Unit size_, Color color_) {
        hasText = true; textContent = content; textSize = size_; textColor = color_;
        return *this;
    }

    Widget& opacity(float o) { opacityValue = o; return *this; }

    Widget& gap(Unit g) { gapUnit = g; return *this; }

    Widget& child(Widget w) { children.push_back(std::move(w)); return *this; }

    // ── input ── setting any of these makes the widget hit-testable.
    Widget& onClick(std::function<void()> fn) { onClickFn = std::move(fn); return *this; }
    Widget& onHover(std::function<void()> fn) { onHoverFn = std::move(fn); return *this; }
    // Also makes the widget focusable — clicking it makes it the keyboard-
    // capture target (DustEngine::uiWantsKeyboard()) until something else is
    // clicked. A widget with only .onClick()/.onHover() never takes focus.
    Widget& onFocus(std::function<void()> fn) { onFocusFn = std::move(fn); return *this; }

    bool isInteractive() const { return (bool)onClickFn || (bool)onHoverFn || (bool)onFocusFn; }
    bool isFocusable()   const { return (bool)onFocusFn; }

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

    // Widgets with .onClick()/.onHover()/.onFocus() set — what
    // DustEngine::endUI() hit-tests against and dispatches callbacks
    // through. Visited in the same parent-then-children order as
    // forEachVisible/forEachText, so "last match under the cursor" during
    // hit-testing naturally means "topmost" (children draw over parents).
    void forEachInteractive(const std::function<void(const Widget&)>& fn) const {
        if (isInteractive())
            fn(*this);
        for (auto& c : children)
            c.forEachInteractive(fn);
    }
};

inline Widget Row()    { Widget w; w.layoutMode = LayoutMode::Row;    return w; }
inline Widget Column() { Widget w; w.layoutMode = LayoutMode::Column; return w; }
inline Widget Stack()  { Widget w; w.layoutMode = LayoutMode::Stack;  return w; }

} // namespace Dust::UI
