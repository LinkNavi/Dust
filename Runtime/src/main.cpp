#include "DustEngine.hpp"
#include "Log.hpp"
#include <cstdio>

// Placeholder — packed with `DustPacker Models models.pack <this>`.
// Swap for real key management once that's a thing.
static constexpr const char* kAssetPackKey = "weup";

int main() {
    Dust::set_log_level(1);

    Dust::DustEngine e;
    if (!e.init("DustEngine", 1280, 720)) return 1;

    Dust::AssetManager assets;
    if (!assets.open("models.pack", kAssetPackKey)) {
        fprintf(stderr, "dust: failed to open models.pack\n");
        return 1;
    }
    Dust::Model cube = e.loadModelFromPack(assets, "cube.obj");
    assets.close();

    Dust::Camera camera;
    camera.position = { 0.0f, 1.5f, 3.0f };
    camera.lookAt({ 0.0f, 0.0f, 0.0f });
    camera.fovDeg = 45.0f;

    float rotation = 0.0f;

    while (!e.shouldClose()) {
        rotation += e.deltaTime() * 60.0f; // degrees/sec, matches the raylib example's +=1 per frame @60fps
        cube.vertSlots[2].v.position[1] += e.deltaTime() * 30.0f;
        cube.updateVertices(e.vulkan); // push the CPU edit above into the GPU buffer

        e.beginDrawing();
            e.clearBackground(0.05f, 0.05f, 0.05f);
            e.beginMode3D(camera);
                e.drawModel(cube, { 0.0f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, rotation);
            e.endMode3D();
        e.endDrawing();
    }

    e.unloadModel(cube);
    e.shutdown();
    return 0;
}
