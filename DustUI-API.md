# DustUI API

CSS-but-CPP widget system built into Dust Engine. Everything is a `Widget`, layouts are widgets with children, components are just functions that return widgets.
ALL FONTS WILL USE AN SDF RENDERER AND CAN CHANGE SIZES WITHOUT HAVING TO DEFINE A WHOLE NEW FONT VAR
---

## Units

```cpp
Dust::px(5)       // absolute pixels
Dust::pct(0.5f)   // 50% of parent's dimension
Dust::vw(0.1f)    // 10% of viewport width
Dust::vh(0.1f)    // 10% of viewport height
```

`pct()` is always relative to the direct parent, never the screen. Use `vw`/`vh` for screen-relative sizing outside of a container.

---

## Anchors

Anchors position a widget relative to its parent (or screen if top-level).

```cpp
Anchor::TopLeft
Anchor::TopCenter
Anchor::TopRight
Anchor::CenterLeft
Anchor::Center
Anchor::CenterRight
Anchor::BottomLeft
Anchor::BottomCenter
Anchor::BottomRight
```

Usage:
```cpp
.anchor(Anchor::TopRight, px(-10), px(10))  // offset x, offset y from anchor point
.anchor(Anchor::Center)                      // dead center, no offset
```

### Anchor Reference Map

```
┌─────────────────────────────────────────┐
│ TopLeft          TopCenter      TopRight │
│ px(10), px(10)   px(0), px(10)  px(-10), px(10)
│                                         │
│ CenterLeft       Center      CenterRight│
│ px(10), px(0)    px(0), px(0)  px(-10), px(0)
│                                         │
│ BottomLeft    BottomCenter   BottomRight │
│ px(10),px(-10) px(0),px(-10) px(-10),px(-10)
└─────────────────────────────────────────┘
```

Offset signs:
- `x` positive = push right, negative = push left
- `y` positive = push down, negative = push up

```cpp
// 10px from top-left corner
.anchor(Anchor::TopLeft,     px(10),  px(10))

// centered horizontally, 10px from top
.anchor(Anchor::TopCenter,   px(0),   px(10))

// 10px from top-right corner
.anchor(Anchor::TopRight,    px(-10), px(10))

// 10px from left, vertically centered
.anchor(Anchor::CenterLeft,  px(10),  px(0))

// dead center
.anchor(Anchor::Center,      px(0),   px(0))

// 10px from right, vertically centered
.anchor(Anchor::CenterRight, px(-10), px(0))

// 10px from bottom-left corner
.anchor(Anchor::BottomLeft,  px(10),  px(-10))

// centered horizontally, 10px from bottom
.anchor(Anchor::BottomCenter,px(0),   px(-10))

// 10px from bottom-right corner
.anchor(Anchor::BottomRight, px(-10), px(-10))
```

---

## Widget

Base building block. Everything inherits from this.

```cpp
UI::Widget()
    .size(px(200), px(40))                        // width, height
    .background(Colors::DarkGray)                 // fill color
    .border(px(2), Colors::White, px(6))          // width, color, corner radius
    .padding(px(8))                               // inner spacing (all sides)
    .padding(px(8), px(4))                        // vertical, horizontal
    .padding(px(4), px(8), px(4), px(8))          // top, right, bottom, left
    .anchor(Anchor::TopLeft, px(10), px(10))
    .text("Hello", px(16), Colors::White)         // content, size, color
    .opacity(0.8f)                                // 0.0 - 1.0
    .child(UI::Widget()...);                      // nest children inside
```

### Everything else a Widget can do

