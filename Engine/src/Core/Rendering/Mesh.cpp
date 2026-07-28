#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Dust {

// ─── VERT OPS ─────────────────────────────────

VertHandle Mesh::addVert(const Vertex& v) {
    if (!vertFree.empty()) {
        uint32_t idx = vertFree.back();
        vertFree.pop_back();
        vertSlots[idx].v     = v;
        vertSlots[idx].alive = true;
        return { idx, vertSlots[idx].gen };
    }
    vertSlots.push_back({ v, 1, true });
    dirty = true;
    return { (uint32_t)(vertSlots.size() - 1), 1 };
}

void Mesh::removeVert(VertHandle h) {
    if (!valid(h) || h.index >= vertSlots.size()) return;
    auto& slot = vertSlots[h.index];
    if (!slot.alive || slot.gen != h.gen) return;
    slot.alive = false;
    slot.gen++;
    vertFree.push_back(h.index);
    dirty = true;
}

Vertex* Mesh::getVert(VertHandle h) {
    if (!valid(h) || h.index >= vertSlots.size()) return nullptr;
    auto& slot = vertSlots[h.index];
    if (!slot.alive || slot.gen != h.gen) return nullptr;
    return &slot.v;
}

void Mesh::setVert(VertHandle h, const Vertex& v) {
    Vertex* vp = getVert(h);
    if (vp) { *vp = v; dirty = true; }
}

// ─── FACE OPS ─────────────────────────────────

FaceHandle Mesh::addFace(VertHandle a, VertHandle b, VertHandle c) {
    Face f{ { a.index, b.index, c.index } };
    if (!faceFree.empty()) {
        uint32_t idx = faceFree.back();
        faceFree.pop_back();
        faceSlots[idx].f     = f;
        faceSlots[idx].alive = true;
        dirty = true;
        return { idx, faceSlots[idx].gen };
    }
    faceSlots.push_back({ f, 1, true });
    dirty = true;
    return { (uint32_t)(faceSlots.size() - 1), 1 };
}

void Mesh::removeFace(FaceHandle h) {
    if (!valid(h) || h.index >= faceSlots.size()) return;
    auto& slot = faceSlots[h.index];
    if (!slot.alive || slot.gen != h.gen) return;
    slot.alive = false;
    slot.gen++;
    faceFree.push_back(h.index);
    dirty = true;
}

Face* Mesh::getFace(FaceHandle h) {
    if (!valid(h) || h.index >= faceSlots.size()) return nullptr;
    auto& slot = faceSlots[h.index];
    if (!slot.alive || slot.gen != h.gen) return nullptr;
    return &slot.f;
}

// ─── BULK ─────────────────────────────────────

void Mesh::recalcNormals() {
    // zero all normals
    for (auto& slot : vertSlots)
        if (slot.alive) slot.v.normal[0] = slot.v.normal[1] = slot.v.normal[2] = 0.0f;

    for (auto& slot : faceSlots) {
        if (!slot.alive) continue;
        Vertex* a = &vertSlots[slot.f.verts[0]].v;
        Vertex* b = &vertSlots[slot.f.verts[1]].v;
        Vertex* c = &vertSlots[slot.f.verts[2]].v;

        float e1[3] = { b->position[0]-a->position[0], b->position[1]-a->position[1], b->position[2]-a->position[2] };
        float e2[3] = { c->position[0]-a->position[0], c->position[1]-a->position[1], c->position[2]-a->position[2] };
        float n[3]  = {
            e1[1]*e2[2] - e1[2]*e2[1],
            e1[2]*e2[0] - e1[0]*e2[2],
            e1[0]*e2[1] - e1[1]*e2[0]
        };
        for (int i = 0; i < 3; i++) {
            vertSlots[slot.f.verts[i]].v.normal[0] += n[0];
            vertSlots[slot.f.verts[i]].v.normal[1] += n[1];
            vertSlots[slot.f.verts[i]].v.normal[2] += n[2];
        }
    }

    // normalize
    for (auto& slot : vertSlots) {
        if (!slot.alive) continue;
        float* n = slot.v.normal;
        float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 0.0001f) { n[0]/=len; n[1]/=len; n[2]/=len; }
    }
    dirty = true;
}

void Mesh::forEachVert(std::function<void(VertHandle, Vertex&)> fn) {
    for (uint32_t i = 0; i < vertSlots.size(); i++) {
        auto& slot = vertSlots[i];
        if (slot.alive) fn({ i, slot.gen }, slot.v);
    }
}

void Mesh::forEachFace(std::function<void(FaceHandle, Face&)> fn) {
    for (uint32_t i = 0; i < faceSlots.size(); i++) {
        auto& slot = faceSlots[i];
        if (slot.alive) fn({ i, slot.gen }, slot.f);
    }
}

// ─── GPU ──────────────────────────────────────

static VkBuffer createBuffer(VulkanContext& ctx, VkDeviceSize size,
                              VkBufferUsageFlags usage, VmaAllocation& alloc) {
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size  = size;
    bufInfo.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer buf;
    vmaCreateBuffer(ctx.allocator, &bufInfo, &allocInfo, &buf, &alloc, nullptr);
    return buf;
}

