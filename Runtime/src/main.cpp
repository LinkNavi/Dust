#include "DustEngine.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Camera.hpp"
#include "AssetManager/AssetManager.hpp"
#include "Log.hpp"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

// Placeholder — packed with `DustPacker Models assets.pack <this>`.
// Swap for real key management once that's a thing.
static constexpr const char* kAssetPackKey = "weup";

int main() {
    Dust::set_log_level(1);
    Dust::DustEngine e;
    e.init("DustEngine");

    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE); // must come after e.init() (glfwInit)

    Dust::log("Vulkan ", e.vulkan.tier.vulkanMajor, ".", e.vulkan.tier.vulkanMinor, ".", e.vulkan.tier.vulkanPatch);
    Dust::log("Dynamic Rendering: ", e.vulkan.tier.dynamicRendering);
    Dust::log("Mesh Shaders: ",      e.vulkan.tier.meshShaders);
    Dust::log("Ray Tracing: ",       e.vulkan.tier.raytracing);

    e.windows.create({ .name="main", .title="DustEngine", .width=1280, .height=720 }, e.vulkan);
    Dust::Window* w = e.windows.get("main");
    w->renderer.init(e.vulkan, w->swapchain);
    w->setClearColor(0.05f, 0.05f, 0.05f);

    Dust::AssetManager assets;
    if (!assets.open("models.pack", kAssetPackKey)) {
        fprintf(stderr, "dust: failed to open assets.pack\n");
        return 1;
    }

    Dust::Mesh cube;
    {
        Dust::AssetHandle h = assets.load("cube.obj");
        if (!Dust::valid(h)) {
            fprintf(stderr, "dust: cube.obj not found in assets.pack\n");
            return 1;
        }
        const std::vector<uint8_t>* bytes = assets.data(h);
        cube = Dust::Mesh::loadOBJFromMemory(bytes->data(), bytes->size());
        assets.release(h); // GPU upload below copies it off; don't need to keep decoded bytes around
    }
    cube.upload(e.vulkan);

    Dust::Camera camera;
    camera.position = { 0.0f, 0.0f, 3.0f }; // looking at the cube down -Z

    float spin = 0.0f;

    e.run([&](float dt) {
        if (!w->renderer.beginFrame(e.vulkan, *w)) return;
        w->renderer.beginRendering(e.vulkan, *w);

        spin += dt; // radians/sec, tumbles on a tilted axis so all faces read
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), spin, glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f)));

        float aspect = (float)w->width / (float)(w->height ? w->height : 1);
        glm::mat4 mvp = camera.viewProj(aspect) * model;

        w->renderer.draw(w->renderer.cmd(), cube,
                         w->renderer.defaultPipeline,
                         w->renderer.defaultLayout,
                         &mvp[0][0]);

        w->renderer.endRendering();
        w->renderer.endFrame(e.vulkan, *w);
    });

    vkDeviceWaitIdle(e.vulkan.device); // GPU may still be using cube's buffers from the last frame
    cube.destroy(e.vulkan);
    assets.close();
    e.shutdown();
    return 0;
}
