// DustPacker — offline tool that packs a folder of assets into a single
// encrypted, compressed .pack file (format shared with AssetManager via
// PackFormat.hpp). Not linked into the game; this never ships.
//
// Model files (fbx/gltf/glb/dae/stl/ply/3ds/...) are converted through
// assimp into DustModel binary (ModelFormat.hpp) before packing — the
// runtime only ever has to parse that one normalized format, no matter what
// the source was. Plain .obj is still packed raw; Engine's original text
// OBJ loader keeps working unchanged for that path.
//
// Usage: DustPacker <assetFolder> <output.pack> <passphrase>

#include "AssetManager/PackFormat.hpp"
#include "AssetManager/ModelFormat.hpp"
#include "FontImport.hpp"
#include <sodium.h>
#include <zstd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unordered_map>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <assimp/postprocess.h>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

namespace fs = std::filesystem;
using namespace Dust::Pack;
namespace Fmt = Dust::ModelFmt;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && !f.read((char*)buf.data(), size)) return {};
    return buf;
}

// ── Model import (assimp) ──────────────────────────────────────────

bool isModelExtension(std::string ext) {
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    static const char* kExts[] = { ".fbx", ".gltf", ".glb", ".dae", ".stl", ".ply", ".3ds" };
    for (auto* e : kExts)
        if (ext == e) return true;
    return false;
}

bool isFontExtension(std::string ext) {
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".ttf" || ext == ".otf";
}

// aiMatrix4x4 is stored row-major (a1..a4 = row 0, ...). glm::mat4 (and the
// binary format) store column-major float[16]. This transposes on copy so
// Engine can hand the 16 floats straight to glm::make_mat4 and get back the
// same transform assimp computed.
void toColumnMajor(const aiMatrix4x4& m, float* out) {
    out[0]=m.a1;  out[4]=m.a2;  out[8]=m.a3;   out[12]=m.a4;
    out[1]=m.b1;  out[5]=m.b2;  out[9]=m.b3;   out[13]=m.b4;
    out[2]=m.c1;  out[6]=m.c2;  out[10]=m.c3;  out[14]=m.c4;
    out[3]=m.d1;  out[7]=m.d2;  out[11]=m.d3;  out[15]=m.d4;
}

bool decodeImageBytes(const uint8_t* data, size_t size, Fmt::Texture& tex) {
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
    if (!pixels) return false;
    tex.width  = (uint32_t)w;
    tex.height = (uint32_t)h;
    tex.pixels.assign(pixels, pixels + (size_t)w * (size_t)h * 4);
    stbi_image_free(pixels);
    return true;
}

struct ModelImportCtx {
    const aiScene*                       scene;
    fs::path                             baseDir; // for resolving external texture paths
    std::unordered_map<std::string, uint32_t> textureCache; // resolved key -> index in out.textures
    Fmt::Model                           out;
};

// Resolves + loads (if not already cached) the first texture of `type` on
// `mat`. Handles both embedded textures (glTF/fbx can pack image bytes
// straight into the scene) and textures referenced by external file path.
// Returns kNoIndex if the slot is empty or the image failed to decode.
uint32_t resolveTexture(ModelImportCtx& ctx, const aiMaterial* mat, aiTextureType type) {
    if (mat->GetTextureCount(type) == 0) return Fmt::kNoIndex;

    aiString path;
    if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return Fmt::kNoIndex;

    std::string key = path.C_Str();
    auto cached = ctx.textureCache.find(key);
    if (cached != ctx.textureCache.end()) return cached->second;

    Fmt::Texture tex;
    bool ok = false;

    if (const aiTexture* embedded = ctx.scene->GetEmbeddedTexture(path.C_Str())) {
        if (embedded->mHeight == 0) {
            // Compressed image (png/jpg) sitting in memory — mWidth is the
            // byte length of the compressed blob in this case.
            ok = decodeImageBytes((const uint8_t*)embedded->pcData, embedded->mWidth, tex);
        } else {
            // Raw texel array, BGRA order (aiTexel).
            tex.width  = embedded->mWidth;
            tex.height = embedded->mHeight;
            tex.pixels.resize((size_t)tex.width * tex.height * 4);
            for (size_t i = 0; i < (size_t)tex.width * tex.height; i++) {
                const aiTexel& t = embedded->pcData[i];
                tex.pixels[i * 4 + 0] = t.r;
                tex.pixels[i * 4 + 1] = t.g;
                tex.pixels[i * 4 + 2] = t.b;
                tex.pixels[i * 4 + 3] = t.a;
            }
            ok = true;
        }
    } else {
        fs::path full = ctx.baseDir / path.C_Str();
        std::vector<uint8_t> bytes = readFile(full);
        if (!bytes.empty()) ok = decodeImageBytes(bytes.data(), bytes.size(), tex);
        if (!ok) fprintf(stderr, "dustpacker:   texture '%s' failed to load, material slot left empty\n", full.string().c_str());
    }

    if (!ok) {
        ctx.textureCache[key] = Fmt::kNoIndex;
        return Fmt::kNoIndex;
    }

    uint32_t idx = (uint32_t)ctx.out.textures.size();
    ctx.out.textures.push_back(std::move(tex));
    ctx.textureCache[key] = idx;
    return idx;
}