bool Mesh::upload(VulkanContext& ctx) {
    // Collect live verts + build index remapping
    std::vector<Vertex>   verts;
    std::vector<uint32_t> remap(vertSlots.size(), UINT32_MAX);

    for (uint32_t i = 0; i < vertSlots.size(); i++) {
        if (!vertSlots[i].alive) continue;
        remap[i] = (uint32_t)verts.size();
        verts.push_back(vertSlots[i].v);
    }

    std::vector<uint32_t> indices;
    for (auto& slot : faceSlots) {
        if (!slot.alive) continue;
        for (int i = 0; i < 3; i++)
            indices.push_back(remap[slot.f.verts[i]]);
    }

    if (verts.empty() || indices.empty()) return true;

    // Destroy old buffers
    if (vertexBuffer) { vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertAlloc); vertexBuffer = VK_NULL_HANDLE; }
    if (indexBuffer)  { vmaDestroyBuffer(ctx.allocator, indexBuffer,  indexAlloc); indexBuffer  = VK_NULL_HANDLE; }

    // Vertex buffer
    VkDeviceSize vSize = verts.size() * sizeof(Vertex);
    vertexBuffer = createBuffer(ctx, vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertAlloc);
    void* vData;
    vmaMapMemory(ctx.allocator, vertAlloc, &vData);
    memcpy(vData, verts.data(), vSize);
    vmaUnmapMemory(ctx.allocator, vertAlloc);

    // Index buffer
    VkDeviceSize iSize = indices.size() * sizeof(uint32_t);
    indexBuffer = createBuffer(ctx, iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexAlloc);
    void* iData;
    vmaMapMemory(ctx.allocator, indexAlloc, &iData);
    memcpy(iData, indices.data(), iSize);
    vmaUnmapMemory(ctx.allocator, indexAlloc);

    indexCount = (uint32_t)indices.size();
    dirty = false;
    return true;
}

void Mesh::destroy(VulkanContext& ctx) {
    if (vertexBuffer) { vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertAlloc); vertexBuffer = VK_NULL_HANDLE; }
    if (indexBuffer)  { vmaDestroyBuffer(ctx.allocator, indexBuffer,  indexAlloc); indexBuffer  = VK_NULL_HANDLE; }
}

// ─── HELPERS ──────────────────────────────────

Mesh Mesh::makeTriangle() {
    Mesh m;
    auto a = m.addVert({{ 0.0f,  0.5f, 0.0f}, {0,0,1}, {0.5f,0.0f}, {1,0,0,1}});
    auto b = m.addVert({{-0.5f, -0.5f, 0.0f}, {0,0,1}, {0.0f,1.0f}, {0,1,0,1}});
    auto c = m.addVert({{ 0.5f, -0.5f, 0.0f}, {0,0,1}, {1.0f,1.0f}, {0,0,1,1}});
    m.addFace(a, b, c);
    return m;
}

Mesh Mesh::makeCube() {
    Mesh m;
    // 8 corners
    auto v0 = m.addVert({{-0.5f,-0.5f,-0.5f},{0,0,0},{0,0},{1,1,1,1}});
    auto v1 = m.addVert({{ 0.5f,-0.5f,-0.5f},{0,0,0},{1,0},{1,1,1,1}});
    auto v2 = m.addVert({{ 0.5f, 0.5f,-0.5f},{0,0,0},{1,1},{1,1,1,1}});
    auto v3 = m.addVert({{-0.5f, 0.5f,-0.5f},{0,0,0},{0,1},{1,1,1,1}});
    auto v4 = m.addVert({{-0.5f,-0.5f, 0.5f},{0,0,0},{0,0},{1,1,1,1}});
    auto v5 = m.addVert({{ 0.5f,-0.5f, 0.5f},{0,0,0},{1,0},{1,1,1,1}});
    auto v6 = m.addVert({{ 0.5f, 0.5f, 0.5f},{0,0,0},{1,1},{1,1,1,1}});
    auto v7 = m.addVert({{-0.5f, 0.5f, 0.5f},{0,0,0},{0,1},{1,1,1,1}});
    // 6 faces (2 tris each)
    m.addFace(v0,v1,v2); m.addFace(v0,v2,v3); // back
    m.addFace(v4,v6,v5); m.addFace(v4,v7,v6); // front
    m.addFace(v0,v3,v7); m.addFace(v0,v7,v4); // left
    m.addFace(v1,v5,v6); m.addFace(v1,v6,v2); // right
    m.addFace(v3,v2,v6); m.addFace(v3,v6,v7); // top
    m.addFace(v0,v4,v5); m.addFace(v0,v5,v1); // bottom
    m.recalcNormals();
    return m;
}

// ─── OBJ LOADER ───────────────────────────────

namespace {
    struct ObjIdx { int v = 0, vt = 0, vn = 0; }; // 1-based; 0 = absent

