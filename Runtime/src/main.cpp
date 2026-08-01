// Feature showcase — one of everything the engine currently does, in the
// order goals.txt lists them. Not a real game, just proof each system works
// end to end. Run with `zora run Runtime`.
#include "DustEngine.hpp"
#include "Log.hpp"
#include <cstdio>
#include <cmath>

// Placeholder — packed with `DustPacker Models models.pack <this>`.
// Swap for real key management once that's a thing.
static constexpr const char* kAssetPackKey = "weup";

// Component used in the ECS/Entity demo below.
struct Health { int hp; };

int main() {
    Dust::set_log_level(1); // 0=none 1=info 2=verbose 3=debug

    Dust::DustEngine e;
    if (!e.init("DustEngine — Feature Showcase", 1280, 720)) return 1;

    // ── Device tier ── gate optional features against real hardware support
    auto& tier = e.vulkan.tier;
    Dust::log("GPU tier — Vulkan ", tier.vulkanMajor, ".", tier.vulkanMinor,
              " | dynamic rendering: ", tier.dynamicRendering,
              " | mesh shaders: ", tier.meshShaders,
              " | ray tracing: ", tier.raytracing);

    // ── Entity/ECS — hierarchy + components (Unity-style, logged not drawn) ──
    Dust::Entity* player = e.createEntity("player");
    Dust::Entity* sword  = e.createEntity("sword", player); // child of player
    player->add<Health>({ 100 });
    Dust::log("entity '", player->name, "' hp=", player->get<Health>()->hp,
              " children=", player->children.size());
    Dust::log("entity '", sword->name, "' parent='", sword->parent->name, "'");
    player->remove<Health>();
    Dust::log("'", player->name, "' has<Health>() after remove: ", player->has<Health>());

    // ── Assets ──
    Dust::AssetManager assets;
    if (!assets.open("models.pack", kAssetPackKey)) {
        fprintf(stderr, "dust: failed to open models.pack\n");
        return 1;
    }

    // Mesh API — raw, procedural geometry, no file or Model involved.
    Dust::Mesh triangle = Dust::Mesh::makeTriangle();
    triangle.upload(e.vulkan);

    // Model — loaded assets, one type whether the source was a plain OBJ or
    // anything DustPacker ran through assimp (fbx/gltf/glb/dae/stl/ply/3ds).
    // OBJ loading: single submesh, no material.
    Dust::Model objCube = e.loadModelFromPack(assets, "cube.obj");

    // Full model loading via assimp (offline, in DustPacker) — multi-
    // material/textured models, both packed from arbitrary source formats
    // into one binary (see AssetManager/ModelFormat.hpp).
    Dust::Model checkerCube = e.loadModelFromPack(assets, "checker_cube.model"); // synthetic test asset
    Dust::Model duck        = e.loadModelFromPack(assets, "Duck.model");        // real-world glTF (Models/Duck.glb)
    assets.close();

    // ── Camera ── pulled back far enough to frame all four objects in a row
    Dust::Camera camera;
    camera.position = { 0.0f, 2.0f, 10.0f };
    camera.lookAt({ 0.0f, 0.0f, 0.0f });
    camera.fovDeg = 55.0f;

    float t        = 0.0f;
    float rotation = 0.0f;

    while (!e.shouldClose()) {
        t        += e.deltaTime();
        rotation += e.deltaTime() * 60.0f; // degrees/sec

        // Mesh API — live vert edit, re-uploaded every frame. Proves you can
        // poke .vertSlots directly (deforming meshes, procedural animation)
        // without going through addVert/addFace again. No VulkanContext to
        // thread through here — upload() already stashed it on the mesh.
        objCube.mesh().vertSlots[2].v.position[1] = 0.5f + sinf(t * 3.0f) * 0.3f;
        objCube.mesh().updateVertices();

        e.beginDrawing();
            e.clearBackground(0.05f, 0.05f, 0.05f);
            e.beginMode3D(camera);
                // Spins around Z (its own face normal), not Y — a flat XY
                // triangle rotated around Y goes edge-on twice a revolution,
                // which is correct geometry, just a bad look for a demo.
                e.drawMesh(triangle, { -4.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, rotation);
                e.drawModel(objCube,     { -1.5f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, rotation);
                e.drawModel(checkerCube, {  1.5f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, rotation);
                e.drawModel(duck,        {  4.5f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, rotation);
            e.endMode3D();
        e.endDrawing();
    }

    triangle.destroy();
    e.unloadModel(objCube);
    e.unloadModel(checkerCube);
    e.unloadModel(duck);
    e.shutdown();
    return 0;
}
