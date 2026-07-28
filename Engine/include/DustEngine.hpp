// DustEngine.hpp — the one header you need: #include "DustEngine.hpp" and go.
#pragma once
#include "Core/Window.hpp"
#include "DustECS.hpp"
#include "Core/Systems/Entity.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Renderer.hpp"
#include "Core/Rendering/Camera.hpp"
#include "Core/Rendering/Mesh.hpp"
#include "AssetManager/AssetManager.hpp"
#include <functional>
#include <list>
#include <string>
#include <glm/glm.hpp>

namespace Dust {

// A Model is just a Mesh — friendlier name for the raylib-shaped API below.
using Model = Mesh;

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

    // Sets the camera used by drawModel() calls made until endMode3D().
    void beginMode3D(const Camera& camera);
    void endMode3D();

    // position/rotationAxis in world units, rotationDeg in degrees — mirrors
    // raylib's DrawModelEx(model, position, rotationAxis, rotationAngle, scale, tint).
    // No tint yet (no per-draw color multiply in the shader), and no depth
    // buffer yet, so overlapping models don't sort — fine for one model.
    void drawModel(Model& model, glm::vec3 position, glm::vec3 rotationAxis,
                   float rotationDeg, glm::vec3 scale = { 1.0f, 1.0f, 1.0f });

    // Loads + uploads in one call.
    Model loadModel(const char* objPath);
    Model loadModelFromPack(AssetManager& assets, const std::string& name);

    // Waits for the GPU to be done with it, then frees it — call before
    // shutdown() for anything returned by loadModel()/loadModelFromPack().
    void unloadModel(Model& model);

private:
    glm::mat4 activeViewProj{ 1.0f };
    bool      frameValid     = false; // false on a skipped (swapchain-rebuild) frame
    bool      renderingBegun = false; // whether beginRendering() has run this frame
};

} // namespace Dust
