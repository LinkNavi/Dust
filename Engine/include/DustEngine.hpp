// DustEngine.hpp — the one header you need: #include "DustEngine.hpp" and go.
#pragma once
#include "Core/Window.hpp"
#include "DustECS.hpp"
#include "Core/Systems/Entity.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Renderer.hpp"
#include "Core/Rendering/Camera.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Model.hpp"
#include "Core/UI/Widget.hpp"
#include "AssetManager/AssetManager.hpp"
#include <functional>
#include <list>
#include <string>
#include <glm/glm.hpp>

namespace Dust {

struct DustEngine {
    WindowManager  windows;
    VulkanContext  vulkan;
    Window*        window = nullptr; // set by init() unless createWindow=false

    ecs::Registry  ecs;
    std::list<Entity> entities;
    Entity         root;
    bool           running = true;

    Entity* createEntity(const char* name, Entity* parent = nullptr);

    // Sets up GLFW + Vulkan + ECS, and — unless createWindow is false — also
    // creates the main window and its renderer and points `window` at it.
    // Usually the only setup call you need (raylib's InitWindow, basically).
    bool init(const char* title = "Dust", uint32_t width = 1280, uint32_t height = 720,
              bool createWindow = true);
    void shutdown();

    // Callback-style loop — an alternative to the shouldClose()/beginDrawing()
    // pair below, if you'd rather hand the engine an update function.
    void run(std::function<void(float dt)> onUpdate);
    void stop();

    // ── Raylib-shaped convenience API — operates on `window` ──────────────

    // Polls events and reports whether the window wants to close — use as
    // your while() condition, same role as raylib's WindowShouldClose().
    bool  shouldClose();
    float deltaTime() const;

    // Frame bracket: beginDrawing(); clearBackground(...); ...draws...; endDrawing();
    //
    // clearBackground() is what actually starts the render pass (Vulkan's
    // dynamic rendering needs the clear value up front, unlike raylib's
    // immediate clear), so call it right after beginDrawing() like every
    // raylib example does. If you draw without calling it, drawModel()/
    // endDrawing() start the pass for you using whatever color was last set.
    void beginDrawing();
    void endDrawing();
    void clearBackground(float r, float g, float b);

    // Sets the camera used by drawModel()/drawMesh() calls made until endMode3D().
    void beginMode3D(const Camera& camera);
    void endMode3D();

    // ── Model — loaded assets (OBJ or anything DustPacker ran through
    // assimp: fbx/gltf/glb/dae/stl/ply/3ds/...). One submesh/no material for
    // a plain OBJ, real materials+textures for anything imported — same
    // three calls either way.
    //
    // position/rotationAxis in world units, rotationDeg in degrees — mirrors
    // raylib's DrawModelEx(model, position, rotationAxis, rotationAngle, scale, tint).
    // No tint yet (no per-draw color multiply in the shader).
    void drawModel(Model& model, glm::vec3 position, glm::vec3 rotationAxis,
                   float rotationDeg, glm::vec3 scale = { 1.0f, 1.0f, 1.0f });

    // Loads + uploads in one call. Dispatches on file extension: ".obj" goes
    // through the plain-text loader (wrapped as a single-submesh, no-material
    // Model); anything else is expected to be a DustModel binary produced by
    // DustPacker's assimp importer (packed under "<name>.model").
    Model loadModel(const char* objPath);
    Model loadModelFromPack(AssetManager& assets, const std::string& name);

    // Waits for the GPU to be done with it, then frees it — call before
    // shutdown() for anything returned by loadModel()/loadModelFromPack().
    void unloadModel(Model& model);

    // ── Mesh — raw, procedural, vert-editable geometry (Mesh::makeTriangle(),
    // addVert/addFace, ...). No file loading, no materials — just a mesh and
    // a transform, drawn with the engine's 1x1 white fallback texture so
    // vertex color comes through unmodified. Mesh itself owns its GPU
    // lifecycle (mesh.upload(vulkan) / mesh.updateVertices() / mesh.destroy()
    // — see Mesh.hpp), drawMesh() just issues the draw call.
    void drawMesh(Mesh& mesh, glm::vec3 position, glm::vec3 rotationAxis,
                 float rotationDeg, glm::vec3 scale = { 1.0f, 1.0f, 1.0f });

    // ── DustUI — see DustUI-API.md for the target API, UITimeline.md for
    // what's built so far (Phase 2: layout + solid rounded-rect/border
    // fills + MSDF text; no sprites/custom shaders yet).
    //
    // beginUI() resets and returns the invisible full-viewport root widget —
    // attach top-level widgets to it with .child(...). DustUI-API.md shows
    // bare top-level widget statements registering themselves implicitly;
    // there's no standard-C++ way to hook that (see UITimeline.md), so this
    // is the one deviation from the doc:
    //
    //   auto& ui = e.beginUI();
    //   ui.child(UI::Column().anchor(...)...);
    //   ui.child(UI::Widget().anchor(...)...);
    //   e.endUI();
    //
    // Immediate mode — the whole tree is rebuilt and redrawn every frame,
    // no diffing yet (Phase 4). Draws after 3D content, always on top.
    UI::Widget& beginUI();
    void        endUI();

    // One font, shared by every .text() widget — DustUI-API.md's .text()
    // call has no per-widget font selection, and MSDF means one atlas
    // already renders crisply at any size, so there's nothing to gain yet
    // from supporting more than one loaded font. Dispatches on extension
    // like loadModelFromPack() — expects a DustFont binary (".font",
    // produced by DustPacker's msdfgen importer from a .ttf/.otf).
    bool loadUIFont(AssetManager& assets, const std::string& name);
    void unloadUIFont();

private:
    glm::mat4  activeViewProj{ 1.0f };
    bool       frameValid     = false; // false on a skipped (swapchain-rebuild) frame
    bool       renderingBegun = false; // whether beginRendering() has run this frame
    UI::Widget uiRoot;
    UI::Font   uiFont;
    bool       uiFontLoaded = false;
};

} // namespace Dust
