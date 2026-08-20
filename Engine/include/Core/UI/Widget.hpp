#pragma once

#include "Core/UI/Units.hpp"
#include "Core/UI/Anchor.hpp"
#include "Core/UI/Color.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Dust::UI {

enum class LayoutMode { None, Row, Column, Stack };

// How a Row/Column distributes leftover space along its own axis, and how it
// places children across the other one. Only meaningful on a container with
// an explicit size — an auto-sized one has no leftover space by definition.
enum class Justify { Start, Center, End, SpaceBetween, SpaceAround };
enum class AlignItems { Start, Center, End, Stretch };

// Coarse draw-order buckets, sorted before zIndex within each bucket
// (UITimeline.md Phase 10). Background is for anything that should sit under
// normal HUD chrome (a vignette, a backdrop dim), Overlay for things that
// must win regardless of what the HUD does (modals, tooltips, debug
// readouts). World/Hand sit between Background and HUD — game-world
// markers (nametags, waypoint labels) and view-locked gadgets (a crosshair)
// should read as "in the scene", never on top of real HUD chrome or a
// modal's Overlay scrim. See Widget::worldPos/.world()/.hand() below —
// endUI() projects them through the active beginMode3D() camera and treats
// the rest as an ordinary anchored screen rect (DustEngine::endUI()).
enum class Layer { Background = 0, World = 1, Hand = 2, HUD = 3, Overlay = 4 };

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

    // ── size constraints ── applied after width/height resolve, before
    // anchoring. px(0) on a max means "no maximum" (a zero-width widget is
    // meaningless, so the sentinel costs nothing).
    Unit minWidth = px(0), minHeight = px(0);
    Unit maxWidth = px(0), maxHeight = px(0);

    // ── margin ── space *outside* the widget: it offsets the widget within
    // its slot and, in a Row/Column, pushes the next sibling along. Padding
    // is the inside equivalent.
    Unit marginTop = px(0), marginRight = px(0), marginBottom = px(0), marginLeft = px(0);

    // ── fill ──
    bool  hasBackground = false;
    Color backgroundColor = Colors::Transparent;
    // Gradient end colour. Unset means it equals backgroundColor, which the
    // shader mixes to a no-op — a flat fill needs no separate path.
    bool  hasGradient   = false;
    Color gradientColor = Colors::Transparent;
    float gradientAngle = 0.0f; // radians, 0 = left→right

    // ── drop shadow ── emitted as an extra instance behind this widget:
    // same silhouette, offset and faded over `shadowBlur` px.
    bool  hasShadow    = false;
    Color shadowColor  = Colors::Transparent;
    float shadowBlurPx = 0.0f;
    float shadowOffX   = 0.0f, shadowOffY = 0.0f;

    // ── border ──
    Unit  borderWidth  = px(0);
    Color borderColor  = Colors::Transparent;
    Unit  borderRadius = px(0);
    // Per-corner overrides (TL, TR, BR, BL). Unset (all zero) falls back to
    // borderRadius for every corner — see .corners().
    // Border gradient — same idea as the fill's, with its own angle.
    bool  hasBorderGradient = false;
    Color borderGradientColor = Colors::Transparent;
    float borderGradientAngle = 0.0f;
    bool  hasCornerRadii = false;
    Unit  radiusTL = px(0), radiusTR = px(0), radiusBR = px(0), radiusBL = px(0);

    // ── padding (resolved against this widget's own content box) ──
    Unit paddingTop = px(0), paddingRight = px(0), paddingBottom = px(0), paddingLeft = px(0);

    float opacityValue = 1.0f;

    // ── sprite (UITimeline.md Phase 5) ── a texture drawn inside this
    // widget's rect, multiplied by backgroundColor (so it doubles as a tint;
    // no .background() means white = untinted). Takes the descriptor set
    // rather than a Texture& because that's what actually gets bound, and
    // DustEngine::createTextureSet() already hands you one — same handle a
    // Model material or the particle system uses.
    VkDescriptorSet spriteSet = VK_NULL_HANDLE;
    float spriteUVMinX = 0.0f, spriteUVMinY = 0.0f, spriteUVMaxX = 1.0f, spriteUVMaxY = 1.0f;

    // ── input capture ── a widget that blocks input swallows everything
    // drawn *below* it: hit-testing ignores any widget earlier in draw order,
    // so a modal doesn't need its callers to disable the UI behind it. Its
    // own descendants come later in the walk, so they stay live.
    bool blocksInput = false;

    // ── clipping (UITimeline.md Phase 6) ── when set, descendants are
    // clipped to this widget's *content* box (its rect inset by padding).
    // Implemented as a per-instance clip rect the fragment shaders discard
    // against, not a scissor push/pop, so clipping never splits a batch.
    bool clipsChildren = false;

    // ── scrolling (UITimeline.md Phase 7) ── a scrollable Row/Column shifts
    // its children's flow start by scrollOffset px and clips them. The
    // offset itself lives on DustEngine (keyed by this widget's implicit id)
    // rather than here, since the tree is rebuilt every frame — endUI()
    // writes last frame's value in before layout and updates it from the
    // wheel afterwards.
    bool  scrollable   = false;
    float scrollOffset = 0.0f;

    // ── custom shader (UITimeline.md Phase 9) ── replaces ui.frag for this
    // widget only. The handle comes from DustEngine::loadUIShader(); params
    // are eight free floats delivered as two vec4 instance attributes, which
    // is why a shader widget still batches alongside everything else instead
    // of needing its own descriptor set.
    VkPipeline shaderPipeline = VK_NULL_HANDLE;
    float shaderParams[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    // ── text — stored, not yet rendered (Phase 2: font system) ──
    bool        hasText = false;
    std::string textContent;
    Unit        textSize = px(16);
    Color       textColor = Colors::White;
    HAlign      textHAlign = HAlign::Left;
    VAlign      textVAlign = VAlign::Middle;
    // Wrap to the content box width. Off by default: wrapping a HUD label
    // that happens to be a few px too narrow is worse than letting it run.
    bool        textWrap = false;
    Unit        textOutlineWidth = px(0);
    Color       textOutlineColor = Colors::Black;
    // Text gradient runs across the whole text block, not per glyph.
    bool        hasTextGradient = false;
    Color       textGradientColor = Colors::Transparent;
    float       textGradientAngle = 0.0f;

    // ── anchor — position relative to parent's content box (or viewport
    // for a top-level widget). Un-anchored Row/Column children fall back to
    // sequential flow instead; un-anchored Stack/None children default to
    // the parent's content-box origin. See UITimeline.md for why every
    // top-level widget needs one of these (no implicit self-registration).
    bool   hasAnchor     = false;
    Anchor anchorType    = Anchor::TopLeft;
    Unit   anchorOffsetX = px(0);
    Unit   anchorOffsetY = px(0);

    // ── draw order (UITimeline.md Phases 8 + 10) ──
    // (layer, zIndex) become a sort key on the *flattened draw list*, not on
    // the widget tree: layout runs first and untouched, then every quad and
    // glyph the tree produced is stably sorted by the key it inherited. Three
    // things fall out of doing it that way rather than by reordering
    // siblings: layout and implicit ids are unaffected by z, a Row's flow
    // order stays the order you wrote it in, and — because text is sorted
    // into the same list as the rects — a modal's scrim covers the text
    // behind it, not just the panels.
    //
    // A child never draws below its parent's bucket: the key is inherited as
    // max(parent, own), so putting a container on Layer::Overlay lifts its
    // whole subtree.
    Layer layer  = Layer::HUD;
    int   zIndex = 0;

    // ── World/Hand layer anchor (UITimeline.md Phase 10) ── set by
    // .world()/.hand(); read once by DustEngine::endUI() before layout()
    // runs, which projects worldPos (or the camera-relative offset, for
    // Hand) through the active beginMode3D() camera and turns the result
    // into an ordinary Anchor::Center + pixel offset, so everything past
    // that point (size/hover/click/draw) is the same screen-rect code every
    // other widget uses. MVP scope: only top-level ui.child(...) widgets are
    // supported (anchor math assumes the viewport is the parent content
    // rect — see endUI()), and there's no raycast hit-test or 3D occlusion
    // against scene geometry, only back-to-front distance sort among World
    // widgets themselves.
    glm::vec3 worldPos       = { 0.0f, 0.0f, 0.0f }; // Layer::World: absolute world position
    glm::vec3 handOffset     = { 0.0f, 0.0f, 0.0f }; // Layer::Hand: (forward, right, up) offsets from camera
    bool      offScreen      = false; // computed by endUI() — behind the camera, don't draw/hit-test

    // ── layout ──
    LayoutMode        layoutMode = LayoutMode::None;
    Justify           justify    = Justify::Start;
    AlignItems        alignItems = AlignItems::Start;
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
    // Fires every frame the mouse is held after pressing this widget, with
    // the frame's mouse delta in px — drag handles, sliders, resize grips.
    // Keeps firing while held even if the cursor leaves the widget, which is
    // what makes dragging survive a fast mouse.
    std::function<void(float dx, float dy)> onDragFn;
    std::function<void()> onPressFn;   // mouse went down over this widget
    std::function<void()> onReleaseFn; // mouse came up over it, click or not
    std::function<void()> onClickFn;
    std::function<void()> onHoverFn;
    std::function<void()> onFocusFn; // only onFocusFn also makes a widget *focusable* — see .onFocus()

    // ── computed — valid only after layout() has run ──
    Rect     computedRect;
    Rect     computedContentRect; // computedRect inset by padding — where children/text actually sit
    Rect     computedClipRect;    // px region this widget is allowed to draw in (intersection of every clipping ancestor)
    // Total px length of this widget's children along its layout axis
    // (Row = width, Column = height), gaps included. What scrolling clamps
    // against: anything past computedContentRect's matching dimension is
    // the overflow.
    float    computedContentExtent = 0.0f;
    float    computedBorderWidth  = 0.0f;
    float    computedBorderRadius = 0.0f;
    float    computedRadii[4] = { 0, 0, 0, 0 }; // TL, TR, BR, BL — equal to computedBorderRadius unless .corners() was used
    // Packed (layer, zIndex) inherited down the tree — what the flattened
    // draw list is sorted by. See the `layer` field above.
    uint64_t computedDepthKey = 0;
    uint64_t computedId = 0; // implicit path-hash identity — see Widget.cpp's layoutRecursive
    // Explicit identity override. The implicit id is derived from position
    // in the tree, so a list that reorders or conditionally inserts items
    // shifts every id after the change — hover/focus/scroll then jump to the
    // wrong widget. Setting this pins identity to something stable instead
    // (UITimeline.md Phase 3 flagged the gap; this closes it).
    uint64_t explicitId = 0;

    // ── builder methods ──
    Widget& size(Unit w, Unit h) { width = w; height = h; return *this; }

    Widget& background(Color c) { hasBackground = true; backgroundColor = c; return *this; }

    // Linear gradient between two colours. angleRad 0 = left→right, pi/2 =
    // top→bottom. Costs nothing extra to draw — a flat fill is the same
    // shader path with both stops equal.
    // Border gradient. Independent angle from the fill's — a ring usually
    // reads better sweeping a different way than the surface behind it.
    Widget& borderGradient(Color from, Color to, float angleRad = 0.0f) {
        borderColor = from;
        hasBorderGradient = true; borderGradientColor = to; borderGradientAngle = angleRad;
        return *this;
    }

    // Gradient across the whole text block (continuous across glyphs and
    // wrapped lines, not restarting per letter).
    Widget& textGradient(Color from, Color to, float angleRad = 0.0f) {
        textColor = from;
        hasTextGradient = true; textGradientColor = to; textGradientAngle = angleRad;
        return *this;
    }

    Widget& gradient(Color from, Color to, float angleRad = 0.0f) {
        hasBackground = true; backgroundColor = from;
        hasGradient   = true; gradientColor   = to; gradientAngle = angleRad;
        return *this;
    }

    // Drop shadow, drawn as one extra instance behind this widget — same
    // silhouette (radii included), offset by dx/dy and faded over blurPx.
    Widget& shadow(Color c, float blurPx, float dx = 0.0f, float dy = 2.0f) {
        hasShadow = true; shadowColor = c; shadowBlurPx = blurPx;
        shadowOffX = dx; shadowOffY = dy;
        return *this;
    }

    // Size clamps, applied after width/height resolve. Handy for text that
    // varies at runtime inside an auto-sized container.
    Widget& minSize(Unit w, Unit h) { minWidth = w; minHeight = h; return *this; }
    Widget& maxSize(Unit w, Unit h) { maxWidth = w; maxHeight = h; return *this; }

    Widget& margin(Unit all) {
        marginTop = marginRight = marginBottom = marginLeft = all;
        return *this;
    }
    Widget& margin(Unit vertical, Unit horizontal) {
        marginTop = marginBottom = vertical;
        marginLeft = marginRight = horizontal;
        return *this;
    }
    Widget& margin(Unit top, Unit right, Unit bottom, Unit left) {
        marginTop = top; marginRight = right; marginBottom = bottom; marginLeft = left;
        return *this;
    }

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

    Widget& align(HAlign h, VAlign v = VAlign::Middle) { textHAlign = h; textVAlign = v; return *this; }

    // Wrap at word boundaries to the content box width. A word wider than
    // the box overflows rather than being split mid-word.
    Widget& wrap(bool enabled = true) { textWrap = enabled; return *this; }

    // Per-glyph outline — makes text legible over arbitrary game content
    // without a backing panel.
    Widget& textOutline(Unit width_, Color color_) {
        textOutlineWidth = width_; textOutlineColor = color_;
        return *this;
    }

    // Per-corner radii (TL, TR, BR, BL). .border()'s single radius applies
    // to all four; this overrides it — pill shapes, tabs, notched panels.
    Widget& corners(Unit tl, Unit tr, Unit br, Unit bl) {
        hasCornerRadii = true;
        radiusTL = tl; radiusTR = tr; radiusBR = br; radiusBL = bl;
        return *this;
    }

    Widget& opacity(float o) { opacityValue = o; return *this; }

    // texSet comes from DustEngine::createTextureSet(). u0/v0/u1/v1 pick a
    // sub-rect of the texture — the whole thing by default, an atlas cell
    // otherwise.
    Widget& sprite(VkDescriptorSet texSet,
                   float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f) {
        spriteSet = texSet;
        spriteUVMinX = u0; spriteUVMinY = v0; spriteUVMaxX = u1; spriteUVMaxY = v1;
        return *this;
    }

    Widget& clip(bool enabled = true) { clipsChildren = enabled; return *this; }

    // Everything drawn before this widget stops being clickable — the
    // primitive a modal dialog is built from.
    Widget& blockInput(bool enabled = true) { blocksInput = enabled; return *this; }

    // Scrolling implies clipping — an unclipped scroll container would just
    // draw its overflow on top of whatever is next to it. Give the container
    // an explicit .size(); a px(0) auto-size grows to fit its children, and
    // something that always fits never has anything to scroll.
    Widget& scroll(bool enabled = true) { scrollable = enabled; clipsChildren = enabled || clipsChildren; return *this; }

    Widget& shader(VkPipeline pipeline,
                   float p0 = 0, float p1 = 0, float p2 = 0, float p3 = 0,
                   float p4 = 0, float p5 = 0, float p6 = 0, float p7 = 0) {
        shaderPipeline = pipeline;
        shaderParams[0] = p0; shaderParams[1] = p1; shaderParams[2] = p2; shaderParams[3] = p3;
        shaderParams[4] = p4; shaderParams[5] = p5; shaderParams[6] = p6; shaderParams[7] = p7;
        return *this;
    }

    Widget& gap(Unit g) { gapUnit = g; return *this; }

    // Main-axis distribution and cross-axis placement for a Row/Column.
    // Both need the container to have an explicit size — an auto-sized one
    // is exactly as big as its children, so there's nothing to distribute.
    Widget& justifyContent(Justify j) { justify = j; return *this; }
    Widget& align(AlignItems a)       { alignItems = a; return *this; }

    // Pins this widget's identity instead of deriving it from tree position.
    // Needed for anything that reorders or conditionally inserts siblings —
    // otherwise hover/focus/scroll state follows the *slot*, not the widget.
    Widget& id(uint64_t stableId) { explicitId = stableId; return *this; }

    Widget& z(int index)      { zIndex = index; return *this; }
    Widget& setLayer(Layer l) { layer  = l;     return *this; }

    // Anchors this widget to a fixed point in the 3D scene (a nametag over
    // an NPC, a waypoint marker). Top-level ui.child(...) only — see the
    // MVP-scope comment on `layer` above.
    Widget& world(glm::vec3 pos) { layer = Layer::World; worldPos = pos; return *this; }

    // View-locked: recomputed every frame as camera.position +
    // camera.forward()*fwd + camera.right()*right + camera.up()*up — a
    // crosshair or held-item icon that rides along with the camera instead
    // of sitting still in the world.
    Widget& hand(float fwd, float right, float up = 0.0f) {
        layer = Layer::Hand; handOffset = { fwd, right, up }; return *this;
    }


    Widget& child(Widget w) { children.push_back(std::move(w)); return *this; }

    // ── input ── setting any of these makes the widget hit-testable.
    Widget& onClick(std::function<void()> fn) { onClickFn = std::move(fn); return *this; }
    // Edge-triggered halves of a click, for drag starts, key-repeat-style
    // held buttons, or press feedback that shouldn't wait for release.
    Widget& onPress(std::function<void()> fn)   { onPressFn = std::move(fn); return *this; }
    Widget& onRelease(std::function<void()> fn) { onReleaseFn = std::move(fn); return *this; }
    Widget& onDrag(std::function<void(float dx, float dy)> fn) { onDragFn = std::move(fn); return *this; }
    Widget& onHover(std::function<void()> fn) { onHoverFn = std::move(fn); return *this; }
    // Also makes the widget focusable — clicking it makes it the keyboard-
    // capture target (DustEngine::uiWantsKeyboard()) until something else is
    // clicked. A widget with only .onClick()/.onHover() never takes focus.
    Widget& onFocus(std::function<void()> fn) { onFocusFn = std::move(fn); return *this; }

    // A scroll container counts as interactive so the wheel over it belongs
    // to the UI (uiWantsMouse()) instead of the game camera.
    bool isInteractive() const {
        return (bool)onClickFn || (bool)onHoverFn || (bool)onFocusFn ||
               (bool)onPressFn || (bool)onReleaseFn || (bool)onDragFn ||
               scrollable || blocksInput;
    }
    bool isFocusable()   const { return (bool)onFocusFn; }

    // Computes computedRect (+ computedBorder*) for this widget and every
    // descendant, given the viewport in pixels. Call once per frame on the
    // root widget (DustEngine::endUI() does this for you).
    void layout(Rect viewport);

    // Visits every widget in the tree (this one included) that actually
    // needs to be drawn (has a background or a visible border) — used by
    // the UI renderer to walk the tree after layout().
    void forEachVisible(const std::function<void(const Widget&)>& fn) const {
        if (!offScreen && (hasBackground || computedBorderWidth > 0.0f || hasShadow ||
            spriteSet != VK_NULL_HANDLE || shaderPipeline != VK_NULL_HANDLE))
            fn(*this);
        for (auto& c : children)
            c.forEachVisible(fn);
    }

    // Same idea, for widgets with .text() set — kept separate from
    // forEachVisible since text goes through a different draw path
    // (font/glyph batching, not a rounded-rect quad).
    void forEachText(const std::function<void(const Widget&)>& fn) const {
        if (!offScreen && hasText && !textContent.empty())
            fn(*this);
        for (auto& c : children)
            c.forEachText(fn);
    }

    // Widgets with .onClick()/.onHover()/.onFocus() set — what
    // DustEngine::endUI() hit-tests against and dispatches callbacks
    // through. Visited in the same parent-then-children order as
    // forEachVisible/forEachText, so "last match under the cursor" during
    // hit-testing naturally means "topmost" (children draw over parents).
    // Scroll containers, parents before children — so "last one containing
    // the cursor" is the innermost, which is the one the wheel should move.
    void forEachScrollable(const std::function<void(const Widget&)>& fn) const {
        if (scrollable) fn(*this);
        for (auto& c : children) c.forEachScrollable(fn);
    }

    // Mutable version — endUI() uses it to push last frame's scroll offsets
    // back into the freshly rebuilt tree before laying it out.
    void forEachScrollableMut(const std::function<void(Widget&)>& fn) {
        if (scrollable) fn(*this);
        for (auto& c : children) c.forEachScrollableMut(fn);
    }

    // Every widget in the tree, parents before children. The draw pass needs
    // one walk that sees rects and text together (they end up interleaved in
    // the same depth-sorted list), which forEachVisible/forEachText can't do
    // separately.
    void forEachWidget(const std::function<void(const Widget&)>& fn) const {
        fn(*this);
        for (auto& c : children) c.forEachWidget(fn);
    }

    bool isVisibleQuad() const {
        return !offScreen && (hasBackground || computedBorderWidth > 0.0f || hasShadow ||
               spriteSet != VK_NULL_HANDLE || shaderPipeline != VK_NULL_HANDLE);
    }
    bool hasDrawableText() const { return !offScreen && hasText && !textContent.empty(); }

    void forEachInteractive(const std::function<void(const Widget&)>& fn) const {
        if (!offScreen && isInteractive())
            fn(*this);
        for (auto& c : children)
            c.forEachInteractive(fn);
    }
};

