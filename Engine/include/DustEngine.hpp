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
#include "Core/Rendering/Texture.hpp"
#include "Core/Rendering/ParticleSystem.hpp"
#include "Core/UI/Widget.hpp"
#include "Core/UI/Components.hpp"
#include "AssetManager/AssetManager.hpp"
#include <functional>
#include <list>
#include <unordered_map>
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

    // ── Textures — load a PNG/JPG/BMP/TGA from disk and upload to GPU.
    // Pass the returned Texture to drawBillboard() or drawParticles() as the
    // material. Call unloadTexture() before shutdown().
    Texture loadTexture(const char* path);
    void    unloadTexture(Texture& tex);

    // Create a VkDescriptorSet for a Texture so it can be passed to
    // drawBillboard() / drawParticles(). The returned set is owned by the
    // material pool — free it with vkFreeDescriptorSets when done, or just
    // let shutdown() destroy the pool.
    VkDescriptorSet createTextureSet(const Texture& tex);

    // ── Billboards — single camera-facing quad with a texture.
    // Call inside beginDrawing()/endDrawing() after beginMode3D().
    // size is world-space diameter. color is an RGBA tint (default white).
    void drawBillboard(glm::vec3 position, float size,
                       VkDescriptorSet texSet,
                       glm::vec4 color = glm::vec4(1.0f));

    // ── Particles — draw all slots in ps as instanced billboard quads.
    // Dead particles (life <= 0) are collapsed to zero size on the GPU — no
    // CPU bookkeeping needed. Call after beginMode3D() each frame; the compute
    // shader runs physics before this draw is submitted.
    // dispatchParticles() must be called before beginDrawing() to simulate.
    void drawParticles(ParticleSystem& ps, VkDescriptorSet texSet = VK_NULL_HANDLE);

    // Dispatch the compute shader to simulate ps for dt seconds.
    // Call this BEFORE beginDrawing() — compute runs on the graphics queue
    // before the graphics pass begins.
    void dispatchParticles(ParticleSystem& ps, float dt,
                           float gravityY = -9.8f, float drag = 0.01f,
                           VkPipeline computePipeline  = VK_NULL_HANDLE,
                           VkPipelineLayout computeLayout = VK_NULL_HANDLE);

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

    // Builds a custom UI shader (UITimeline.md Phase 9) from SPIR-V and
    // returns the pipeline handle to hand to Widget::shader(). The bytes are
    // a *fragment* shader only — it pairs with the stock ui.vert and must
    // declare the same input interface (see Shaders/ui_pulse.frag for a
    // working example, embedded as ui_pulse_frag_spv). Returns
    // VK_NULL_HANDLE on failure; the renderer owns and destroys the pipeline.
    VkPipeline loadUIShader(const uint32_t* fragSpv, size_t fragSpvLen);

    // Applies this frame's typed characters and backspaces to `buffer` if a
    // focusable widget currently holds keyboard focus. Call after endUI() —
    // that's what computes the focus for this frame. maxLength 0 = unbounded.
    // UI::TextInput() pairs with this; see Core/UI/Components.hpp.
    void editFocusedText(std::string& buffer, size_t maxLength = 0);

    // One font, shared by every .text() widget — DustUI-API.md's .text()
    // call has no per-widget font selection, and MSDF means one atlas
    // already renders crisply at any size, so there's nothing to gain yet
    // from supporting more than one loaded font. Dispatches on extension
    // like loadModelFromPack() — expects a DustFont binary (".font",
    // produced by DustPacker's msdfgen importer from a .ttf/.otf).
    bool loadUIFont(AssetManager& assets, const std::string& name);
    void unloadUIFont();

    // ── Input — one system, shared by the engine and DustUI (UITimeline.md
    // Phase 3). Key/button codes are plain GLFW constants (GLFW_KEY_W,
    // GLFW_MOUSE_BUTTON_LEFT, ...) — consistent with the rest of Dust not
    // hiding its backends (VulkanContext exposes raw vk-bootstrap types the
    // same way).
    //
    // Capture-aware by default: while a focusable UI widget holds keyboard
    // focus (e.g. a text box — see Widget::onFocus()), isKeyDown()/
    // isKeyPressed()/isKeyReleased() report *no* keys down at all, so typing
    // "w" into a name field doesn't also walk your player character
    // forward. Same idea for the mouse — while the cursor is over an
    // interactive widget (Widget::onClick()/onHover()/onFocus()),
    // isMouseButtonDown() and friends go quiet and mouseDelta() reports
    // zero, so dragging a UI slider doesn't also spin a free-look camera.
    // Pass ignoreUICapture=true to see the real state anyway (e.g. a pause
    // menu's Escape key should work no matter what has focus).
    //
    // mousePosition()/typedTextThisFrame() are always raw — a position
    // isn't "consumed" the way a press is, and typed text is only ever
    // meaningful to whatever's actually reading it (your code, or the
    // focused widget) so there's nothing useful to suppress.
    bool isKeyDown(int key, bool ignoreUICapture = false) const;
    bool isKeyPressed(int key, bool ignoreUICapture = false) const;
    bool isKeyReleased(int key, bool ignoreUICapture = false) const;

    bool isMouseButtonDown(int button, bool ignoreUICapture = false) const;
    bool isMouseButtonPressed(int button, bool ignoreUICapture = false) const;
    bool isMouseButtonReleased(int button, bool ignoreUICapture = false) const;

    glm::vec2 mousePosition() const;
    glm::vec2 mouseDelta(bool ignoreUICapture = false) const;
    float     mouseScrollY(bool ignoreUICapture = false) const;

    const std::string& typedTextThisFrame() const;

    // What DustUI claimed *this frame* — the same flags isKeyDown()/
    // isMouseButtonDown() already check internally, exposed directly for
    // when you need to branch on it yourself (e.g. skip a raycast into the
    // world entirely if the click was actually on a UI widget).
    bool uiWantsMouse() const;
    bool uiWantsKeyboard() const;

