#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace Dust {

struct VulkanContext;

// ─── VERTEX ───────────────────────────────────
// Must match default.vert attribute layout exactly

struct Vertex {
    float position[3]; // location 0
    float normal[3];   // location 1
    float uv[2];       // location 2
    float color[4];    // location 3
};

// ─── HANDLES ──────────────────────────────────

struct VertHandle { uint32_t index; uint32_t gen; };
struct FaceHandle { uint32_t index; uint32_t gen; };

constexpr uint32_t INVALID_GEN = 0;
inline bool valid(VertHandle h) { return h.gen != INVALID_GEN; }
inline bool valid(FaceHandle h) { return h.gen != INVALID_GEN; }

// ─── FACE ─────────────────────────────────────

struct Face {
    uint32_t verts[3]; // indices into dense vert array
};

// ─── MESH ─────────────────────────────────────

struct Mesh {
    // CPU-side slot map storage
    struct VertSlot { Vertex v; uint32_t gen; bool alive; };
    struct FaceSlot { Face   f; uint32_t gen; bool alive; };

    std::vector<VertSlot> vertSlots;
    std::vector<FaceSlot> faceSlots;
    std::vector<uint32_t> vertFree;
    std::vector<uint32_t> faceFree;

    // GPU buffers (VMA)
    VkBuffer     vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertAlloc   = VK_NULL_HANDLE;
    VkBuffer     indexBuffer  = VK_NULL_HANDLE;
    VmaAllocation indexAlloc  = VK_NULL_HANDLE;
    uint32_t     indexCount   = 0;
    uint32_t     uploadedVertCount = 0; // live vert count as of the last upload() — see updateVertices()
    bool         dirty        = true;

    // Stashed by upload() — updateVertices()/destroy() reuse it so callers
    // don't have to keep re-threading VulkanContext through every frame just
    // to poke vertex data. upload() is the one place a context has to come
    // from somewhere; everything downstream of it shouldn't need it again.
    VulkanContext* ownerCtx = nullptr;

    // ── Vert ops ──
    VertHandle addVert(const Vertex& v);
    void       removeVert(VertHandle h);
    Vertex*    getVert(VertHandle h);
    void       setVert(VertHandle h, const Vertex& v);

    // ── Face ops ──
    FaceHandle addFace(VertHandle a, VertHandle b, VertHandle c);
    void       removeFace(FaceHandle h);
    Face*      getFace(FaceHandle h);

    // ── Bulk ──
    void recalcNormals();
    void forEachVert(std::function<void(VertHandle, Vertex&)> fn);
    void forEachFace(std::function<void(FaceHandle, Face&)>   fn);

    // ── GPU ──
    // The one call that needs a VulkanContext explicitly — everything else
    // below reuses the context stashed here (see ownerCtx above).
    bool upload(VulkanContext& ctx);   // call after modifying verts/faces (recreates GPU buffers)
    void destroy();

    // Re-copies current vertex positions/attributes into the EXISTING GPU
    // buffer — no buffer recreation, cheap enough to call every frame for
    // animated/deforming meshes (direct field edits like
    // `mesh.vertSlots[i].v.position[...] = ...` don't need setVert/dirty,
    // just call this afterward). Vertex *count* must match the last
    // upload() — if you added/removed verts or faces, call upload() instead,
    // it handles resizing.
    //
    // Caveat: this writes straight into the buffer the GPU may still be
    // reading from a frame still in flight (Renderer double-buffers command
    // buffers/fences per FrameData, but not per-mesh vertex data) — fine for
    // iterating on vertex manipulation, but a real animated-mesh path will
    // eventually want a per-frame-in-flight buffer to close that race.
    bool updateVertices();

    // ── Helpers ──
    static Mesh makeTriangle();  // default test mesh
    static Mesh makeCube();

    // Unit quad, corners at (0,0) and (1,1) in the XY plane — position.xy
    // doubles as a 0..1 local UV. Used by DustUI's screen-space widget
    // rendering (Core/UI/), but generic enough for anything else 2D.
    static Mesh makeQuad();

    // Basic Wavefront OBJ loader — v/vt/vn/f, fan-triangulates n-gons,
    // dedupes shared v/vt/vn corners. No materials/groups yet — vertex color
    // defaults to white unless the non-standard "v x y z r g b [a]" vertex-
    // color extension is present. recalcNormals() runs automatically if the
    // file has no vn lines.
    static Mesh loadOBJ(const char* path);

    // Same parser, fed from an in-memory buffer — e.g. bytes decoded by
    // AssetManager, which never touches the filesystem for the raw .obj.
    static Mesh loadOBJFromMemory(const uint8_t* data, size_t size);
};

} // namespace Dust
