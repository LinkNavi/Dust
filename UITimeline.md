# DustUI Timeline

Target API lives in `DustUI-API.md`. This file tracks build order — each
phase depends on the one before it. Update the checkboxes/status as work
lands; don't rewrite history, just flip status.

One deliberate deviation from DustUI-API.md, decided during Phase 1: the doc
shows bare top-level widgets between `beginUI()`/`endUI()` registering
themselves implicitly —

```cpp
e.beginUI();
    UI::Column().anchor(...)...;   // no assignment, no explicit add
    UI::Widget().anchor(...)...;
e.endUI();
```

There's no standard-C++ hook that fires when a temporary expression
statement finishes (short of RAII destructor tricks that get fragile fast
with copies/moves in a fluent chain). So `beginUI()` returns the root
`Widget&`, and top-level widgets attach with an explicit `.child(...)`:

```cpp
auto& ui = e.beginUI();
ui.child(UI::Column().anchor(...)...);
ui.child(UI::Widget().anchor(...)...);
e.endUI();
```

Everything else — `Row`/`Column`/`Stack`, `.anchor()`, `.size()`,
`.background()`, `.border()`, `.padding()`, `.child()` nesting, units,
components-as-functions — matches the doc as written.

---

## Phase 1 — Core layout + solid rendering [IN PROGRESS]

The minimum to see and position boxes on screen. No text, no images, no
custom shaders yet — this phase is purely `background`/`border`/`padding`/
`anchor`/`Row`/`Column`/`Stack`.

- [ ] `Color` + `Colors::` constants + `Color::fromHex`
- [ ] `Unit` (`px`/`pct`/`vw`/`vh`) + resolution against a parent/viewport size
- [ ] `Anchor` (9-point) + offset resolution
- [ ] `Widget` — value-type fluent builder: `.size/.background/.border/
      .padding/.opacity/.anchor/.gap/.child`. `.text()` exists on the
      builder (so call sites from the doc compile) but is a stored, unused
      stub until Phase 2 — no glyphs yet.
- [ ] `Row`/`Column`/`Stack` layout modes
- [ ] Layout pass: anchor resolution for anchored widgets, sequential flow
      for un-anchored Row/Column children, overlay for Stack children —
      see the deviation note above for why every top-level widget needs an
      anchor or an explicit position
- [ ] UI render pipeline — screen-space quads, rounded-rect + border SDF
      fragment shader, alpha blending, no depth test, drawn after 3D content
      in the same dynamic-rendering pass (`e.beginUI()/endUI()` slot in
      after `endMode3D()`)
- [ ] `DustEngine::beginUI()/endUI()` — rebuilds + redraws the whole tree
      every frame (no diffing yet, see Phase 5)
- [ ] Runtime demo — one or two examples straight out of the doc (health
      bar, hotbar), screenshot-verified against the real GPU like the model
      loading work

## Phase 2 — Font / text

Blocked on: nothing in DustUI itself, but the engine has no font system at
all yet (goals.txt "Font System" section, currently empty). Needs a glyph
atlas (likely stb_truetype, vendored the same way stb_image was for texture
loading), multi-size support per the goals.txt line ("one font instance
support multiple sizes"), and per-glyph quad batching. `.text()` gets wired
up to actually draw once this lands.

## Phase 3 — Sprites

`.background(Texture&)` or a dedicated `.sprite()`/`.icon()` call to draw an
image inside a widget's rect. Reuses `Dust::Texture` (already built for
Model materials) — should be small once Phase 1's quad renderer exists.

## Phase 4 — Custom shader widgets

`.shader(path, paramSetter)` — per-widget custom fragment shader with
user-supplied params (spiral charge meters, bent bars, liquid fills). Needs
runtime shader loading (already exists — `ShaderModule::load`) plus a
generic param-passing mechanism, since push constants are fixed-layout and
each shader widget wants different params — likely a small UBO/descriptor
per shader widget rather than growing the push constant block per-shader.

## Phase 5 — Immediate-mode diffing / batching

DustUI-API.md's immediate-mode section says "DustUI diffs internally so only
changed widgets re-draw" — Phase 1 does not do this, it redraws everything
every frame. Add: skip GPU work for widgets whose computed style+rect didn't
change frame-to-frame, and batch multiple widget quads into fewer draw calls
instead of one draw per widget.

## Phase 6 — Layers (HUD / overlay / world-space UI)

Explicitly marked TBD in DustUI-API.md itself. Overlaps goals.txt's "Add a
way to edit layers" under Good Rendering — probably the same piece of work,
worth doing together rather than twice.