```cpp
UI::Widget()
    // ── size & spacing ──
    .minSize(px(80), px(0)).maxSize(px(240), px(0))  // clamps; px(0) max = unbounded
    .margin(px(6))                                    // outside spacing — same 1/2/4-arg forms as padding

    // ── fill ──
    .gradient(Colors::DarkBlue, Colors::Purple, degrees(135))  // fill gradient
    .borderGradient(Colors::Cyan, Colors::Purple, degrees(45)) // border gradient, own angle
    .textGradient(Colors::Gold, Colors::Cyan, degrees(90))     // across the whole text block
    .sprite(texSet, 0, 0, 1, 1)                        // texture (+ optional UV sub-rect)
    .shadow(Colors::Black.alpha(0.6f), 10.0f, 0, 4)    // colour, blur px, dx, dy
    .corners(px(16), px(4), px(16), px(4))             // per-corner radius: TL, TR, BR, BL

    // ── text ──
    .align(HAlign::Center, VAlign::Middle)             // inside the padded content box
    .wrap()                                            // word wrap to the content width
    .textOutline(px(2), Colors::Black)                 // per-glyph outline

    // ── behaviour ──
    .clip()                                            // clip descendants to the content box
    .scroll()                                          // wheel-scrollable (implies .clip())
    .z(10).setLayer(Layer::Overlay)                    // draw order: layer first, then z
    .id(0xBEEF)                                        // pin identity across reorders
    .shader(pipeline, p0, p1, /*...*/ p7)              // custom fragment shader
    .onPress(fn).onRelease(fn).onClick(fn).onHover(fn).onFocus(fn);
```

Notes worth knowing:

- A `px(0)` size means **auto** — the widget sizes itself to its children. That's
  why an un-sized `Row()` anchored to an edge used to vanish.