void convertMaterials(ModelImportCtx& ctx) {
    ctx.out.materials.resize(ctx.scene->mNumMaterials);
    for (unsigned i = 0; i < ctx.scene->mNumMaterials; i++) {
        const aiMaterial* mat = ctx.scene->mMaterials[i];
        Fmt::Material&    m   = ctx.out.materials[i];

        aiColor4D baseColor;
        if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS ||
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
            m.baseColor[0] = baseColor.r; m.baseColor[1] = baseColor.g;
            m.baseColor[2] = baseColor.b; m.baseColor[3] = baseColor.a;
        }

        float metallic, roughness;
        if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)   m.metallic  = metallic;
        if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) m.roughness = roughness;

        aiColor3D emissive;
        if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
            m.emissive[0] = emissive.r; m.emissive[1] = emissive.g; m.emissive[2] = emissive.b;
        }

        m.baseColorTexture = resolveTexture(ctx, mat, aiTextureType_BASE_COLOR);
        if (m.baseColorTexture == Fmt::kNoIndex)
            m.baseColorTexture = resolveTexture(ctx, mat, aiTextureType_DIFFUSE); // obj/fbx fallback

        m.normalTexture = resolveTexture(ctx, mat, aiTextureType_NORMALS);

        m.metallicRoughnessTexture = resolveTexture(ctx, mat, aiTextureType_GLTF_METALLIC_ROUGHNESS);
        if (m.metallicRoughnessTexture == Fmt::kNoIndex)
            m.metallicRoughnessTexture = resolveTexture(ctx, mat, aiTextureType_METALNESS);

        m.emissiveTexture = resolveTexture(ctx, mat, aiTextureType_EMISSIVE);

        m.occlusionTexture = resolveTexture(ctx, mat, aiTextureType_AMBIENT_OCCLUSION);
        if (m.occlusionTexture == Fmt::kNoIndex)
            m.occlusionTexture = resolveTexture(ctx, mat, aiTextureType_LIGHTMAP);
    }
}

void convertNode(ModelImportCtx& ctx, const aiNode* node, const aiMatrix4x4& parentTransform) {
    aiMatrix4x4 world = parentTransform * node->mTransformation;

    for (unsigned i = 0; i < node->mNumMeshes; i++) {
        const aiMesh* mesh = ctx.scene->mMeshes[node->mMeshes[i]];
        if (!mesh->HasFaces() || !mesh->HasPositions()) continue;

        Fmt::Submesh sm;
        sm.materialIndex = mesh->mMaterialIndex < ctx.out.materials.size()
                            ? mesh->mMaterialIndex : Fmt::kNoIndex;
        toColumnMajor(world, sm.transform);

        sm.vertices.reserve(mesh->mNumVertices);
        for (unsigned v = 0; v < mesh->mNumVertices; v++) {
            Fmt::Vertex vert{};
            vert.position[0] = mesh->mVertices[v].x;
            vert.position[1] = mesh->mVertices[v].y;
            vert.position[2] = mesh->mVertices[v].z;

            if (mesh->HasNormals()) {
                vert.normal[0] = mesh->mNormals[v].x;
                vert.normal[1] = mesh->mNormals[v].y;
                vert.normal[2] = mesh->mNormals[v].z;
            }
            if (mesh->HasTextureCoords(0)) {
                vert.uv[0] = mesh->mTextureCoords[0][v].x;
                vert.uv[1] = mesh->mTextureCoords[0][v].y;
            }
            if (mesh->HasVertexColors(0)) {
                vert.color[0] = mesh->mColors[0][v].r;
                vert.color[1] = mesh->mColors[0][v].g;
                vert.color[2] = mesh->mColors[0][v].b;
                vert.color[3] = mesh->mColors[0][v].a;
            } else {
                vert.color[0] = vert.color[1] = vert.color[2] = vert.color[3] = 1.0f;
            }
            sm.vertices.push_back(vert);
        }

        sm.indices.reserve((size_t)mesh->mNumFaces * 3);
        for (unsigned f = 0; f < mesh->mNumFaces; f++) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue; // aiProcess_Triangulate guarantees this
            sm.indices.push_back(face.mIndices[0]);
            sm.indices.push_back(face.mIndices[1]);
            sm.indices.push_back(face.mIndices[2]);
        }

        ctx.out.submeshes.push_back(std::move(sm));
    }

    for (unsigned i = 0; i < node->mNumChildren; i++)
        convertNode(ctx, node->mChildren[i], world);
}

