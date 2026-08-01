#pragma once

// On-disk format for a converted model (DustModel) — produced once, offline,
// by DustPacker's assimp importer, and consumed at runtime by Engine's model
// loader. Lives here (not in Engine) so both sides can include it without
// DustPacker pulling in Vulkan, and without Engine and DustPacker duplicating
// (and drifting on) the byte layout — same reasoning as PackFormat.hpp.
//
// Supports multiple formats (obj/fbx/gltf/dae/...) by normalizing everything
// at pack time to one shape: a flat list of textures (raw RGBA8), a flat list
// of materials (indices into textures), and a flat list of submeshes (one per
// source mesh+material, each carrying its baked node-to-model transform).
// Runtime only ever parses this one format, no matter what the source was.
//
// Layout (little-endian, no padding):
//   [ModelHeader]
//   textureCount  x [TextureHeader][width*height*4 RGBA8 bytes]
//   materialCount x [MaterialRecord]
//   submeshCount  x [SubmeshHeader][vertexCount * Vertex][indexCount * uint32_t]

#include <cstdint>
#include <cstring>
#include <vector>

namespace Dust::ModelFmt {

inline constexpr char     kMagic[4] = { 'D', 'M', 'D', '1' };
inline constexpr uint32_t kVersion  = 1;
inline constexpr uint32_t kNoIndex  = 0xFFFFFFFFu; // "no texture" / "no material"

// Mirrors Dust::Vertex in Engine/Core/Rendering/Mesh.hpp field-for-field.
// Duplicated (not shared) on purpose — this header must stay dependency-free
// so DustPacker doesn't need to link Vulkan just to write models.
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float color[4];
};

#pragma pack(push, 1)
struct ModelHeader {
    char     magic[4];
    uint32_t version;
    uint32_t textureCount;
    uint32_t materialCount;
    uint32_t submeshCount;
};

struct TextureHeader {
    uint32_t width;
    uint32_t height; // pixel data (width*height*4 RGBA8 bytes) follows immediately
};

struct MaterialRecord {
    float    baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float    metallic     = 0.0f;
    float    roughness    = 1.0f;
    float    emissive[3]  = { 0.0f, 0.0f, 0.0f };
    uint32_t baseColorTexture         = kNoIndex;
    uint32_t normalTexture            = kNoIndex;
    uint32_t metallicRoughnessTexture = kNoIndex;
    uint32_t emissiveTexture          = kNoIndex;
    uint32_t occlusionTexture         = kNoIndex;
};

struct SubmeshHeader {
    uint32_t materialIndex = kNoIndex;
    uint32_t vertexCount   = 0;
    uint32_t indexCount    = 0;
    // Node-to-model transform baked in at import time (column-major, identity
    // by default), so multi-node source scenes (fbx/gltf) don't need a
    // runtime scene graph just to render in the right place.
    float transform[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
};
#pragma pack(pop)

// ── In-memory, owns its data ───────────────────

struct Texture {
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> pixels; // RGBA8, width*height*4 bytes
};

struct Material : MaterialRecord {};

struct Submesh {
    uint32_t              materialIndex = kNoIndex;
    float                 transform[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

struct Model {
    std::vector<Texture>  textures;
    std::vector<Material> materials;
    std::vector<Submesh>  submeshes;
};

// ── (De)serialization — the only place that touches the byte layout ──

inline std::vector<uint8_t> serialize(const Model& m) {
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        out.insert(out.end(), b, b + n);
    };

    ModelHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version       = kVersion;
    header.textureCount  = (uint32_t)m.textures.size();
    header.materialCount = (uint32_t)m.materials.size();
    header.submeshCount  = (uint32_t)m.submeshes.size();
    put(&header, sizeof(header));

    for (auto& t : m.textures) {
        TextureHeader th{ t.width, t.height };
        put(&th, sizeof(th));
        put(t.pixels.data(), t.pixels.size());
    }

    for (auto& mat : m.materials) {
        MaterialRecord rec = mat;
        put(&rec, sizeof(rec));
    }

    for (auto& sm : m.submeshes) {
        SubmeshHeader sh{};
        sh.materialIndex = sm.materialIndex;
        sh.vertexCount   = (uint32_t)sm.vertices.size();
        sh.indexCount    = (uint32_t)sm.indices.size();
        memcpy(sh.transform, sm.transform, sizeof(sh.transform));
        put(&sh, sizeof(sh));
        put(sm.vertices.data(), sm.vertices.size() * sizeof(Vertex));
        put(sm.indices.data(),  sm.indices.size()  * sizeof(uint32_t));
    }

    return out;
}

// Returns false on any malformed/truncated input — always check before using `out`.
inline bool deserialize(const uint8_t* data, size_t size, Model& out) {
    size_t off = 0;
    auto get = [&](void* p, size_t n) -> bool {
        if (off + n > size) return false;
        memcpy(p, data + off, n);
        off += n;
        return true;
    };

    ModelHeader header{};
    if (!get(&header, sizeof(header))) return false;
    if (memcmp(header.magic, kMagic, 4) != 0) return false;
    if (header.version != kVersion) return false;

    out = Model{};
    out.textures.resize(header.textureCount);
    for (auto& t : out.textures) {
        TextureHeader th{};
        if (!get(&th, sizeof(th))) return false;
        t.width  = th.width;
        t.height = th.height;
        size_t bytes = (size_t)th.width * th.height * 4;
        t.pixels.resize(bytes);
        if (bytes && !get(t.pixels.data(), bytes)) return false;
    }

    out.materials.resize(header.materialCount);
    for (auto& mat : out.materials) {
        MaterialRecord rec{};
        if (!get(&rec, sizeof(rec))) return false;
        static_cast<MaterialRecord&>(mat) = rec;
    }

    out.submeshes.resize(header.submeshCount);
    for (auto& sm : out.submeshes) {
        SubmeshHeader sh{};
        if (!get(&sh, sizeof(sh))) return false;
        sm.materialIndex = sh.materialIndex;
        memcpy(sm.transform, sh.transform, sizeof(sm.transform));

        sm.vertices.resize(sh.vertexCount);
        size_t vBytes = (size_t)sh.vertexCount * sizeof(Vertex);
        if (vBytes && !get(sm.vertices.data(), vBytes)) return false;

        sm.indices.resize(sh.indexCount);
        size_t iBytes = (size_t)sh.indexCount * sizeof(uint32_t);
        if (iBytes && !get(sm.indices.data(), iBytes)) return false;
    }

    return true;
}

} // namespace Dust::ModelFmt
