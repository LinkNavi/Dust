

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

## Phase 3 — Input [DONE]
Hover, click, and focus states per widget. Needs a hit-test pass after layout (walk the tree back-to-front, first widget whose rect contains the cursor wins), mouse button + release tracking, and a focused-widget concept for keyboard input. API additions: `.onClick()`, `.onHover()`, `.onFocus()` callbacks on `Widget`. Required before any interactive UI (hotbar clicks, inventory, buttons) is usable.

One system, shared by the engine and DustUI — not two competing input paths.
`isKeyDown()`/`isMouseButtonDown()`/etc. on `DustEngine` are capture-aware by
default: a focused UI widget (a text box) makes them report nothing at all
for real gameplay code, no manual "am I typing" check needed at each call
site. `ignoreUICapture=true` bypasses it for the rare case that needs to
(the widget system itself, or a pause-menu Escape key).

- [x] `Core/Input.hpp/.cpp` — raw per-window state (key/mouse down + edge-
      triggered pressed/released, mouse pos/delta, scroll, UTF-8 typed
      text) populated via GLFW callbacks (not polling, so fast taps between
      frames aren't missed), wired into `WindowManager`.
- [x] `DustEngine` query API — `isKeyDown/Pressed/Released`,
      `isMouseButtonDown/Pressed/Released`, `mousePosition/Delta`,
      `mouseScrollY`, `typedTextThisFrame`, `uiWantsMouse()`/
      `uiWantsKeyboard()`. Key/button codes are plain GLFW constants —
      consistent with the rest of Dust not hiding its backends.
- [x] `Widget::onClick()/onHover()/onFocus()` — setting any of them makes a
      widget hit-testable; only `onFocus()` also makes it *focusable*.
      `onHover()`/`onFocus()` are level-triggered (fire every frame the
      condition holds); `onClick()` is the one genuinely edge-triggered case
      (full press+release over the same widget). Identity across frames
      (needed since the tree is rebuilt from scratch every frame) is an
      *implicit* path-hash id — parent id + child index — computed during
      `layout()`. No `.id()` override exists yet; a widget that reorders
      itself among siblings (a sortable list) would need one — fine for the
      fixed-layout HUD style UI everything's built for today.
- [x] `DustEngine::endUI()` — hit-test pass right after `layout()`, before
      the render walks: last widget under the cursor in tree order wins
      (children draw over parents, so "last" = "topmost"). Click sets focus
      only if the hit widget is focusable; clicking anything else (a
      non-focusable widget, or empty space) blurs.
- [x] Runtime demo — hotbar slots are clickable (`.onClick` picks the active
      slot) and hoverable (`.onHover` brightens the border); a chat-box-style
      widget is focusable, and while focused a simulated "player moves
      forward" log (on W) goes quiet — the concrete "movement vs. text box"
      proof this phase was built for. Verified against the real GPU via
      synthesized X11 input (python-xlib/XTEST) at 1280x720.

**Open report — root-caused and fixed.** Bottom-anchored widgets being
invisible on Hyprland turned out to be two separate bugs, neither in
`resolveAnchor()`: (1) the hotbar `Row()` had no `.size()`, so it computed as
a 0x0 box — anchoring put its *origin* on the bottom edge and every slot
flowed off-screen below it (fixed by intrinsic sizing: a `px(0)` size now
means "size to children", see `measureSize()` in Widget.cpp); and (2) the
swapchain was never rebuilt when the compositor resized the window, because
Hyprland reports neither `OUT_OF_DATE` nor `SUBOPTIMAL` — so layout and the
viewport used the real window size while the swapchain stayed at its creation
size, drawing the whole UI ~1.5x magnified with its bottom/right outside the
render area. `Renderer::beginFrame()` now compares the swapchain extent
against the framebuffer size directly, and `endUI()` lays out against the
*swapchain* extent (scaling mouse coords to match) so layout, rendering, and
hit-testing can't disagree again.

---

## Phase 4 — Diffing / batching [DONE]
Move this earlier while the tree is still simple — before custom shaders complicate diff logic. Skip GPU work for widgets whose computed style+rect didn't change frame-to-frame. Batch multiple widget quads into fewer draw calls instead of one draw per widget. Covers DustUI-API.md's "DustUI diffs internally so only changed widgets re-draw."

- [x] `Core/UI/RectInstance.hpp` — one widget quad's full draw state (rect,
      fill, border, params, clip, sprite UVs). This struct *is* the
      per-instance vertex layout in ui.vert, so anything added to it needs a
      matching attribute in `Renderer::init`'s `uiPb.instanceAttribs`.