// Converts any assimp-supported model file into DustModel binary bytes.
// Returns empty on failure — caller logs and skips packing that asset.
std::vector<uint8_t> convertModelToDustBinary(const fs::path& path) {
    Assimp::Importer importer;
    unsigned flags = aiProcess_Triangulate
                    | aiProcess_JoinIdenticalVertices
                    | aiProcess_GenNormals          // no-op if the file already has normals
                    | aiProcess_ImproveCacheLocality
                    | aiProcess_FlipUVs              // matches stb_image's top-down row order
                    | aiProcess_GenUVCoords
                    | aiProcess_ValidateDataStructure;

    const aiScene* scene = importer.ReadFile(path.string(), flags);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        fprintf(stderr, "dustpacker: assimp failed on '%s': %s\n",
                path.string().c_str(), importer.GetErrorString());
        return {};
    }

    ModelImportCtx ctx;
    ctx.scene   = scene;
    ctx.baseDir = path.parent_path();
    convertMaterials(ctx);

    aiMatrix4x4 identity;
    convertNode(ctx, scene->mRootNode, identity);

    if (ctx.out.submeshes.empty()) {
        fprintf(stderr, "dustpacker: '%s' produced no renderable submeshes\n", path.string().c_str());
        return {};
    }

    printf("dustpacker:   -> %zu submesh(es), %zu material(s), %zu texture(s)\n",
           ctx.out.submeshes.size(), ctx.out.materials.size(), ctx.out.textures.size());
    return Fmt::serialize(ctx.out);
}