    ObjIdx parseFaceToken(const std::string& tok) {
        ObjIdx idx;
        size_t p1 = tok.find('/');
        if (p1 == std::string::npos) { idx.v = std::stoi(tok); return idx; }
        idx.v = std::stoi(tok.substr(0, p1));

        size_t p2 = tok.find('/', p1 + 1);
        if (p2 == std::string::npos) { // v/vt
            std::string vt = tok.substr(p1 + 1);
            if (!vt.empty()) idx.vt = std::stoi(vt);
            return idx;
        }
        std::string vt = tok.substr(p1 + 1, p2 - p1 - 1); // v/vt/vn or v//vn
        if (!vt.empty()) idx.vt = std::stoi(vt);
        std::string vn = tok.substr(p2 + 1);
        if (!vn.empty()) idx.vn = std::stoi(vn);
        return idx;
    }

    // OBJ negative indices are relative to how many entries have been
    // parsed so far (count at time the face line is read), not file totals.
    int resolveIdx(int idx, size_t countSoFar) {
        return idx < 0 ? (int)countSoFar + idx + 1 : idx;
    }
}

namespace {

// Shared by loadOBJ (file) and loadOBJFromMemory (bytes from AssetManager) —
// both just need to hand this an istream over the .obj text.
Mesh loadOBJStream(std::istream& file) {
    Mesh m;

    struct V3 { float x, y, z; };
    struct V2 { float x, y; };
    struct V4 { float x, y, z, w; };
    std::vector<V3> positions;
    std::vector<V3> normals;
    std::vector<V2> uvs;
    std::vector<V4> colors; // parallel to positions; from optional "v x y z r g b [a]" extension

    // Cache one Mesh vertex per unique "v/vt/vn" corner combo (OBJ's index
    // streams don't share vertices the way our interleaved Vertex does).
    std::unordered_map<std::string, VertHandle> cache;

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;

        if (tag == "v") {
            // Standard xyz, optionally followed by the non-standard r g b [a]
            // vertex-color extension (Blender/MeshLab/CloudCompare export it).
            std::vector<float> nums;
            float val;
            while (ss >> val) nums.push_back(val);

            V3 p{};
            if (nums.size() >= 3) { p.x = nums[0]; p.y = nums[1]; p.z = nums[2]; }
            positions.push_back(p);

            V4 c{1.0f, 1.0f, 1.0f, 1.0f};
            if (nums.size() >= 6) { c.x = nums[3]; c.y = nums[4]; c.z = nums[5]; }
            if (nums.size() >= 7) c.w = nums[6];
            colors.push_back(c);
        } else if (tag == "vn") {
            V3 n{};
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "vt") {
            V2 uv{};
            ss >> uv.x >> uv.y;
            uvs.push_back(uv);
        } else if (tag == "f") {
            std::vector<VertHandle> corners;
            std::string tok;
            while (ss >> tok) {
                auto cached = cache.find(tok);
                if (cached != cache.end()) {
                    corners.push_back(cached->second);
                    continue;
                }

                ObjIdx raw = parseFaceToken(tok);
                int vi  = resolveIdx(raw.v,  positions.size());
                int vti = raw.vt ? resolveIdx(raw.vt, uvs.size())     : 0;
                int vni = raw.vn ? resolveIdx(raw.vn, normals.size()) : 0;

                if (vi < 1 || (size_t)vi > positions.size()) continue; // malformed line

                Vertex vert{};
                V3 p = positions[vi - 1];
                vert.position[0] = p.x; vert.position[1] = p.y; vert.position[2] = p.z;
                if (vni >= 1 && (size_t)vni <= normals.size()) {
                    V3 n = normals[vni - 1];
                    vert.normal[0] = n.x; vert.normal[1] = n.y; vert.normal[2] = n.z;
                }
                if (vti >= 1 && (size_t)vti <= uvs.size()) {
                    V2 uv = uvs[vti - 1];
                    vert.uv[0] = uv.x; vert.uv[1] = uv.y;
                }
                V4 c = colors[vi - 1]; // parallel to positions, always populated
                vert.color[0] = c.x; vert.color[1] = c.y; vert.color[2] = c.z; vert.color[3] = c.w;

                VertHandle h = m.addVert(vert);
                cache.emplace(tok, h);
                corners.push_back(h);
            }

            // Fan-triangulate n-gons
            for (size_t i = 1; i + 1 < corners.size(); i++)
                m.addFace(corners[0], corners[i], corners[i + 1]);
        }
        // ignore o/g/s/usemtl/mtllib/comments — no materials/groups yet
    }

    if (normals.empty())
        m.recalcNormals();

    return m;
}

} // namespace

Mesh Mesh::loadOBJ(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "dust: failed to open OBJ '%s'\n", path);
        return Mesh{};
    }
    return loadOBJStream(file);
}

Mesh Mesh::loadOBJFromMemory(const uint8_t* data, size_t size) {
    std::string text((const char*)data, size);
    std::istringstream stream(text);
    return loadOBJStream(stream);
}

} // namespace Dust