- [x] `ui.vert`/`ui.frag` rebuilt around the same shared unit quad + instance
      buffer the text pipeline already used: everything that was a push
      constant is now a per-instance attribute, and the push block is down to
      just `screenSize`.
- [x] `Renderer::drawUIRects()` replaces `drawUIRect()` — one instanced draw
      per *texture switch* rather than per widget. `endUI()` flattens the
      visible tree in draw order (children after parents) and cuts batches
      only where the descriptor set changes; the whole showcase HUD is 1-2
      draw calls.
- [x] Frame-to-frame diff: `endUI()` keeps last frame's instance array and
      skips the memcpy into the mapped buffer when nothing moved. Honest
      scope note — the draw calls themselves are still re-recorded every
      frame, because the command buffer is. The win is the upload, not the
      submission.

---

## Phase 5 — Sprites [DONE]
`.background(Texture&)` or a dedicated `.sprite()`/`.icon()` call to draw an image inside a widget's rect. Reuses `Dust::Texture` (already built for Model materials) — should be small once Phase 1's quad renderer exists.

- [x] `.sprite(texSet, u0,v0,u1,v1)` — takes the `VkDescriptorSet` that
      `DustEngine::createTextureSet()` already returns (the same handle a
      Model material or the particle system binds) rather than a `Texture&`,
      since the set is what actually gets bound. The UV sub-rect defaults to
      the whole texture; pass one to pull a cell out of an atlas.
- [x] ui.frag multiplies the fill by the sampled texture unconditionally —
      untextured widgets bind the renderer's existing 1x1 white fallback, so
      plain fills, plain sprites, and *tinted* sprites (`.background()` +
      `.sprite()`) are all one code path with no branch. Sprites go through
      the same rounded-rect SDF, so they round off with the box and get the
      border for free.
- [x] Runtime demo — a 96x96 rounded, bordered sprite widget top-left,
      sharing the sonic texture set the particle system uses.

---

## Phase 6 — Clipping / overflow [DONE]
Widgets that overflow their parent's bounds need to be clipped. Critical for scrollable lists, tooltips, and any container with `overflow: hidden` semantics. Likely implemented via Vulkan scissor rect pushed/popped during the render walk. Without this, Column lists and Stack overlays bleed outside their bounds.

Implemented as a **per-instance clip rect the fragment shaders discard
against**, not the scissor push/pop this entry originally guessed at —
scissor state is per-draw-call, so it would have split the Phase 4 batch at
every clipping container and undone the phase before it.

- [x] `.clip()` on any widget clips its descendants to its *content* box.
      `layoutRecursive()` carries the rect down and intersects it at each
      clipping ancestor, so nesting composes; `computedClipRect` lands on
      every widget alongside `computedRect`.
- [x] Applies to fills, borders, sprites (ui.frag) *and* text (text.frag —
      `GlyphInstance` gained a clip rect, narrowed by `endUI()` after each
      run is laid out, so glyphs cut off mid-shape at the container edge).
- [x] Hit-testing honours it too: a clipped-away row isn't visible, so it
      doesn't swallow clicks.
- [x] Runtime demo — a fixed 176x120 window over a taller Column that
      scrolls on a sine, rows cut off at both edges, still clickable only
      while visible.

---

## Phase 7 — Scrollable containers [DONE]
Scrollable `Column`/`Row` with an overflow clip. Depends on Phase 6 (clipping). Needed for inventory lists, spell books, settings menus. API addition: `.scrollable()` on `Column`/`Row`, exposing scroll offset either automatically (mouse wheel when hovered) or manually via a bound float.

- [x] `.scroll()` — shifts the flow start by the container's offset and turns
      clipping on (an unclipped scroll view just paints its overflow over
      whatever is beside it). Works on `Row`/`Column` along their own axis,
      and on a plain widget wrapping a tall `Column` (vertical), which is how
      you'd normally build one.