// Compress `raw` with zstd, then AEAD-encrypt it with a fresh random nonce.
// Fills in everything on `entry` except name/offset.
bool packOne(const std::vector<uint8_t>& raw, const uint8_t key[kKeyBytes],
             TocEntry& entry, std::vector<uint8_t>& outCipher) {
    size_t bound = ZSTD_compressBound(raw.size());
    std::vector<uint8_t> compressed(bound);
    size_t compSize = ZSTD_compress(compressed.data(), bound,
                                    raw.data(), raw.size(), 19 /* offline, favor ratio */);
    if (ZSTD_isError(compSize)) {
        fprintf(stderr, "dustpacker: zstd compress failed: %s\n", ZSTD_getErrorName(compSize));
        return false;
    }
    compressed.resize(compSize);

    randombytes_buf(entry.nonce, kNonceBytes);
    outCipher.resize(compSize + kTagBytes);
    unsigned long long cipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        outCipher.data(), &cipherLen,
        compressed.data(), compressed.size(),
        nullptr, 0, nullptr, entry.nonce, key);
    outCipher.resize((size_t)cipherLen);

    entry.compressedSize   = outCipher.size();
    entry.uncompressedSize = raw.size();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: DustPacker <assetFolder> <output.pack> <passphrase>\n");
        return 1;
    }
    fs::path inputDir  = argv[1];
    fs::path outputPath = argv[2];
    std::string passphrase = argv[3];

    if (sodium_init() < 0) {
        fprintf(stderr, "dustpacker: libsodium init failed\n");
        return 1;
    }
    if (!fs::is_directory(inputDir)) {
        fprintf(stderr, "dustpacker: '%s' is not a directory\n", inputDir.string().c_str());
        return 1;
    }

    uint8_t key[kKeyBytes];
    crypto_generichash(key, kKeyBytes,
                        (const unsigned char*)passphrase.data(), passphrase.size(),
                        nullptr, 0);

    std::vector<TocEntry>            entries;
    std::vector<std::vector<uint8_t>> blobs;

    for (auto& p : fs::recursive_directory_iterator(inputDir)) {
        if (!p.is_regular_file()) continue;

        fs::path rel = fs::relative(p.path(), inputDir);
        std::string name = rel.generic_string(); // forward slashes, portable

        std::vector<uint8_t> raw;
        bool isModel = isModelExtension(p.path().extension().string());
        bool isFont  = isFontExtension(p.path().extension().string());
        if (isModel) {
            printf("dustpacker: importing %-40s (via assimp)\n", name.c_str());
            raw = convertModelToDustBinary(p.path());
            if (raw.empty()) {
                fprintf(stderr, "dustpacker: skipping '%s' — model import failed\n", name.c_str());
                continue;
            }
            // Store under a normalized extension so AssetManager/Engine know
            // to run the DustModel binary deserializer, not guess from the
            // original format.
            fs::path renamed = rel;
            renamed.replace_extension(".model");
            name = renamed.generic_string();
        } else if (isFont) {
            printf("dustpacker: importing %-40s (via msdfgen)\n", name.c_str());
            raw = DustPacker::convertFontToDustBinary(p.path());
            if (raw.empty()) {
                fprintf(stderr, "dustpacker: skipping '%s' — font import failed\n", name.c_str());
                continue;
            }
            fs::path renamed = rel;
            renamed.replace_extension(".font");
            name = renamed.generic_string();
        } else {
            raw = readFile(p.path());
        }

        TocEntry entry;
        entry.name = name;
        std::vector<uint8_t> cipher;
        if (!packOne(raw, key, entry, cipher)) {
            fprintf(stderr, "dustpacker: failed to pack '%s'\n", name.c_str());
            return 1;
        }

        printf("dustpacker: %-40s %8zu -> %8zu bytes\n", name.c_str(), raw.size(), cipher.size());
        entries.push_back(std::move(entry));
        blobs.push_back(std::move(cipher));
    }

    if (entries.empty()) {
        fprintf(stderr, "dustpacker: no files found under '%s'\n", inputDir.string().c_str());
        return 1;
    }

    // Offsets are placeholders (0) for this pass — the serialized TOC size
    // doesn't depend on their value (fixed-width u64 fields), only on the
    // count and name lengths, so this is safe to do before offsets exist.
    std::vector<uint8_t> tocPlainSized = serializeToc(entries);

    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version      = kVersion;
    header.assetCount   = (uint32_t)entries.size();
    header.tocCipherLen = tocPlainSized.size() + kTagBytes;

    uint64_t offset = sizeof(FileHeader) + header.tocCipherLen;
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].offset = offset;
        offset += blobs[i].size();
    }

    // Re-serialize now that real offsets are filled in, then encrypt.
    std::vector<uint8_t> tocPlain = serializeToc(entries);
    randombytes_buf(header.tocNonce, kNonceBytes);
    std::vector<uint8_t> tocCipher(tocPlain.size() + kTagBytes);
    unsigned long long tocCipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        tocCipher.data(), &tocCipherLen,
        tocPlain.data(), tocPlain.size(),
        nullptr, 0, nullptr, header.tocNonce, key);
    tocCipher.resize((size_t)tocCipherLen);
    header.tocCipherLen = tocCipher.size();

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        fprintf(stderr, "dustpacker: failed to open '%s' for writing\n", outputPath.string().c_str());
        return 1;
    }
    out.write((const char*)&header, sizeof(header));
    out.write((const char*)tocCipher.data(), (std::streamsize)tocCipher.size());
    for (auto& blob : blobs)
        out.write((const char*)blob.data(), (std::streamsize)blob.size());

    printf("dustpacker: wrote '%s' — %zu assets\n", outputPath.string().c_str(), entries.size());
    return 0;
}