private:
    glm::mat4  activeViewProj{ 1.0f };
    glm::vec3  activeCamRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3  activeCamUp   { 0.0f, 1.0f, 0.0f };
    bool       frameValid     = false;
    bool       renderingBegun = false;
    UI::Widget uiRoot;
    UI::Font   uiFont;
    bool       uiFontLoaded = false;

    // This frame's flattened widget quads and last frame's, kept as members
    // so neither allocates per frame. Comparing them is the Phase 4 diff:
    // identical means the mapped instance buffer already holds the right
    // data and the upload can be skipped.
    // One entry per run of quads or glyphs a single widget produced, tagged
    // with the depth key it inherited. Sorting these — rather than the widget
    // tree — is what makes layers work without perturbing layout.
    struct UIDrawItem {
        uint64_t depthKey;
        bool     isText;
        uint32_t first;  // index into uiRects / uiGlyphs
        uint32_t count;
    };

    // All kept as members so none of them allocate per frame.
    std::vector<UI::RectInstance>  uiRects;        // tree order
    std::vector<UI::GlyphInstance> uiGlyphs;       // tree order
    std::vector<UIDrawItem>        uiDrawItems;
    std::vector<UI::RectInstance>  uiSortedRects;  // draw order — what actually gets uploaded
    std::vector<UI::GlyphInstance> uiSortedGlyphs;
    std::vector<UI::RectInstance>  uiPrevRects;    // last frame's, for the diff

    // Scroll offset per scroll container, keyed by the widget's implicit
    // path-hash id — the tree is rebuilt every frame, so this is the only
    // place it can live. Entries are never pruned; a HUD has a handful of
    // scroll views, not thousands.
    std::unordered_map<uint64_t, float> uiScrollOffsets;
    static constexpr float kUIScrollStepPx = 40.0f; // px per wheel notch

    // Persistent across frames on purpose — the Widget tree is rebuilt from
    // scratch every frame (immediate mode), so hover/focus/press state
    // can't live on a Widget instance. Identified by Widget's implicit
    // path-hash id (see Core/UI/Widget.hpp) instead of a pointer, so there's
    // nothing here that can dangle when the tree gets rebuilt.
    uint64_t uiHoveredId = 0;
    uint64_t uiFocusedId = 0;
    uint64_t uiPressedId = 0; // widget that got the mouse-down half of a click

    // Lazily-built compute pipeline for particle simulation
    VkPipelineLayout m_particleComputeLayout   = VK_NULL_HANDLE;
    // Outlives the pipeline layout on purpose: a VkPipelineLayout keeps
    // referencing the set layouts it was built from, and destroying them
    // early invalidates it (only legal with the maintenance4 feature).
    VkDescriptorSetLayout m_particleComputeSetLayout = VK_NULL_HANDLE;
    VkPipeline       m_particleComputePipeline = VK_NULL_HANDLE;
    bool             ensureComputePipeline();
};

} // namespace Dust