inline Widget Row()    { Widget w; w.layoutMode = LayoutMode::Row;    return w; }
inline Widget Column() { Widget w; w.layoutMode = LayoutMode::Column; return w; }
inline Widget Stack()  { Widget w; w.layoutMode = LayoutMode::Stack;  return w; }

// ── Components ── DustUI-API.md's model is "components are just C++
// functions that build and return a Widget"; these are the handful common
// enough to ship. Nothing here is privileged — they're the same builder
// calls you'd write inline, and every one stays chainable afterwards.

// Auto-sized text. Give it a size if you want a fixed slot.
inline Widget Label(const char* text, Unit size = px(14), Color color = Colors::White) {
    return Widget().text(text, size, color);
}

// Flexible gap in a Row/Column — the idiom for pushing what follows to the
// far end without hand-computing offsets.
inline Widget Spacer(Unit amount) {
    Widget w;
    w.width = amount; w.height = amount; // whichever axis the parent flows along is the one that reads
    return w;
}

// Panel: a rounded, bordered container. The defaults are the same dark
// chrome the showcase uses, so `Panel()` alone already looks deliberate.
inline Widget Panel(Color bg = Color{ 0.12f, 0.12f, 0.14f, 0.92f },
                    Color borderCol = Colors::Gray,
                    Unit radius = px(6), Unit borderW = px(1)) {
    return Widget().background(bg).border(borderW, borderCol, radius).padding(px(8));
}

