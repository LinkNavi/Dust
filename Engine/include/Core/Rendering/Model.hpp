#pragma once

#include "Core/Rendering/Mesh.hpp"
#include "Core/Rendering/Texture.hpp"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Dust {

struct VulkanContext;
struct Renderer;

struct Material {
    float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic     = 0.0f;
    float roughness    = 1.0f;
    float emissive[3]  = { 0.0f, 0.0f, 0.0f };

    // Indices into Model::textures, or -1 if that slot is absent. Only
    // baseColorTexture is sampled by the current unlit default shader — the
    // others are decoded and sitting on the GPU (linear, not sRGB — see
    // Texture::upload) ready for a future lit shader, no re-import needed
    // when that lands.
    int baseColorTexture         = -1;
    int normalTexture            = -1;
    int metallicRoughnessTexture = -1;
    int emissiveTexture          = -1;
    int occlusionTexture         = -1;

    VkDescriptorSet materialSet = VK_NULL_HANDLE; // set=0 for the unlit default pipeline — bound at draw time

    // set=0 for the lit pipeline (Renderer::litMaterialSetLayout) — all five
    // maps this material has, defaults filled in for whatever's absent (see
    // Renderer::createLitMaterialSet). Built alongside materialSet so
    // DustEngine::drawModel can pick either at draw time with no extra work.
    VkDescriptorSet litMaterialSet = VK_NULL_HANDLE;
};

struct Submesh {
    Mesh      mesh;
    int       materialIndex = -1;   // index into Model::materials, or -1
    glm::mat4 transform{ 1.0f };    // baked node-to-model transform from the source file
};

// A loaded model asset: one or more submeshes, each with an optional
// material/textures. The one type behind DustEngine::loadModel()/
// loadModelFromPack() — a plain OBJ produces a trivial single submesh with
// no material, an assimp-imported file (fbx/gltf/glb/dae/stl/ply/3ds — see
// DustPacker + AssetManager/ModelFormat.hpp) produces the full thing. Same
// verbs either way: loadModel/drawModel/unloadModel.
//
// Distinct from `Mesh` — the raw, procedural, vert-editable geometry
// primitive (Mesh::makeTriangle(), addVert/addFace, ...), which has no
// concept of materials or file loading and is drawn with drawMesh().
struct Model {
    std::vector<Texture>  textures;
    std::vector<Material> materials;
    std::vector<Submesh>  submeshes;

    // Convenience for the common single-submesh case (any OBJ-loaded model,
    // or an imported one that just happens to be a single piece) — avoids
    // reaching through .submeshes[0].mesh for basic vert-editing.
    Mesh&       mesh(size_t i = 0)       { return submeshes[i].mesh; }
    const Mesh& mesh(size_t i = 0) const { return submeshes[i].mesh; }

    // Frees every GPU resource this model owns (meshes, textures,
    // descriptor sets). Caller is responsible for vkDeviceWaitIdle first if
    // the model might still be in flight.
    void destroy(VulkanContext& ctx, Renderer& renderer);
};

// Parses DustModel binary bytes (ModelFormat.hpp) and uploads everything —
// textures, materials (+ descriptor sets), submesh meshes — to the GPU.
// Returns an empty Model (no submeshes) if the bytes don't parse.
Model loadModelFromMemory(VulkanContext& ctx, Renderer& renderer,
                          const uint8_t* data, size_t size);

// Wraps an already-uploaded single Mesh (e.g. the plain-text OBJ loader) as
// a trivial one-submesh, no-material Model — so loadModel()/loadModelFromPack()
// can return the same type regardless of source format.
Model wrapMesh(Mesh&& mesh);

} // namespace Dust
