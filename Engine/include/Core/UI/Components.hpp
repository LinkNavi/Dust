#pragma once

#include "Core/UI/Widget.hpp"
#include <string>
#include <vector>

// Higher-level DustUI components. Everything here is a plain function that
// builds and returns a Widget — DustUI-API.md's model exactly. Nothing is
// privileged: no component touches engine state the caller can't, and every
// return value is still chainable, so `Dropdown(...).z(50)` works.
//
// DustUI is immediate mode, so none of these own state. Anything that has to
// persist across frames (is this dropdown open? what's in this text box?) is
// a reference parameter the caller keeps — which also means saving/restoring
// UI state is just saving those variables.
namespace Dust::UI {

// CSS-style gradient angle -> the radians the shaders want. Matches CSS
// `linear-gradient(<deg>, ...)`: 0deg points up, 90deg right, clockwise.
// Without this you're converting in your head every time, and the sign of
// the Y axis (screen space is Y-down) is easy to get backwards.
inline float degrees(float cssDeg) { return (cssDeg - 90.0f) * 0.01745329252f; }

// ── Tooltip ────────────────────────────────────────────────────────────
// A small popup, anchored wherever you put it, on the Overlay layer so it
// wins over whatever it's floating above. Build it only on the frames it
// should be visible — that's the immediate-mode way to "show" something.
inline Widget Tooltip(const char* text, Unit size = px(12)) {
    return Widget()
        .background(Color{ 0.08f, 0.08f, 0.10f, 0.96f })
        .border(px(1), Colors::Gray, px(4))
        .padding(px(5), px(9))
        .shadow(Colors::Black.alpha(0.5f), 6.0f, 0.0f, 2.0f)
        .setLayer(Layer::Overlay)
        .text(text, size, Colors::White)
        .align(HAlign::Center, VAlign::Middle);
}

// ── Modal ──────────────────────────────────────────────────────────────
// A full-viewport scrim that eats input aimed at anything behind it, plus a
// centred dialog panel. `.blockInput()` on the scrim is what does the eating:
// hit-testing skips everything drawn before it, and the dialog is a child so
// it stays live. Add your own content with .child() on the returned widget's
// dialog — or use ModalDialog() below, which hands you the panel directly.
inline Widget Modal(Widget dialog, Color scrim = Color{ 0.0f, 0.0f, 0.0f, 0.55f }) {
    return Widget()
        .size(vw(1.0f), vh(1.0f))
        .background(scrim)
        .setLayer(Layer::Overlay)
        .z(100)
        .blockInput()
        .child(std::move(dialog));
}

// Convenience: title + body + a single confirm button, centred.
inline Widget ModalDialog(const char* title, const char* body,
                          const char* confirmLabel, std::function<void()> onConfirm,
                          Unit w = px(320), Unit h = px(170)) {
    return Modal(Widget()
        .size(w, h)
        .anchor(Anchor::Center)
        .background(Color{ 0.12f, 0.13f, 0.16f, 1.0f })
        .border(px(2), Colors::Gold, px(10))
        .shadow(Colors::Black.alpha(0.7f), 16.0f, 0.0f, 6.0f)
        .padding(px(14))
        .child(Column()
            .size(pct(1.0f), pct(1.0f))
            .gap(px(10))
            .child(Widget().size(pct(1.0f), px(24))
                .text(title, px(17), Colors::White)
                .align(HAlign::Center))
            .child(Widget().size(pct(1.0f), px(56))
                .text(body, px(13), Colors::LightGray)
                .wrap()
                .align(HAlign::Left, VAlign::Top))
            .child(Widget().size(pct(1.0f), px(34))
                .child(Button(confirmLabel, std::move(onConfirm), false, px(110), px(30))
                    .anchor(Anchor::TopRight)))));
}

// ── Dropdown ───────────────────────────────────────────────────────────
// Closed: a button showing the current selection. Open: the same button plus
// a clipped Column of options below it. `open` and `selected` are the
// caller's — toggling and selecting just write to them.
inline Widget Dropdown(const char* const* options, int count, int& selected, bool& open,
                       Unit w = px(150), Unit rowH = px(26), Unit maxListH = px(120)) {
    Widget root = Column().gap(px(2));

    root.child(Widget()
        .size(w, rowH)
        .background(Color{ 0.18f, 0.18f, 0.20f, 1.0f })
        .border(px(1), open ? Colors::Gold : Colors::Gray, px(4))
        .padding(px(0), px(8))
        .text(count > 0 ? options[selected] : "", px(13), Colors::White)
        .onClick([&open]() { open = !open; }));

    if (open) {
        Widget list = Column().gap(px(1));
        for (int i = 0; i < count; i++) {
            list.child(Widget()
                .size(pct(1.0f), rowH)
                .background(i == selected ? Color{ 0.30f, 0.26f, 0.10f, 1.0f }
                                          : Color{ 0.14f, 0.14f, 0.16f, 1.0f })
                .padding(px(0), px(8))
                .text(options[i], px(13), Colors::White)
                // Capturing by reference is safe: the callback is invoked
                // inside endUI(), during the same frame this tree was built.
                .onClick([&selected, &open, i]() { selected = i; open = false; }));
        }
        // Scroll rather than clip alone, so a long option list still works.
        root.child(Widget()
            .size(w, maxListH)
            .background(Color{ 0.10f, 0.10f, 0.12f, 0.98f })
            .border(px(1), Colors::Gray, px(4))
            .shadow(Colors::Black.alpha(0.5f), 8.0f, 0.0f, 3.0f)
            .padding(px(2))
            .scroll()
            .child(list));
    }
    // Above its siblings, or the options would open *behind* whatever comes
    // next in the parent.
    return root.z(20);
}

// ── Image button ───────────────────────────────────────────────────────
// Sprite + click, with the border doubling as the state readout.
inline Widget ImageButton(VkDescriptorSet texSet, std::function<void()> onClick,
                          Unit size = px(48), bool active = false,
                          Color tint = Colors::White) {
    return Widget()
        .size(size, size)
        .background(tint)
        .sprite(texSet)
        .border(px(2), active ? Colors::Gold : Colors::Gray, px(6))
        .onClick(std::move(onClick));
}

// ── Text input ─────────────────────────────────────────────────────────
// Focusable box that displays `buffer`. Pair it with
// DustEngine::editFocusedText(buffer) after endUI() — this half draws and
// takes focus, that half applies the keystrokes. `focused` must be reset to
// false by the caller each frame before endUI(); onFocus is level-triggered,
// so it re-sets it every frame focus actually holds.
inline Widget TextInput(const std::string& buffer, bool& focused,
                        const char* placeholder = "",
                        Unit w = px(240), Unit h = px(30)) {
    bool empty = buffer.empty();
    return Widget()
        .size(w, h)
        .background(Color{ 0.10f, 0.10f, 0.12f, 1.0f })
        .border(px(2), focused ? Colors::Cyan : Colors::Gray, px(4))
        .padding(px(0), px(8))
        .clip() // long input scrolls out of the box instead of over the HUD
        .onFocus([&focused]() { focused = true; })
        // A caret only while focused — immediate mode means "append a glyph"
        // is the whole implementation.
        .text(empty ? placeholder : (focused ? (buffer + "|").c_str() : buffer.c_str()),
              px(13), empty ? Colors::Gray : Colors::White);
}

// ── Tabs ───────────────────────────────────────────────────────────────
// A Row of buttons that writes the active index. The caller switches on that
// index to decide which body to build — swapping the body here would mean
// building every tab's contents every frame just to throw them away.
inline Widget Tabs(const char* const* labels, int count, int& active,
                   Unit tabW = px(90), Unit tabH = px(28)) {
    Widget row = Row().gap(px(2));
    for (int i = 0; i < count; i++) {
        bool on = (i == active);
        row.child(Widget()
            .size(tabW, tabH)
            .background(on ? Color{ 0.20f, 0.20f, 0.24f, 1.0f } : Color{ 0.12f, 0.12f, 0.14f, 1.0f })
            // Square bottom corners so the active tab reads as joined to the
            // panel under it.
            .corners(px(6), px(6), px(0), px(0))
            .border(px(1), on ? Colors::Gold : Colors::Gray, px(6))
            .text(labels[i], px(13), on ? Colors::White : Colors::LightGray)
            .align(HAlign::Center)
            .onClick([&active, i]() { active = i; }));
    }
    return row;
}

// ── Draggable ──────────────────────────────────────────────────────────
// Wraps `content` in a title bar you can drag. x/y are the caller's — the
// drag callback just adds the frame's mouse delta to them.
inline Widget Draggable(const char* title, float& x, float& y, Widget content,
                        Unit w = px(220), Unit h = px(150)) {
    return Widget()
        .size(w, h)
        .anchor(Anchor::TopLeft, px(x), px(y))
        .background(Color{ 0.12f, 0.13f, 0.16f, 0.96f })
        .border(px(1), Colors::Gray, px(8))
        .shadow(Colors::Black.alpha(0.5f), 10.0f, 0.0f, 4.0f)
        .child(Widget()
            .size(pct(1.0f), px(26))
            .background(Color{ 0.20f, 0.20f, 0.26f, 1.0f })
            .corners(px(8), px(8), px(0), px(0))
            .padding(px(0), px(8))
            .text(title, px(12), Colors::White)
            .onDrag([&x, &y](float dx, float dy) { x += dx; y += dy; }))
        .child(std::move(content)
            .anchor(Anchor::TopLeft, px(0), px(26)));
}

// ── Toast ──────────────────────────────────────────────────────────────
// One entry in a notification queue: message plus how long it has left.
struct Toast {
    std::string message;
    float       secondsLeft = 0.0f;
};

// Ticks every toast, drops the expired ones, and returns a Column of the
// survivors. Call once per frame — the tick and the build are together on
// purpose, so a toast can't be drawn on a frame it should have expired.
inline Widget Toasts(std::vector<Toast>& toasts, float dt,
                     Unit w = px(240), Unit rowH = px(30)) {
    for (auto& t : toasts) t.secondsLeft -= dt;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(),
                                [](const Toast& t) { return t.secondsLeft <= 0.0f; }),
                 toasts.end());

    Widget col = Column().gap(px(6)).setLayer(Layer::Overlay);
    for (const auto& t : toasts) {
        // Fade over the last half second so they leave rather than vanish.
        float a = t.secondsLeft < 0.5f ? t.secondsLeft / 0.5f : 1.0f;
        col.child(Widget()
            .size(w, rowH)
            .background(Color{ 0.10f, 0.12f, 0.16f, 0.95f })
            .border(px(1), Colors::Cyan, px(6))
            .padding(px(0), px(10))
            .opacity(a)
            .text(t.message.c_str(), px(12), Colors::White)
            .align(HAlign::Left, VAlign::Middle));
    }
    return col;
}

} // namespace Dust::UI