- [x] `computedContentExtent` — total length of the children along the layout
      axis, measured during layout, so the offset can be clamped to real
      overflow instead of scrolling into empty space.
- [x] Offsets live on `DustEngine` keyed by the widget's implicit path-hash
      id, not on the Widget (rebuilt every frame). `endUI()` therefore lays
      out twice on the first frame a container has a non-zero offset: pass
      one hands out ids and measures extents, pass two re-flows with the
      offset applied. Skipped entirely when nothing scrolls.
- [x] Wheel goes to the *innermost* scroll container under the cursor, and a
      scroll container counts as interactive so `uiWantsMouse()` is true over
      it — the wheel belongs to the list, not the game camera. The applied
      offset lands the following frame (this frame's layout already ran);
      one frame of wheel latency is invisible and it keeps layout to a
      single pass over known offsets.
- [x] Runtime demo — the clipped list from Phase 6 is now wheel-scrollable.
      Verified headlessly by driving the offset directly (clamping, re-flow,
      and clipping all confirmed against the real GPU); the wheel *event*
      path itself couldn't be exercised under Xvfb, since synthetic XTEST
      button-4/5 events never reached GLFW there.

---

## Phase 8 — Z-ordering [DONE]
Control draw order between top-level widgets. Currently no way to guarantee a tooltip or modal renders over everything else. API addition: `.zIndex(int)` on `Widget`, sort top-level widgets by z before the render walk.

- [x] `.z(int)` on any widget, sorted among its siblings — not just at the
      top level, since the cost is the same and a tooltip inside a panel
      wants it too.
- [x] The sort happens on the **flattened draw list**, not on the widget
      tree. First attempt reordered siblings before layout; that worked for a
      Stack but reordered a `Row`'s flow as a side effect of asking a child to
      draw on top, and it reassigned implicit ids whenever z changed. Sorting
      the emitted quads instead leaves layout, flow order, and ids completely
      untouched.
- [x] The key is `(layer, zIndex)` packed into a uint64 and inherited down the
      tree as a max, so lifting a container lifts its whole subtree and a
      child can never sink below its parent's bucket.
- [x] Stable sort, so equal keys keep tree order — children still draw over
      parents, and a widget's drop shadow stays immediately behind it.

---

## Phase 9 — Custom shader widgets [DONE]
`.shader(path, paramSetter)` — per-widget custom fragment shader with user-supplied params. Needs runtime shader loading (already exists — `ShaderModule::load`) plus a generic param-passing mechanism — likely a small UBO/descriptor per shader widget rather than growing the push constant block per-shader. Moved later since it's the most self-contained phase and nothing else depends on it.

- [x] `DustEngine::loadUIShader(fragSpv, len)` → `VkPipeline`, handed to
      `.shader(pipeline, p0..p7)`. A custom UI shader is a **fragment shader
      only**: it pairs with the stock ui.vert and must declare the same input
      interface, which is what lets a shader widget keep the same instance
      buffer, the same pipeline layout, and the same batcher as every other
      widget. The pipeline is bound through `uiLayout` and owned by Renderer.
- [x] Params ship as two vec4 instance attributes rather than the per-widget
      UBO/descriptor this entry guessed at. It costs 32 bytes on *every*
      instance including plain ones, but there's no descriptor set to
      allocate, write, or free per shader widget, and no extra bind — the
      batch key is just `(texture, pipeline)`.
- [x] `Shaders/ui_pulse.frag` — worked example (animated radial pulse,
      params = time/speed/rings), embedded by BuildShaders.sh via a new
      `embed_frag` helper that emits the frag alone, since pairing it with
      ui.vert again would redefine `ui_vert_spv`.
- [x] Runtime demo — a pulsing 110x110 disc, top right. Screenshot-verified.

---

## Phase 10 — Layers (HUD / overlay / world-space UI) [PARTIAL]
Explicitly marked TBD in DustUI-API.md. Overlaps goals.txt's "Add a way to edit layers" under Good Rendering — probably the same piece of work, worth doing together rather than twice.

- [x] `UI::Layer` (`Background` / `HUD` / `Overlay`) via `.setLayer()`, sorted
      ahead of `zIndex` in the same pass as Phase 8 — so layer is the coarse
      bucket and z orders within it. This is what makes "a modal is always
      above the HUD regardless of declaration order" true.
- [x] Text is sorted into the *same* list as the widget quads. Previously all
      text drew in one pass after every rect, which meant a modal scrim dimmed
      the panels behind it but the text on those panels showed straight
      through. Interleaving costs a pipeline switch per depth group, which for
      a real HUD is a handful of draw calls.
- [ ] **World-space UI is not built.** It's the other half of this phase and
      it isn't a sort key — a widget tree projected onto a quad in the 3D
      scene needs the camera matrix in ui.vert, depth testing back on, and a
      hit-test that raycasts instead of comparing screen rects. Deliberately
      left as a missing `Layer` value rather than one that silently does
      nothing. Worth doing alongside goals.txt's layer editing rather than
      twice.

---

## Post-phase — filling in the widget surface [DONE]

The ten phases got the *systems* in; this pass filled in the things a real UI
wants that no single phase owned, plus the higher-level components. Not a
phase of its own because nothing here needed new machinery — it's all built
on the instance layout, the batcher, and the input pass that already existed.

**Text.** Horizontal/vertical alignment, word wrap, and per-glyph outlines,
all via a new `layoutTextBox()`. The outline forced a font-bake change: the
atlas was baked with a 4px distance range, and thresholding past half of that
reads saturated field data and paints a grey box around each glyph instead of
an outline. Widened `kPxRange`/`kPadPx` to 12 in DustPacker (so `models.pack`
had to be regenerated — the same stale-pack trap Phase 2 hit) and clamped the
shader to the range it can honour, so an over-wide request degrades to
"thinner than asked" rather than "boxes".

**Visuals.** Linear gradients on fills, borders *and* text — each with its own
angle, `UI::degrees()` converting CSS-style angles (0 = up, 90 = right) into
the radians the shaders want. Per-corner radii. Drop shadows, emitted as one
extra instance behind the widget so they cost a buffer slot rather than a draw
call. Text gradients run across the whole block: every glyph carries its rect
expressed in the widget's content box, so the ramp is continuous across
letters and wrapped lines instead of restarting per glyph.

**Layout.** Margins, `.minSize()`/`.maxSize()`, `justifyContent`, `alignItems`.

**Input.** `.onPress()`/`.onRelease()` as the separate halves of a click,
`.onDrag()` (keeps firing after the cursor outruns the widget, which is what
makes dragging survive a fast mouse), `.blockInput()`, and `.id()` to pin
identity — closing the gap Phase 3 flagged, where identity followed the *slot*
so a reordering list moved hover/focus/scroll state to the wrong widget.

**Components** (`Core/UI/Components.hpp`): Label, Panel, Button, ProgressBar,
Spacer, Tooltip, Modal/ModalDialog, Dropdown, ImageButton, TextInput, Tabs,
Draggable, Toast. None own state — DustUI is immediate mode, so anything that
persists is a reference the caller keeps, which also makes saving/restoring UI
state just saving those variables.

### Two things this pass had to go back and fix

**Z-ordering reordered siblings.** Phase 8 sorted the widget tree before
layout. That's fine for a Stack, but in a `Row` it slid children along the
axis as a side effect of asking one to draw on top, and it reassigned implicit
ids whenever z changed. Draw order is now a stable sort on the *flattened draw
list*, keyed by a `(layer, z)` uint64 inherited down the tree as a max —
layout, flow order, and ids are all untouched, and lifting a container lifts
its whole subtree.

**Text drew after everything.** Rects and glyphs were two separate passes, so
every glyph landed on top of every quad. Invisible until the modal landed and
its scrim dimmed the panels behind it while their text showed straight
through. Both now go into the same depth-sorted list, at the cost of a
pipeline switch per depth group.

**Atlas bleed.** A hairline down the side of every glyph, only visible once an
outline widened the thresholded band: each glyph's UV rect sat exactly on its
cell boundary, so a bilinear tap at the quad edge blended in whichever
unrelated glyph the shelf packer had put next door. The fragment shader now
clamps sampling to half a texel inside the glyph's own cell (atlas dimensions
arrive as a push constant). Worth remembering for any other tightly-packed
atlas — the sprite path has the same shape of problem waiting in it.
