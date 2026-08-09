// Feature showcase — one of everything the engine currently does, in the
// order goals.txt lists them. Not a real game, just proof each system works
// end to end. Run with `zora run Runtime`.
#include "DustEngine.hpp"
#include "Log.hpp"
#include "Core/UI/PulseShader.hpp"
#include "Core/UI/HealthBarShader.hpp"
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

    // ── Particles ──
    Dust::ParticleSystem ps;
    ps.init(e.vulkan.allocator, e.vulkan.device,
            e.vulkan.graphicsFamily, e.vulkan.graphicsQueue, 5000);

    Dust::Texture sonicTex = e.loadTexture("Models/sonic.png");
    if (!sonicTex.valid()) {
        fprintf(stderr, "dust: sonic.png not found — check working directory\n");
    }
    VkDescriptorSet sonicSet = e.createTextureSet(sonicTex);

    // Burst emit at startup — continuous re-emission happens in the loop below
    for (int i = 0; i < 200; i++) {
        float angle = (float)i / 200.0f * 6.2831f;
        ps.emit({
            .pos   = { 0.0f, 0.0f, 0.0f },
            .life  = 3.0f + (float)(i % 5) * 0.5f,
            .vel   = { cosf(angle) * 2.0f, 4.0f + (float)(i % 3), sinf(angle) * 2.0f },
            .size  = 0.15f,
            .color = { 1.0f, 0.8f, 0.2f, 1.0f },
        });
    }

    // Custom UI shader (UITimeline.md Phase 9) — fragment shader only, pairs
    // with the engine's ui.vert. ui_pulse_frag_spv is baked in by
    // BuildShaders.sh from Engine/Shaders/ui_pulse.frag.
    VkPipeline pulseShader     = e.loadUIShader((const uint32_t*)ui_pulse_frag_spv, ui_pulse_frag_spv_len);
    VkPipeline healthBarShader = e.loadUIShader((const uint32_t*)ui_healthbar_frag_spv, ui_healthbar_frag_spv_len);

    // ── Camera ── pulled back far enough to frame all four objects in a row
    Dust::Camera camera;
    camera.position = { 0.0f, 2.0f, 10.0f };
    camera.lookAt({ 0.0f, 0.0f, 0.0f });
    camera.fovDeg = 55.0f;

    float t        = 0.0f;
    float rotation = 0.0f;

    // DustUI input demo state (UITimeline.md Phase 3) — persists across
    // frames on purpose, same reason DustEngine keeps uiHoveredId/
    // uiFocusedId itself: the widget tree is rebuilt from scratch every
    // frame, so nothing here can live on a Widget instance.
    int         activeSlot  = 0;     // which hotbar slot .onClick() last selected
    int         hoveredSlot = -1;    // which slot .onHover() saw this frame, -1 = none
    int         selectedRow = 0;     // which row of the clipped list was last clicked
    int         sinkChoice  = 0;     // which kitchen-sink Button is active
    bool        chatFocused = false; // whether the chat box currently holds keyboard focus
    std::string chatText;

    // Component demo state — all caller-owned, since DustUI is immediate mode
    // and a component that stored this itself couldn't be rebuilt each frame.
    int   activeTab   = 0;
    int   dropdownSel = 0;
    bool  dropdownOpen = false;
    bool  modalOpen    = false;
    float panelX = 340.0f, panelY = 120.0f; // dragged window position
    std::vector<Dust::UI::Toast> toasts;
    static const char* kOptions[] = { "Fireball", "Ice Shard", "Chain Lightning", "Meteor", "Heal" };
    static const char* kTabs[]    = { "Stats", "Gear", "Skills" };

    while (!e.shouldClose()) {
        t        += e.deltaTime();
        rotation += e.deltaTime() * 60.0f; // degrees/sec

        // Dispatch particle compute before the graphics pass
        e.dispatchParticles(ps, e.deltaTime());

        // Trickle-emit a few particles per frame so the system stays populated
        for (int i = 0; i < 5; i++) {
            float angle = t * 3.0f + (float)i * 1.2566f; // evenly spread
            ps.emit({
                .pos   = { 0.0f, 0.0f, 0.0f },
                .life  = 3.0f,
                .vel   = { cosf(angle) * 2.0f, 4.0f + (float)i * 0.4f, sinf(angle) * 2.0f },
                .size  = 0.15f,
                .color = { 1.0f, 0.8f, 0.2f, 1.0f },
            });
        }

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
                e.drawParticles(ps, sonicSet);
            e.endMode3D();

            // DustUI — Phase 2 (see UITimeline.md): layout + solid rounded-
            // rect/border fills + real MSDF text.
            float playerHpPct = 0.55f + 0.45f * (sinf(t) * 0.5f + 0.5f); // animated, proves it's live per-frame

            auto& ui = e.beginUI();

            // Z-ordering + layers (Phases 8/10) — declared *before* the
            // widgets it covers, but Layer::Overlay sorts it last, so it
            // draws over them. Without it this would be painted first and
            // vanish under the hotbar.
            ui.child(Dust::UI::Widget()
                .size(Dust::px(220), Dust::px(28))
                .background(Dust::Color{ 0.1f, 0.1f, 0.1f, 0.85f })
                .border(Dust::px(1), Dust::Colors::Gold, Dust::px(6))
                .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(-120))
                .padding(Dust::px(0), Dust::px(10))
                .setLayer(Dust::UI::Layer::Overlay)
                .text("Overlay layer — draws on top", Dust::px(12), Dust::Colors::White));

            // Health Bar — a dog-leg bar (------\________) drawn entirely by
            // ui_healthbar.frag on one quad. A nested-widget version can't do
            // this: the fill would have to be the same rectangle as the
            // track. Params are fill fraction, where the bend starts (0..1
            // across the width), thickness px, drop px. .background() is the
            // filled colour, the border colour is the empty track.
            ui.child(Dust::UI::Widget()
                .size(Dust::px(260), Dust::px(38))
                .background(Dust::Colors::Red)
                .border(Dust::px(0), Dust::Colors::DarkRed, Dust::px(0))
                .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(-70))
                .shader(healthBarShader, playerHpPct, 0.5f, 10.0f, 18.0f));

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
            // HotbarSlot example. Click selects a slot, hover brightens the
            // border — colors below reflect *last* frame's hoveredSlot/
            // activeSlot (hoveredSlot is reset just before endUI() so a
            // slot that stops being hovered doesn't stay lit).
            Dust::UI::Widget hotbar = Dust::UI::Row()
                .gap(Dust::px(4))
                .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(-24)); // margin so it isn't flush against the window edge
            for (int i = 0; i < 6; i++) {
                bool active  = (i == activeSlot);
                bool hovered = (i == hoveredSlot);
                Dust::Color borderCol = hovered ? Dust::Colors::White : (active ? Dust::Colors::Gold : Dust::Colors::Gray);
                hotbar.child(Dust::UI::Stack()
                    .size(Dust::px(48), Dust::px(48))
                    .background(active ? Dust::Colors::DarkGold : Dust::Colors::DarkGray)
                    .border(Dust::px(2), borderCol, Dust::px(4))
                    .onClick([&activeSlot, i]() { activeSlot = i; })
                    .onHover([&hoveredSlot, i]() { hoveredSlot = i; })
                    .child(Dust::UI::Widget()
                        .anchor(Dust::UI::Anchor::BottomRight, Dust::px(-4), Dust::px(-4))
                        .text(std::to_string(i + 1).c_str(), Dust::px(11), Dust::Colors::LightGray)));
            }
            ui.child(hotbar);

            // Kitchen sink — one panel exercising the widget features that
            // aren't a whole phase on their own: gradient fill, drop shadow,
            // per-corner radii, text alignment/wrapping/outline, margins,
            // justify/align on a Row, and the Panel/Label/Button/ProgressBar
            // component helpers.
            ui.child(Dust::UI::Panel(Dust::Color{ 0.10f, 0.11f, 0.14f, 0.94f })
                .size(Dust::px(300), Dust::px(250))
                .anchor(Dust::UI::Anchor::CenterLeft, Dust::px(20), Dust::px(0))
                .corners(Dust::px(16), Dust::px(4), Dust::px(16), Dust::px(4))
                .shadow(Dust::Colors::Black.alpha(0.6f), 10.0f, 0.0f, 4.0f)
                .padding(Dust::px(12))
                .child(Dust::UI::Column()
                    .gap(Dust::px(8))
                    // Text gradient runs across the whole string, not per
                    // glyph — 90deg is CSS's left→right.
                    .child(Dust::UI::Label("Kitchen Sink", Dust::px(20))
                        .align(Dust::UI::HAlign::Center)
                        .size(Dust::pct(1.0f), Dust::px(26))
                        .textGradient(Dust::Colors::Gold, Dust::Colors::Cyan, Dust::UI::degrees(90.0f))
                        .textOutline(Dust::px(2.0f), Dust::Colors::Black))
                    // Gradient + wrapped, justified body text.
                    .child(Dust::UI::Widget()
                        .size(Dust::pct(1.0f), Dust::px(66))
                        .gradient(Dust::Colors::DarkBlue, Dust::Colors::Purple, Dust::UI::degrees(135.0f))
                        .border(Dust::px(2), Dust::Colors::Gray, Dust::px(6))
                        .borderGradient(Dust::Colors::Cyan, Dust::Colors::Purple, Dust::UI::degrees(45.0f))
                        .padding(Dust::px(6))
                        .text("Gradient fill, word wrap, and a per-glyph outline all on one widget.",
                              Dust::px(12), Dust::Colors::White)
                        .wrap()
                        .align(Dust::UI::HAlign::Left, Dust::UI::VAlign::Top)
                        .textOutline(Dust::px(1.0f), Dust::Colors::Black))
                    // Row with SpaceBetween + centred cross axis, and a
                    // margin on the middle child pushing it off its neighbours.
                    .child(Dust::UI::Row()
                        .size(Dust::pct(1.0f), Dust::px(34))
                        .justifyContent(Dust::UI::Justify::SpaceBetween)
                        .align(Dust::UI::AlignItems::Center)
                        .child(Dust::UI::Button("One", [&](){ sinkChoice = 0; }, sinkChoice == 0, Dust::px(74), Dust::px(28)))
                        .child(Dust::UI::Button("Two", [&](){ sinkChoice = 1; }, sinkChoice == 1, Dust::px(74), Dust::px(28))
                            .margin(Dust::px(0), Dust::px(6)))
                        .child(Dust::UI::Button("Three", [&](){ sinkChoice = 2; }, sinkChoice == 2, Dust::px(74), Dust::px(28))))
                    .child(Dust::UI::ProgressBar(playerHpPct, Dust::pct(1.0f), Dust::px(14)))
                    // Pill via per-corner radii, right-aligned text.
                    .child(Dust::UI::Widget()
                        .size(Dust::pct(1.0f), Dust::px(24))
                        .background(Dust::Colors::DarkGold)
                        .corners(Dust::px(12), Dust::px(12), Dust::px(12), Dust::px(12))
                        .padding(Dust::px(0), Dust::px(10))
                        .text("right-aligned pill", Dust::px(12), Dust::Colors::White)
                        .align(Dust::UI::HAlign::Right))));

            // Sprite (Phase 5) — a texture inside a widget rect, sharing the
            // descriptor set the particle system already uses. No
            // .background() means no tint; the border/radius still come from
            // the same rounded-rect SDF, so sprites round off with the box.
            ui.child(Dust::UI::Widget()
                .size(Dust::px(96), Dust::px(96))
                .border(Dust::px(2), Dust::Colors::White, Dust::px(12))
                .anchor(Dust::UI::Anchor::TopLeft, Dust::px(20), Dust::px(20))
                .sprite(sonicSet));

            // Clipping + scrolling (Phases 6/7) — a fixed-size window over a
            // Column taller than it is. Mouse wheel over it scrolls; rows get
            // cut off mid-glyph at both edges (the clip applies to text as
            // well as fills), and a clipped-away row can't be clicked.
            Dust::UI::Widget list = Dust::UI::Column().gap(Dust::px(4));
            for (int i = 0; i < 8; i++) {
                bool sel = (i == selectedRow);
                list.child(Dust::UI::Widget()
                    .size(Dust::px(160), Dust::px(26))
                    .background(sel ? Dust::Colors::DarkGold : Dust::Colors::DarkGray)
                    .border(Dust::px(1), Dust::Colors::Gray, Dust::px(3))
                    .padding(Dust::px(0), Dust::px(8))
                    .onClick([&selectedRow, i]() { selectedRow = i; })
                    .text(("Item " + std::to_string(i + 1)).c_str(), Dust::px(13), Dust::Colors::White));
            }
            ui.child(Dust::UI::Widget()
                .size(Dust::px(176), Dust::px(120))
                .background(Dust::Colors::Black)
                .border(Dust::px(2), Dust::Colors::White, Dust::px(6))
                .anchor(Dust::UI::Anchor::CenterRight, Dust::px(-20), Dust::px(0))
                .padding(Dust::px(8))
                .scroll()
                .child(list));

            // Custom shader widget (Phase 9) — same instance buffer, same
            // batch path, different fragment shader. params: time, speed,
            // ring count. fill/border act as the two gradient endpoints.
            if (pulseShader != VK_NULL_HANDLE) {
                ui.child(Dust::UI::Widget()
                    .size(Dust::px(110), Dust::px(110))
                    .background(Dust::Colors::Cyan)
                    .border(Dust::px(0), Dust::Colors::DarkGold, Dust::px(55))
                    .anchor(Dust::UI::Anchor::TopRight, Dust::px(-20), Dust::px(20))
                    .shader(pulseShader, t, 4.0f, 3.0f));
            }

            // Chat box — now the TextInput component. Same "movement vs. text
            // box" proof as before: click it to focus, then type. While
            // focused, isKeyDown()/isKeyPressed() report no keys at all by
            // default, so the simulated "player movement" below goes quiet
            // even though WASD is still physically held.
            ui.child(Dust::UI::TextInput(chatText, chatFocused,
                                         "Click, then type (WASD works normally until you do)",
                                         Dust::px(320), Dust::px(30))
                .anchor(Dust::UI::Anchor::BottomLeft, Dust::px(10), Dust::px(-24)));

            // Tooltip — built only on the frames it should be visible, which
            // is immediate mode's answer to show/hide. Overlay layer, so it
            // floats over the hotbar it describes.
            if (hoveredSlot >= 0) {
                ui.child(Dust::UI::Tooltip(("Slot " + std::to_string(hoveredSlot + 1) + " — click to equip").c_str())
                    .size(Dust::px(210), Dust::px(26))
                    .anchor(Dust::UI::Anchor::BottomCenter, Dust::px(0), Dust::px(-140)));
            }

            // Draggable window — the title bar's .onDrag() adds the frame's
            // mouse delta to the caller's x/y. Contains a tabbed panel.
            {
                Dust::UI::Widget body = Dust::UI::Column().size(Dust::pct(1.0f), Dust::px(120)).padding(Dust::px(8));
                body.child(Dust::UI::Tabs(kTabs, 3, activeTab, Dust::px(64), Dust::px(24)));
                const char* tabBody = activeTab == 0 ? "STR 14   DEX 11   INT 18"
                                    : activeTab == 1 ? "Sword of Testing +2"
                                                     : "Fireball, Blink, Mend";
                body.child(Dust::UI::Widget()
                    .size(Dust::pct(1.0f), Dust::px(60))
                    .background(Dust::Colors::Black.alpha(0.35f))
                    .corners(Dust::px(0), Dust::px(6), Dust::px(6), Dust::px(6))
                    .padding(Dust::px(8))
                    .text(tabBody, Dust::px(12), Dust::Colors::LightGray)
                    .wrap()
                    .align(Dust::UI::HAlign::Left, Dust::UI::VAlign::Top));
                ui.child(Dust::UI::Draggable("Drag me", panelX, panelY, std::move(body),
                                             Dust::px(230), Dust::px(150)));
            }

            // Dropdown + image button + the button that opens the modal.
            ui.child(Dust::UI::Row()
                .gap(Dust::px(8))
                .align(Dust::UI::AlignItems::Center)
                .anchor(Dust::UI::Anchor::TopRight, Dust::px(-20), Dust::px(150))
                .child(Dust::UI::ImageButton(sonicSet, [&]() {
                        toasts.push_back({ "Sprite button clicked", 2.5f });
                    }, Dust::px(40)))
                .child(Dust::UI::Dropdown(kOptions, 5, dropdownSel, dropdownOpen, Dust::px(150)))
                .child(Dust::UI::Button("Modal", [&]() { modalOpen = true; }, false,
                                        Dust::px(80), Dust::px(26))));

            // Toasts — ticked and rebuilt in one call so one can't outlive
            // its timer by a frame.
            ui.child(Dust::UI::Toasts(toasts, e.deltaTime())
                .anchor(Dust::UI::Anchor::TopCenter, Dust::px(0), Dust::px(60)));

            // Modal — the scrim's .blockInput() makes everything drawn before
            // it unclickable, so nothing behind needs to know it exists.
            if (modalOpen) {
                ui.child(Dust::UI::ModalDialog(
                    "Confirm", "Input to everything behind this dialog is blocked by the scrim, not by the caller.",
                    "Got it", [&]() { modalOpen = false; toasts.push_back({ "Modal dismissed", 2.0f }); }));
            }

            hoveredSlot = -1; // see comment above — must run after the widgets that read it, before endUI()'s hit-test can set it fresh
            chatFocused = false; // same idea: endUI() sets this back to true this frame iff the chat box is still actually focused

            e.endUI();

            // Everything below reads input *after* endUI() so uiWantsKeyboard()/
            // uiWantsMouse() reflect this frame's freshly-computed hit-test,
            // not last frame's.
            e.editFocusedText(chatText); // no-op unless a focusable widget holds focus
            if (e.isKeyPressed(GLFW_KEY_W)) Dust::log("(sim) player moves forward — uiWantsKeyboard()=", e.uiWantsKeyboard());
        e.endDrawing();
    }

    triangle.destroy();
    e.unloadModel(objCube);
    e.unloadModel(checkerCube);
    e.unloadModel(duck);
    ps.cleanup();
    e.unloadTexture(sonicTex);
    e.shutdown();
    return 0;
}
