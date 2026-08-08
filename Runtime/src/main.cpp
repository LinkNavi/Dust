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

    // DustUI text — MSDF atlas baked offline by DustPacker (msdfgen), one
    // atlas renders crisply at every size .text() asks for. See
    // Models/NotoSans-LICENSE.txt (Apache 2.0) for the font's license.
    if (!e.loadUIFont(assets, "NotoSans-Regular.font"))
        fprintf(stderr, "dust: failed to load UI font — text widgets will render as empty boxes\n");

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

            // DustUI — Phase 2 (see UITimeline.md): layout + solid rounded-
            // rect/border fills + real MSDF text.
            float playerHpPct = 0.55f + 0.45f * (sinf(t) * 0.5f + 0.5f); // animated, proves it's live per-frame

            auto& ui = e.beginUI();

            // Health Bar — DustUI-API.md "Real Examples", plus a label to
            // exercise text sitting inside a Row alongside another widget.
            ui.child(Dust::UI::Row()
                .size(Dust::px(200), Dust::px(20))
                .background(Dust::Colors::DarkRed)
                .border(Dust::px(2), Dust::Colors::White, Dust::px(4))
                .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(-70))
                .child(Dust::UI::Widget()
                    .size(Dust::pct(playerHpPct), Dust::pct(1.0f))
                    .background(Dust::Colors::Red)));

            // Name Tag — real text now, centered in the box by endUI()'s
            // vertical-centering rule (see DustEngine::endUI()).
            ui.child(Dust::UI::Widget()
                .size(Dust::px(120), Dust::px(30))
                .background(Dust::Colors::DarkGray)
                .border(Dust::px(2), Dust::Colors::White, Dust::px(5))
                .anchor(Dust::UI::Anchor::TopCenter, Dust::px(0), Dust::px(20))
                .padding(Dust::px(0), Dust::px(12))
                .text("Kirby", Dust::px(14), Dust::Colors::White));

            // Big text, same atlas as the 14px name tag above — proves one
            // MSDF bake renders crisply at very different sizes, not just
            // the size it happened to be baked at (48px here).
            ui.child(Dust::UI::Widget()
                .size(Dust::px(500), Dust::px(80))
                .background(Dust::Colors::Black)
                .anchor(Dust::UI::Anchor::Center, Dust::px(0), Dust::px(-150))
                .padding(Dust::px(0), Dust::px(12))
                .text("Quick Fox Jumps", Dust::px(48), Dust::Colors::White));

            // Hotbar — Stack per slot (background + number label), Row to
            // lay the slots out with a gap, matching DustUI-API.md's
            // HotbarSlot example.
            Dust::UI::Widget hotbar = Dust::UI::Row()
                .gap(Dust::px(4))
                .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(0));
            for (int i = 0; i < 6; i++) {
                bool active = (i == 0);
                hotbar.child(Dust::UI::Stack()
                    .size(Dust::px(48), Dust::px(48))
                    .background(active ? Dust::Colors::DarkGold : Dust::Colors::DarkGray)
                    .border(Dust::px(2), active ? Dust::Colors::Gold : Dust::Colors::Gray, Dust::px(4))
                    .child(Dust::UI::Widget()
                        .anchor(Dust::UI::Anchor::BottomRight, Dust::px(0), Dust::px(0))
                        .text(std::to_string(i + 1).c_str(), Dust::px(11), Dust::Colors::LightGray)));
            }
            ui.child(hotbar);

            e.endUI();
        e.endDrawing();
    }

    triangle.destroy();
    e.unloadModel(objCube);
    e.unloadModel(checkerCube);
    e.unloadModel(duck);
    e.shutdown();
    return 0;
}