- `.textOutline()` is limited by the font atlas's baked distance range
  (`kPxRange` in DustPacker's `FontImport.cpp`); asking for more than about
  half of it is clamped rather than degrading into a box around each glyph.
- `.id()` matters for any list that reorders or conditionally inserts items —
  without it, identity follows the *slot*, so hover/focus/scroll state jumps
  to whatever moved into that position.
- `UI::degrees()` converts a CSS-style gradient angle (0 = up, 90 = right,
  clockwise) into the radians the shaders want. Every gradient — fill, border,
  text — takes the same convention, and a custom shader can read the same
  angle out of its params.
- Draw order is a sort on the flattened quad list, not on the tree: `.z()` and
  `.setLayer()` never move a widget in a `Row`, never change its id, and lift
  its text along with its background.

---

## Layouts

Layouts are widgets that arrange children automatically.

`Row`/`Column` also take distribution and alignment, both of which need the
container to have an explicit size — an auto-sized one is exactly as big as
its children, so there is nothing left over to distribute:

```cpp
UI::Row()
    .size(pct(1.0f), px(34))
    .justifyContent(Justify::SpaceBetween)  // Start, Center, End, SpaceBetween, SpaceAround
    .align(AlignItems::Center)              // Start, Center, End, Stretch
    .gap(px(8));
```

### Row — horizontal

```cpp
UI::Row()
    .gap(px(8))          // space between children
    .padding(px(8))
    .size(pct(1.0f), px(40))
    .child(UI::Widget()...)
    .child(UI::Widget()...);
```

### Column — vertical

```cpp
UI::Column()
    .gap(px(4))
    .padding(px(8))
    .child(UI::Widget()...)
    .child(UI::Widget()...);
```

### Stack — z-layered (for overlays)

```cpp
UI::Stack()
    .size(px(200), px(200))
    .child(UI::Widget()...)   // bottom layer
    .child(UI::Widget()...);  // top layer, drawn over
```

Children in a Stack anchor relative to the Stack widget.

---

## Interactive components

Everything below lives in `Core/UI/Components.hpp`. DustUI is immediate mode,
so none of them own state — anything that persists (is this open? what's
typed?) is a reference the caller keeps, which also makes saving/restoring UI
state just saving those variables.

```cpp
UI::Tooltip("Click to equip");                        // build it only on frames it should show
UI::ModalDialog("Confirm", body, "OK", onConfirm);    // scrim blocks input to everything behind
UI::Dropdown(options, count, selected, open);         // clipped, scrollable option list
UI::ImageButton(texSet, onClick);                     // sprite + click
UI::TextInput(buffer, focused, "placeholder");        // pair with engine.editFocusedText(buffer)
UI::Tabs(labels, count, activeTab);                   // writes the active index
UI::Draggable("Title", x, y, std::move(body));        // title bar drags, writes x/y
UI::Toasts(toastVector, deltaTime);                   // ticks timers and builds the survivors
```

Two of them lean on primitives worth knowing about on their own:

- **`.blockInput()`** — the widget swallows input aimed at anything drawn
  before it. That's the whole modal mechanism: nothing behind a dialog needs
  to know the dialog exists. Its own children come later in the walk, so they
  stay live.
- **`.onDrag(fn)`** — fires every frame the mouse is held after pressing this
  widget, with the frame's delta. It keeps firing even once the cursor
  outruns the widget, which is what makes dragging survive a fast mouse.

---

## Built-in components

Shipped in `Core/UI/Widget.hpp` — nothing privileged, just the builder calls
you'd otherwise write inline, and each one stays chainable:

```cpp
UI::Label("Kitchen Sink", px(20));                 // auto-sized text
UI::Panel();                                        // rounded, bordered, padded container
UI::Button("Start", []{ ... }, isActive);           // centred label + onClick
UI::ProgressBar(0.6f, px(200), px(16));             // track + fill child
UI::Spacer(px(12));                                 // fixed gap in a Row/Column
```

---

## Components (reusable functions)

Break big widget trees into named pieces or functions.

```cpp
// Named piece
auto hpBar = UI::Widget()
    .size(pct(1.0f), px(12))
    .background(Colors::DarkRed)
    .border(px(0), Colors::None, px(2))
    .child(UI::Widget()
        .size(pct(playerHpPct), pct(1.0f))
        .background(Colors::Red));

// Reusable component function
UI::Widget StatBar(float pct, Color fill, Color bg) {
    return UI::Widget()
        .size(pct(1.0f), px(12))
        .background(bg)
        .border(px(0), Colors::None, px(2))
        .child(UI::Widget()
            .size(pct(pct), pct(1.0f))
            .background(fill));
}

// Usage
.child(StatBar(playerHpPct,  Colors::Red,    Colors::DarkRed))
.child(StatBar(playerMpPct,  Colors::Blue,   Colors::DarkBlue))
.child(StatBar(playerStamPct, Colors::Yellow, Colors::DarkGray))
```

---

## Real Examples

### Health Bar

```cpp
UI::Row()
    .size(px(200), px(20))
    .background(Colors::DarkRed)
    .border(px(2), Colors::White, px(4))
    .anchor(Anchor::BottomCenter, px(0), px(-20))
    .child(UI::Widget()
        .size(pct(playerHpPct), pct(1.0f))
        .background(Colors::Red));
```

### Name Tag

```cpp
UI::Widget()
    .size(px(120), px(30))
    .background(Colors::DarkGray)
    .border(px(2), Colors::White, px(5))
    .anchor(Anchor::TopCenter, px(0), px(-35))
    .padding(px(4), px(8))
    .text("Steve", px(14), Colors::White);
```

### Target Stats Box

```cpp
// Components
UI::Widget LabeledBar(const char* label, float pct, Color fill, Color bg) {
    return UI::Row()
        .gap(px(4))
        .size(pct(1.0f), px(14))
        .child(UI::Widget()
            .size(px(24), pct(1.0f))
            .text(label, px(11), Colors::Gray))
        .child(UI::Widget()
            .size(pct(1.0f), pct(1.0f))
            .background(bg)
            .border(px(0), Colors::None, px(2))
            .child(UI::Widget()
                .size(pct(pct), pct(1.0f))
                .background(fill)));
}

// Assembly
auto targetStats = UI::Widget()
    .size(px(200), px(100))
    .background(Colors::DarkGray)
    .border(px(2), Colors::White, px(6))
    .anchor(Anchor::TopRight, px(-10), px(10))
    .child(UI::Column()
        .padding(px(8))
        .gap(px(6))
        .child(UI::Widget()
            .text("Steve", px(15), Colors::White))
        .child(LabeledBar("HP", targetHpPct,  Colors::Red,    Colors::DarkRed))
        .child(LabeledBar("MP", targetMpPct,  Colors::Blue,   Colors::DarkBlue))
        .child(LabeledBar("SP", targetSpPct,  Colors::Yellow, Colors::DarkGray)));
```

### Hotbar

```cpp
UI::Widget HotbarSlot(int index, bool active) {
    return UI::Stack()
        .size(px(48), px(48))
        .background(active ? Colors::DarkGold : Colors::DarkGray)
        .border(px(2), active ? Colors::Gold : Colors::Gray, px(4))
        .child(UI::Widget()                            // item icon placeholder
            .size(px(32), px(32))
            .anchor(Anchor::Center)
            .background(Colors::Gray))
        .child(UI::Widget()                            // slot number
            .anchor(Anchor::BottomRight, px(-2), px(-2))
            .text(std::to_string(index).c_str(), px(9), Colors::LightGray));
}

UI::Row()
    .gap(px(4))
    .anchor(Anchor::BottomCenter, px(0), px(-10))
    .child(HotbarSlot(1, true))
    .child(HotbarSlot(2, false))
    .child(HotbarSlot(3, false))
    ...
```

---

## Full Game UI Example (in-engine)

A complete RPG HUD — player stats bottom-left, hotbar bottom-center, minimap bottom-right, target stats top-right, spell charge top-center, alerts top-left.

```cpp
// ── Components ──────────────────────────────────────────────

UI::Widget StatBar(const char* label, float pct, Color fill, Color bg) {
    return UI::Row()
        .gap(px(4))
        .size(pct(1.0f), px(13))
        .child(UI::Widget()
            .size(px(20), pct(1.0f))
            .text(label, px(10), Colors::LightGray))
        .child(UI::Widget()
            .size(pct(1.0f), pct(1.0f))
            .background(bg)
            .border(px(0), Colors::None, px(2))
            .child(UI::Widget()
                .size(pct(pct), pct(1.0f))
                .background(fill)));
}

UI::Widget HotbarSlot(int index, bool active, bool hasItem) {
    return UI::Stack()
        .size(px(48), px(48))
        .background(active ? Colors::DarkGold : Colors::DarkGray)
        .border(px(2), active ? Colors::Gold : Colors::Gray, px(4))
        .child(UI::Widget()
            .size(px(32), px(32))
            .anchor(Anchor::Center)
            .background(hasItem ? Colors::Gray : Colors::Transparent))
        .child(UI::Widget()
            .anchor(Anchor::BottomRight, px(-3), px(-3))
            .text(std::to_string(index).c_str(), px(9), Colors::LightGray));
}

// ── Full HUD ────────────────────────────────────────────────

e.beginUI();

    // ── TOP LEFT — alerts/buffs
    UI::Column()
        .anchor(Anchor::TopLeft, px(10), px(10))
        .gap(px(4))
        .child(UI::Widget()
            .size(px(160), px(28))
            .background(Color{0.8f, 0.2f, 0.2f, 0.85f})
            .border(px(1), Colors::Red, px(4))
            .padding(px(4), px(8))
            .text("Burning  -5hp/s", px(12), Colors::White))
        .child(UI::Widget()
            .size(px(160), px(28))
            .background(Color{0.2f, 0.4f, 0.8f, 0.85f})
            .border(px(1), Colors::Blue, px(4))
            .padding(px(4), px(8))
            .text("Haste  +30% spd", px(12), Colors::White));

    // ── TOP CENTER — spell charge meter
    UI::Widget()
        .anchor(Anchor::TopCenter, px(0), px(10))
        .size(px(64), px(64))
        .shader("spiral_charge.frag", [&](UI::ShaderParams& p) {
            p.set("progress", spellChargePct);
            p.set("color",    Colors::Purple);
            p.set("bg_color", Colors::DarkGray);
        });

    // ── TOP RIGHT — target stats
    UI::Widget()
        .anchor(Anchor::TopRight, px(-10), px(10))
        .size(px(200), px(110))
        .background(Color{0.1f, 0.1f, 0.1f, 0.85f})
        .border(px(2), Colors::Gray, px(6))
        .child(UI::Column()
            .padding(px(8))
            .gap(px(6))
            .child(UI::Widget()
                .text("Steve", px(15), Colors::White))
            .child(StatBar("HP", targetHpPct,  Colors::Red,    Colors::DarkRed))
            .child(StatBar("MP", targetMpPct,  Colors::Blue,   Colors::DarkBlue))
            .child(StatBar("SP", targetSpPct,  Colors::Yellow, Colors::DarkGray)));

    // ── BOTTOM LEFT — player stats
    UI::Widget()
        .anchor(Anchor::BottomLeft, px(10), px(-10))
        .size(px(200), px(90))
        .background(Color{0.1f, 0.1f, 0.1f, 0.85f})
        .border(px(2), Colors::Gray, px(6))
        .child(UI::Column()
            .padding(px(8))
            .gap(px(6))
            .child(UI::Widget()
                .text("Kirby", px(15), Colors::White))
            .child(StatBar("HP", playerHpPct,  Colors::Red,    Colors::DarkRed))
            .child(StatBar("MP", playerMpPct,  Colors::Blue,   Colors::DarkBlue))
            .child(StatBar("SP", playerSpPct,  Colors::Yellow, Colors::DarkGray)));

    // ── BOTTOM CENTER — hotbar
    UI::Row()
        .anchor(Anchor::BottomCenter, px(0), px(-10))
        .gap(px(4))
        .child(HotbarSlot(1, true,  true))
        .child(HotbarSlot(2, false, true))
        .child(HotbarSlot(3, false, true))
        .child(HotbarSlot(4, false, false))
        .child(HotbarSlot(5, false, false))
        .child(HotbarSlot(6, false, false))
        .child(HotbarSlot(7, false, false))
        .child(HotbarSlot(8, false, false))
        .child(HotbarSlot(9, false, false));

    // ── BOTTOM RIGHT — minimap
    UI::Stack()
        .anchor(Anchor::BottomRight, px(-10), px(-10))
        .size(px(160), px(160))
        .background(Colors::Black)
        .border(px(2), Colors::Gray, px(80))   // full radius = circle
        .child(UI::Widget()                     // player dot
            .size(px(6), px(6))
            .anchor(Anchor::Center, px(0), px(0))
            .background(Colors::White)
            .border(px(0), Colors::None, px(3)));

e.endUI();
```

### What's anchored where

```
┌─────────────────────────────────────────────┐
│ [Buffs]        [Spell Charge]  [Target Stats]│
│ TopLeft        TopCenter       TopRight      │
│                                             │
│                                             │
│                                             │
│ [Player Stats] [Hotbar]        [Minimap]    │
│ BottomLeft     BottomCenter    BottomRight  │
└─────────────────────────────────────────────┘
```

---

## Immediate Mode (live updating)

Rebuild the widget tree every frame inside `beginUI`/`endUI`. DustUI diffs internally so only changed widgets re-draw.

```cpp
while (!e.shouldClose()) {
    e.beginDrawing();
        e.beginUI();
            UI::Widget()
                .size(px(200), px(20))
                .background(Colors::DarkRed)
                .child(UI::Widget()
                    .size(pct(playerHpPct), pct(1.0f))  // updates every frame
                    .background(Colors::Red));
        e.endUI();
    e.endDrawing();
}
```

---

## Custom Shader Widgets

For UI that can't be expressed with shapes alone — spiral charge meters, bent health bars, liquid fills, etc.

```cpp
UI::Widget()
    .size(px(64), px(64))
    .shader("spiral_charge.frag", [&](UI::ShaderParams& p) {
        p.set("progress", chargeAmount);   // 0.0 - 1.0
        p.set("color",    Colors::Purple);
        p.set("bg_color", Colors::DarkGray);
    });
```

The shader receives:
- `uv` — normalized 0-1 coords of the widget quad
- `resolution` — pixel size of the widget
- Any params you pass via `ShaderParams`

### Spiral Charge Shader Example

```glsl
// spiral_charge.frag
layout(location = 0) in vec2 uv;
layout(push_constant) uniform Params {
    float progress;
    vec4  color;
    vec4  bg_color;
    vec2  resolution;
} p;

#define TAU 6.28318

void main() {
    vec2 centered = uv - 0.5;
    float angle = atan(centered.y, centered.x);         // -PI to PI
    float norm_angle = (angle / TAU) + 0.5;             // 0 to 1
    float radius = length(centered);

    // Archimedean spiral: r = a + b*theta
    float turns = 3.0;
    float spiral_r = fract(norm_angle * turns + p.progress * turns);
    float thickness = 0.04;

    float on_spiral = smoothstep(thickness, 0.0, abs(radius - spiral_r * 0.45));
    float filled = step(norm_angle, p.progress);

    vec4 col = mix(p.bg_color, p.color, on_spiral * filled);
    fragColor = col;
}
```

### Bent Health Bar Shader Example

```glsl
// bent_bar.frag — straight then 45deg then straight
layout(location = 0) in vec2 uv;
layout(push_constant) uniform Params {
    float progress;
    vec4  fill_color;
    vec4  bg_color;
} p;

float sampleBar(vec2 uv, float progress) {
    // remap uv.x along a bent path
    float bend_start = 0.6;
    float bend_end   = 0.75;

    float path;
    if (uv.x < bend_start) {
        path = uv.x / bend_start * bend_start;
    } else if (uv.x < bend_end) {
        float t = (uv.x - bend_start) / (bend_end - bend_start);
        path = bend_start + t * 0.15;
    } else {
        path = bend_end + (uv.x - bend_end);
    }

    return step(path, progress);
}

void main() {
    float filled = sampleBar(uv, p.progress);
    fragColor = mix(p.bg_color, p.fill_color, filled);
}
```

Usage:
```cpp
UI::Widget()
    .size(px(180), px(16))
    .shader("bent_bar.frag", [&](UI::ShaderParams& p) {
        p.set("progress",   playerHpPct);
        p.set("fill_color", Colors::Red);
        p.set("bg_color",   Colors::DarkRed);
    });
```

---

## In-Engine Frame Structure

```cpp
e.beginDrawing();
    e.beginMode3D(camera);
        // world geometry
    e.endMode3D();

    e.beginUI();
        // all UI here, drawn on top of 3D
        UI::Widget()...
    e.endUI();
e.endDrawing();
```

UI always draws over 3D. Layer system (HUD, overlay, world-space UI) TBD.

---

## Color Constants

```cpp
Colors::White       Colors::Black
Colors::Red         Colors::DarkRed
Colors::Green       Colors::DarkGreen
Colors::Blue        Colors::DarkBlue
Colors::Yellow      Colors::Gold        Colors::DarkGold
Colors::Gray        Colors::DarkGray    Colors::LightGray
Colors::Purple      Colors::Cyan
Colors::Transparent

// Or raw
Color{ 1.0f, 0.5f, 0.2f, 1.0f }  // RGBA 0-1
Color::fromHex(0xFF8844FF)
```
