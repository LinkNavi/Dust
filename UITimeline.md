

---

## Phase 1 — Core layout + solid rendering [DONE]


---

## Phase 2 — Font / text [DONE]
Blocked on: nothing in DustUI itself, but the engine has no font system at all yet. Needs an **SDF glyph atlas** built offline via **msdfgen** (multichannel SDF — sharper corners than single-channel), a runtime atlas texture + glyph metadata loader, and a custom fragment shader doing SDF threshold so one atlas covers all sizes sharply. Per-glyph quad batching feeds into a single instanced draw call. Optional shader params unlock outline/shadow/glow at no extra atlas cost. `.text()` gets wired up to actually draw once this lands.

- [x] `AssetManager/FontFormat.hpp` — DustFont binary: header + per-glyph
      metrics (em-space plane bounds, atlas pixel rect) + RGBA8 MSDF atlas.
- [x] DustPacker: `.ttf`/`.otf` -> MSDF atlas (`FontImport.cpp`, msdfgen-ext
      for FreeType glyph loading, msdfgen-core for `generateMSDF`). Printable
      ASCII (0x20-0x7E) baked at 48px/em, 4px distance range, shelf-packed
      into one atlas, packed as `<name>.font` (same `.model` renaming
      convention as the assimp importer).
- [x] `Core/UI/Font.hpp/.cpp` — runtime `Font` (atlas `Texture` + descriptor
      set, reusing `Renderer::createMaterialSet`) and `layoutText()` (em ->
      screen-space glyph quads, Y-flip since em space is Y-up and screen
      space is Y-down, `\n` support even though nothing uses it yet).
- [x] `Shaders/text.vert`/`text.frag` — shared unit quad instanced once per
      glyph (`PipelineBuilder` gained an optional second, per-instance
      vertex binding to support this — `instanceAttribs`/`instanceStride`).
      Fragment shader does the standard msdfgen median-of-3-channels
      reconstruction, thresholded in screen-space pixels via a per-instance
      `screenPxRange` (`distanceRangePx * sizePx / atlasPixelsPerEm`) so a
      batch mixing multiple text sizes off the same font still thresholds
      correctly glyph-by-glyph. Outline support included (extra threshold
      band, push-constant width/color, 0 = off at zero extra cost); shadow/
      glow deferred — the same technique extends to them but wasn't needed
      for the Phase 2 demo, worth revisiting alongside Phase 9 (custom
      shader widgets) since the mechanism (extra distance thresholds) is
      the same idea.
- [x] `DustEngine::loadUIFont()`/`endUI()` — one shared font for every
      `.text()` widget (matches DustUI-API.md — `.text()` has no per-widget
      font param, and MSDF means one atlas already covers every size, so
      there's nothing to gain from more than one loaded font yet). Text
      widgets are left-aligned, vertically centered in their padded content
      box using the font's em-box (ascender/descender), not the specific
      string's ink, so mixed runs don't jitter the baseline.
- [x] Runtime demo — real "Kirby" name tag, a large "Quick Fox Jumps" sample
      proving the same atlas stays crisp from 14px to 48px, hotbar slot
      numbers. Screenshot-verified against the real GPU.

Bug found + fixed along the way: chased what looked like a glyph-rendering
bug (garbled/overlapping text) for a while before realizing it was
`models.pack` being stale relative to the DustPacker binary that packed it —
the exact class of problem the `zora run` dependency-closure fix addressed,
just for asset packing instead of compiling. No code bug; the pack just
needed regenerating. Worth remembering next time something looks broken
after touching an importer.

---

## Phase 3 — Input
Hover, click, and focus states per widget. Needs a hit-test pass after layout (walk the tree back-to-front, first widget whose rect contains the cursor wins), mouse button + release tracking, and a focused-widget concept for keyboard input. API additions: `.onClick()`, `.onHover()`, `.onFocus()` callbacks on `Widget`. Required before any interactive UI (hotbar clicks, inventory, buttons) is usable.

---

## Phase 4 — Diffing / batching
Move this earlier while the tree is still simple — before custom shaders complicate diff logic. Skip GPU work for widgets whose computed style+rect didn't change frame-to-frame. Batch multiple widget quads into fewer draw calls instead of one draw per widget. Covers DustUI-API.md's "DustUI diffs internally so only changed widgets re-draw."

---

## Phase 5 — Sprites
`.background(Texture&)` or a dedicated `.sprite()`/`.icon()` call to draw an image inside a widget's rect. Reuses `Dust::Texture` (already built for Model materials) — should be small once Phase 1's quad renderer exists.

---

## Phase 6 — Clipping / overflow
Widgets that overflow their parent's bounds need to be clipped. Critical for scrollable lists, tooltips, and any container with `overflow: hidden` semantics. Likely implemented via Vulkan scissor rect pushed/popped during the render walk. Without this, Column lists and Stack overlays bleed outside their bounds.

---

## Phase 7 — Scrollable containers
Scrollable `Column`/`Row` with an overflow clip. Depends on Phase 6 (clipping). Needed for inventory lists, spell books, settings menus. API addition: `.scrollable()` on `Column`/`Row`, exposing scroll offset either automatically (mouse wheel when hovered) or manually via a bound float.

---

## Phase 8 — Z-ordering
Control draw order between top-level widgets. Currently no way to guarantee a tooltip or modal renders over everything else. API addition: `.zIndex(int)` on `Widget`, sort top-level widgets by z before the render walk.

---

## Phase 9 — Custom shader widgets
`.shader(path, paramSetter)` — per-widget custom fragment shader with user-supplied params. Needs runtime shader loading (already exists — `ShaderModule::load`) plus a generic param-passing mechanism — likely a small UBO/descriptor per shader widget rather than growing the push constant block per-shader. Moved later since it's the most self-contained phase and nothing else depends on it.

---

## Phase 10 — Layers (HUD / overlay / world-space UI)
Explicitly marked TBD in DustUI-API.md. Overlaps goals.txt's "Add a way to edit layers" under Good Rendering — probably the same piece of work, worth doing together rather than twice.
