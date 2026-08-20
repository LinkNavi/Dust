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
#include <array>
#include <string>
#include <vector>
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

    // ── Fog — cheap distance fog for drawModel()/drawMesh(), mixing shaded
    // color toward fogColor between fogStart/fogEnd (view-space distance
    // from the camera, derived for free from clip.w in default.vert — no
    // extra draw/pass/texture). Off by default; chainable to match the
    // UI widget setter style. start/end in the same world units as Camera.
    DustEngine& setFog(bool enabled, glm::vec3 color = { 0.5f, 0.5f, 0.5f },
                        float start = 10.0f, float end = 50.0f) {
        fogEnabled = enabled; fogColor = color; fogStart = start; fogEnd = end;
        return *this;
    }

    // ── Lighting — Blinn-Phong (Shaders/lit.vert/lit.frag), NOT full PBR —
    // cheap enough for low-end GPUs. Auto-selected: drawModel()/drawMesh()
    // switch to the lit pipeline the moment any light below is configured
    // (a directional light with intensity > 0, or at least one active
    // point/spot), and keep using the existing unlit default pipeline
    // otherwise — an existing unlit demo that never calls any of these
    // keeps rendering exactly as before, zero-cost. See DustEngine.cpp's
    // drawModel()/drawMesh() and anyLightsActive().
    static constexpr int kMaxPointLights = (int)Dust::kMaxPointLights; // see Renderer.hpp — shared with the lit shader's fixed-size arrays
    static constexpr int kMaxSpotLights  = (int)Dust::kMaxSpotLights;

    // One directional "sun" light. direction is the way the light travels
    // (e.g. {0,-1,0} shines straight down); intensity 0 turns it off.
    DustEngine& setDirectionalLight(glm::vec3 direction, glm::vec3 color, float intensity);

    // Flat ambient term added under every light — a cheap stand-in for real
    // indirect lighting, same "not PBR, cheap" reasoning as the Blinn-Phong
    // model itself.
    DustEngine& setAmbientLight(glm::vec3 color, float intensity);

    // Fixed-slot lights (kMaxPointLights/kMaxSpotLights total, matching the
    // lit shader's fixed-size arrays — no dynamic branching on GPU). Returns
    // a handle for removePointLight()/removeSpotLight(), or -1 if every slot
    // is already in use. radius/range is the distance the light's
    // contribution falls off to zero.
    int  addPointLight(glm::vec3 pos, glm::vec3 color, float intensity, float radius);
    void removePointLight(int handle);

    // innerDeg/outerDeg bound the cone's soft edge — full intensity inside
    // innerDeg, fading to zero at outerDeg (outerDeg should be >= innerDeg).
    int  addSpotLight(glm::vec3 pos, glm::vec3 direction, glm::vec3 color, float intensity,
                      float range, float innerDeg = 20.0f, float outerDeg = 30.0f);
    void removeSpotLight(int handle);

    // ── Directional shadow map (Shaders/shadow.vert/frag, Renderer::shadowImage) ──
    // Off by default. Only takes effect when a directional light is also
    // configured (setDirectionalLight with intensity > 0) — no directional
    // light means the shadow pass is skipped, not an error, same as calling
    // this before any light exists at all. See DustEngine.cpp's endMode3D().
    DustEngine& setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; return *this; }

    // Bounds of the light-space orthographic frustum used to render the
    // shadow map — center of the box in world space, half-extent (box is
    // 2*halfExtent on a side, in the plane perpendicular to the light), and
    // near/far along the light's own direction. Defaults cover roughly a
    // 20x20 unit area, which is enough for this repo's demo scene without
    // ever needing to be called. Kept tight (not some huge default like
    // 1000) — a directional shadow map's usable resolution is inversely
    // proportional to how much world space it has to cover.
    DustEngine& setShadowBounds(glm::vec3 center, float halfExtent, float near = 1.0f, float far = 40.0f) {
        shadowCenter = center; shadowHalfExtent = halfExtent; shadowNear = near; shadowFar = far;
        return *this;
    }

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

    // Locks/hides the cursor for mouselook (GLFW_CURSOR_DISABLED gives
    // unbounded relative deltas — exactly what mouseDelta() wants while
    // flying) or restores normal cursor behavior for UI interaction.
    void setCursorLocked(bool locked);

