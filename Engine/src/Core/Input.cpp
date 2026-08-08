#include "Core/Input.hpp"

namespace Dust {

namespace {

void appendUtf8(std::string& out, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        out += (char)codepoint;
    } else if (codepoint <= 0x7FF) {
        out += (char)(0xC0 | (codepoint >> 6));
        out += (char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += (char)(0xE0 | (codepoint >> 12));
        out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out += (char)(0x80 | (codepoint & 0x3F));
    } else {
        out += (char)(0xF0 | (codepoint >> 18));
        out += (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out += (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out += (char)(0x80 | (codepoint & 0x3F));
    }
}

void keyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key < 0 || key > GLFW_KEY_LAST) return; // GLFW_KEY_UNKNOWN and friends
    auto* state = (InputState*)glfwGetWindowUserPointer(window);
    if (!state) return;

    if (action == GLFW_PRESS) {
        state->keysDown[key]    = true;
        state->keysPressed[key] = true;
    } else if (action == GLFW_RELEASE) {
        state->keysDown[key]     = false;
        state->keysReleased[key] = true;
    }
    // GLFW_REPEAT: keysDown is already true, nothing else to update.
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    auto* state = (InputState*)glfwGetWindowUserPointer(window);
    if (!state) return;

    if (action == GLFW_PRESS) {
        state->mouseDown[button]    = true;
        state->mousePressed[button] = true;
    } else if (action == GLFW_RELEASE) {
        state->mouseDown[button]     = false;
        state->mouseReleased[button] = true;
    }
}

void cursorPosCallback(GLFWwindow* window, double x, double y) {
    auto* state = (InputState*)glfwGetWindowUserPointer(window);
    if (!state) return;

    // GLFW reports cursor position in *screen/logical* coordinates, but
    // window->width/height (and therefore every DustUI rect, and the
    // viewport/scissor the renderer sets up) are *framebuffer* pixels —
    // see WindowManager::updateAll(), which sources them from
    // glfwGetFramebufferSize(). On an unscaled display these are the same
    // numbers and this is a no-op; on a HiDPI/fractional-scale display
    // they're not (e.g. a 1280x720 logical window backed by a 1920x1080
    // framebuffer at 1.5x), and every hit-test would silently compare
    // mouse coordinates against widget rects in two different coordinate
    // spaces — clicks land nowhere near where the widget actually is.
    int winW = 0, winH = 0, fbW = 0, fbH = 0;
    glfwGetWindowSize(window, &winW, &winH);
    glfwGetFramebufferSize(window, &fbW, &fbH);

    double scaleX = (winW > 0) ? (double)fbW / (double)winW : 1.0;
    double scaleY = (winH > 0) ? (double)fbH / (double)winH : 1.0;

    state->mouseX = x * scaleX;
    state->mouseY = y * scaleY;
}

void scrollCallback(GLFWwindow* window, double dx, double dy) {
    auto* state = (InputState*)glfwGetWindowUserPointer(window);
    if (!state) return;
    state->scrollX += dx;
    state->scrollY += dy;
}

void charCallback(GLFWwindow* window, unsigned int codepoint) {
    auto* state = (InputState*)glfwGetWindowUserPointer(window);
    if (!state) return;
    appendUtf8(state->textInput, codepoint);
}

} // namespace

void installInputCallbacks(GLFWwindow* handle, InputState* state) {
    glfwSetWindowUserPointer(handle, state);
    glfwSetKeyCallback(handle, keyCallback);
    glfwSetMouseButtonCallback(handle, mouseButtonCallback);
    glfwSetCursorPosCallback(handle, cursorPosCallback);
    glfwSetScrollCallback(handle, scrollCallback);
    glfwSetCharCallback(handle, charCallback);
}

void beginInputFrame(InputState& state) {
    for (auto& v : state.keysPressed)   v = false;
    for (auto& v : state.keysReleased)  v = false;
    for (auto& v : state.mousePressed)  v = false;
    for (auto& v : state.mouseReleased) v = false;
    state.scrollX = 0.0;
    state.scrollY = 0.0;
    state.textInput.clear();

    if (state.mouseInitialized) {
        state.mouseDeltaX = state.mouseX - state.prevMouseX;
        state.mouseDeltaY = state.mouseY - state.prevMouseY;
    } else {
        state.mouseDeltaX = 0.0;
        state.mouseDeltaY = 0.0;
        state.mouseInitialized = true;
    }
    state.prevMouseX = state.mouseX;
    state.prevMouseY = state.mouseY;
}

} // namespace Dust
