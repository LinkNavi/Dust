#include "DustEngine.hpp"
#include "Log.hpp"

int main() {
    Dust::DustEngine e;
    e.init("DustEngine");
Dust::set_log_level(1);
    Dust::log("Vulkan ", e.vulkan.tier.vulkanMajor, ".", e.vulkan.tier.vulkanMinor, ".", e.vulkan.tier.vulkanPatch);
    Dust::log("Dynamic Rendering: ", e.vulkan.tier.dynamicRendering);
    Dust::log("Mesh Shaders: ",      e.vulkan.tier.meshShaders);
    Dust::log("Ray Tracing: ",       e.vulkan.tier.raytracing);

    e.windows.create({ .name="main", .title="DustEngine", .width=1280, .height=720 }, e.vulkan);
/*
    e.run([&](float dt) {
        // game logic
    });
 */
    e.shutdown();
    return 0;
}
