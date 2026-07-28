#include "DustEngine.hpp"
#include "Log.hpp"

int main() {
    Dust::set_log_level(1);

    Dust::DustEngine e;
    e.init("DustEngine");

    Dust::log("Vulkan ", e.vulkan.tier.vulkanMajor, ".", e.vulkan.tier.vulkanMinor, ".", e.vulkan.tier.vulkanPatch);
    Dust::log("Dynamic Rendering: ", e.vulkan.tier.dynamicRendering);
    Dust::log("Mesh Shaders: ",      e.vulkan.tier.meshShaders);
    Dust::log("Ray Tracing: ",       e.vulkan.tier.raytracing);

    e.windows.create({ .name="main", .title="DustEngine", .width=1280, .height=720 }, e.vulkan);

    Dust::Window* w = e.windows.get("main");
    w->renderer.init(e.vulkan, w->swapchain);
    w->setClearColor(0.05f, 0.05f, 0.05f);

    e.run([&](float dt) {
        if (!w->renderer.beginFrame(e.vulkan, *w)) return;
        w->renderer.beginRendering(e.vulkan, *w);
        // draw calls go here
        w->renderer.endRendering();
        w->renderer.endFrame(e.vulkan, *w);
    });

    e.shutdown();
    return 0;
}