// Button: label centred in a rounded box, wired to onClick. `active` drives
// the highlighted state — DustUI is immediate mode, so the caller owns that
// bool and this stays stateless.
inline Widget Button(const char* label, std::function<void()> onClick,
                     bool active = false,
                     Unit w = px(120), Unit h = px(32)) {
    return Widget()
        .size(w, h)
        .background(active ? Color{ 0.35f, 0.30f, 0.12f, 1.0f } : Color{ 0.18f, 0.18f, 0.20f, 1.0f })
        .border(px(2), active ? Colors::Gold : Colors::Gray, px(5))
        .text(label, px(14), Colors::White)
        .align(HAlign::Center, VAlign::Middle)
        .onClick(std::move(onClick));
}

// ProgressBar: a track with a fill child sized by pct. Two widgets rather
// than a shader, because the rounded corners and border come free that way.
inline Widget ProgressBar(float fraction, Unit w = px(200), Unit h = px(16),
                          Color fill = Colors::Red, Color track = Colors::DarkGray) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    return Widget()
        .size(w, h)
        .background(track)
        .border(px(1), Colors::Gray, px(3))
        .child(Widget().size(pct(fraction), pct(1.0f)).background(fill).border(px(0), Colors::Transparent, px(3)));
}

} // namespace Dust::UI