private:
    glm::mat4  activeViewProj{ 1.0f };
    glm::vec3  activeCamRight{ 1.0f, 0.0f, 0.0f };
    glm::vec3  activeCamUp   { 0.0f, 1.0f, 0.0f };
    // Copied (not referenced) in beginMode3D — cheap struct, and drawModel/
    // drawMesh's distance cull and World/Hand UI projection both need
    // position/farClip/forward() after the caller's Camera may have gone
    // out of scope for the frame.
    Camera     activeCamera;
    bool       frameValid     = false;
    bool       renderingBegun = false;

    // Fog state — see setFog(); read by drawMesh()/drawModel() each draw.
    bool       fogEnabled = false;
    glm::vec3  fogColor{ 0.5f, 0.5f, 0.5f };
    float      fogStart = 10.0f;
    float      fogEnd   = 50.0f;

    // Lighting state — see setDirectionalLight()/setAmbientLight()/
    // addPointLight()/addSpotLight(). Pushed into Renderer::lightsSet's UBO
    // once per frame by updateLightsUBO() (called from beginMode3D(), which
    // is also where the camera this lighting is relative to becomes known),
    // not per draw — see LightsUBOData's doc comment in Renderer.hpp.
    bool       dirLightOn         = false;
    glm::vec3  dirLightDirection{ 0.0f, -1.0f, 0.0f };
    glm::vec3  dirLightColor{ 1.0f, 1.0f, 1.0f };
    float      dirLightIntensity  = 0.0f;
    glm::vec3  ambientColor{ 0.03f, 0.03f, 0.03f };
    float      ambientIntensity   = 1.0f;

    struct PointLightSlot {
        bool      active    = false;
        glm::vec3 pos{ 0.0f };
        glm::vec3 color{ 1.0f };
        float     intensity = 0.0f;
        float     radius    = 1.0f;
    };
    struct SpotLightSlot {
        bool      active    = false;
        glm::vec3 pos{ 0.0f };
        glm::vec3 dir{ 0.0f, -1.0f, 0.0f };
        glm::vec3 color{ 1.0f };
        float     intensity = 0.0f;
        float     range     = 1.0f;
        float     innerCos  = 1.0f; // cos(0deg) — precomputed at addSpotLight() time
        float     outerCos  = 0.0f; // cos(90deg)
    };
    std::array<PointLightSlot, kMaxPointLights> pointLights{};
    std::array<SpotLightSlot,  kMaxSpotLights>  spotLights{};

    bool anyLightsActive() const;
    void updateLightsUBO();

    // ── Directional shadows — see setShadowsEnabled()/setShadowBounds(). ──
    bool      shadowsEnabled   = false;
    glm::vec3 shadowCenter{ 0.0f };
    float     shadowHalfExtent = 10.0f; // covers this repo's demo scene's -5..+5 X range plus margin
    float     shadowNear       = 1.0f;
    float     shadowFar        = 40.0f;

    // True for the duration of a beginMode3D()/endMode3D() bracket iff this
    // frame is actually doing a shadow pass (shadowsEnabled && dirLightOn) —
    // computed once in beginMode3D so drawModel()/drawMesh() only need a
    // single bool check, not to re-derive it per draw. When false, drawing
    // is the original immediate-mode path with zero added cost.
    bool      shadowPassActive = false;
    glm::mat4 lightSpaceViewProj{ 1.0f };

    // One buffered lit draw call — enough to replay both the shadow pass
    // (mesh + world only) and the color pass (everything) once the shadow
    // map has been populated. Only populated/consumed when shadowPassActive
    // — see drawModel()/drawMesh()/endMode3D() in DustEngine.cpp.
    struct BufferedLitDraw {
        Mesh*           mesh;
        glm::mat4       world;
        uint32_t        featureMask;
        VkDescriptorSet litMaterialSet;
        glm::vec4       baseColorFactor;
        glm::vec4       materialParams;
    };
    std::vector<BufferedLitDraw> shadowDrawBuffer;
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

    // Lazily-built once and reused every dispatchParticles() call — only the
    // descriptor set *contents* and command buffer *recording* change per
    // dispatch, not these Vulkan objects.
    VkCommandPool    m_particleComputePool     = VK_NULL_HANDLE;
    VkCommandBuffer  m_particleComputeCmd      = VK_NULL_HANDLE;
    VkDescriptorPool m_particleComputeDescPool = VK_NULL_HANDLE;
    VkDescriptorSet  m_particleComputeDescSet  = VK_NULL_HANDLE;
    VkFence          m_particleComputeFence    = VK_NULL_HANDLE;
    bool             m_particleComputeFencePending = false;
};

} // namespace Dust
