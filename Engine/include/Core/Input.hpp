#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>
#include <string>

namespace Dust {

// Raw, per-window hardware input state — populated by GLFW callbacks (see
// Window.cpp), not polling, so fast taps between frames aren't missed.
// This is the *unfiltered* layer: DustUI's own hit-testing needs to see
// real input regardless of what it's about to claim, so it reads this
// directly. Game code should generally go through DustEngine's query API
// instead (Core/Input.hpp only, not DustEngine.hpp) — that layer is capture-
// aware (see UITimeline.md Phase 3) so a focused text widget doesn't leak
// keystrokes into player movement.
struct InputState {
    // true for every frame the key/button is held down.
    bool keysDown[GLFW_KEY_LAST + 1] = {};
    bool mouseDown[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    // true for exactly one frame — the one the transition happened on.
    // Cleared and re-derived each frame in Window::beginInputFrame().
    bool keysPressed[GLFW_KEY_LAST + 1] = {};
    bool keysReleased[GLFW_KEY_LAST + 1] = {};
    bool mousePressed[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    bool mouseReleased[GLFW_MOUSE_BUTTON_LAST + 1] = {};

    double mouseX = 0.0, mouseY = 0.0;
    double mouseDeltaX = 0.0, mouseDeltaY = 0.0; // since last frame
    double scrollX = 0.0, scrollY = 0.0;         // accumulated this frame

    // UTF-8 text typed this frame (from GLFW's char callback, which already
    // handles layout/shift/dead-keys) — cleared every frame. Meant for
    // whatever widget currently holds keyboard focus; not filtered by
    // capture state itself since it's only meaningful to its consumer.
    std::string textInput;

    // Internal bookkeeping for beginInputFrame()'s delta computation — not
    // meant to be read directly (mouseDeltaX/Y above already do the work).
    double prevMouseX = 0.0, prevMouseY = 0.0;
    bool   mouseInitialized = false; // avoids a large first-frame delta jump
};

// Registers GLFW callbacks on `handle` that write into `*state`. Called once
// per window, right after glfwCreateWindow — see WindowManager::create().
void installInputCallbacks(GLFWwindow* handle, InputState* state);

// Called once per window per frame, before glfwPollEvents(): clears the
// edge-triggered arrays and per-frame accumulators (scroll, textInput) so
// this frame starts clean, and captures the mouse-delta baseline.
void beginInputFrame(InputState& state);

} // namespace Dust
