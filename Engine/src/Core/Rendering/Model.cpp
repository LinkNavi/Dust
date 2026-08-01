#include "Core/Rendering/Model.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include "Core/Rendering/Renderer.hpp"
#include "AssetManager/ModelFormat.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cstdio>
#include <cstring>

namespace Dust {

namespace {
int toIndex(uint32_t raw) { return raw == ModelFmt::kNoIndex ? -1 : (int)raw; }
}

void Model::destroy(VulkanContext& ctx, Renderer& renderer) {
    for (auto& sm : submeshes) sm.mesh.destroy();
    submeshes.clear();

    for (auto& mat : materials)
        if (mat.materialSet != VK_NULL_HANDLE)
            vkFreeDescriptorSets(ctx.device, renderer.materialPool, 1, &mat.materialSet);
    materials.clear();

    for (auto& tex : textures) tex.destroy(ctx);
    textures.clear();
}

Model wrapMesh(Mesh&& mesh) {
    Model out;
    Submesh sm;
    sm.mesh = std::move(mesh);
    sm.materialIndex = -1; // no material — draws with the white fallback texture
    out.submeshes.push_back(std::move(sm));
    return out;
}

Model loadModelFromMemory(VulkanContext& ctx, Renderer& renderer,
                          const uint8_t* data, size_t size) {
    Model out;

    ModelFmt::Model parsed;
    if (!ModelFmt::deserialize(data, size, parsed)) {
        fprintf(stderr, "dust: failed to parse DustModel binary (corrupt, or packed by an older DustPacker?)\n");
        return out;
    }

    // sRGB vs linear is a per-usage decision, not a per-file one — the same
    // image bytes could in principle be reused as both, though in practice
    // each texture in an imported scene only ever fills one material slot.
    // Base color/emissive are color data (sRGB); everything else (normal,
    // metallic/roughness, occlusion) is data and must stay linear.
    std::vector<bool> isColorTexture(parsed.textures.size(), false);
    for (auto& m : parsed.materials) {
        if (m.baseColorTexture != ModelFmt::kNoIndex && m.baseColorTexture < isColorTexture.size())
            isColorTexture[m.baseColorTexture] = true;
        if (m.emissiveTexture != ModelFmt::kNoIndex && m.emissiveTexture < isColorTexture.size())
            isColorTexture[m.emissiveTexture] = true;
    }

    out.textures.resize(parsed.textures.size());
    for (size_t i = 0; i < parsed.textures.size(); i++) {
        auto& t = parsed.textures[i];
        if (!out.textures[i].upload(ctx, t.pixels.data(), t.width, t.height, isColorTexture[i]))
            fprintf(stderr, "dust: texture %zu failed to upload\n", i);
    }

    out.materials.reserve(parsed.materials.size());
    for (auto& m : parsed.materials) {
        Material mat;
        memcpy(mat.baseColor, m.baseColor, sizeof(mat.baseColor));
        mat.metallic  = m.metallic;
        mat.roughness = m.roughness;
        memcpy(mat.emissive, m.emissive, sizeof(mat.emissive));
        mat.baseColorTexture         = toIndex(m.baseColorTexture);
        mat.normalTexture            = toIndex(m.normalTexture);
        mat.metallicRoughnessTexture = toIndex(m.metallicRoughnessTexture);
        mat.emissiveTexture          = toIndex(m.emissiveTexture);
        mat.occlusionTexture         = toIndex(m.occlusionTexture);

        const Texture& colorTex = (mat.baseColorTexture >= 0 && (size_t)mat.baseColorTexture < out.textures.size())
                                 ? out.textures[mat.baseColorTexture]
                                 : renderer.defaultWhiteTexture;
        mat.materialSet = renderer.createMaterialSet(ctx, colorTex);

        out.materials.push_back(mat);
    }

    out.submeshes.reserve(parsed.submeshes.size());
    for (auto& sm : parsed.submeshes) {
        Submesh submesh;
        submesh.materialIndex = toIndex(sm.materialIndex);
        submesh.transform     = glm::make_mat4(sm.transform);

        std::vector<VertHandle> handles;
        handles.reserve(sm.vertices.size());
        for (auto& v : sm.vertices) {
            Vertex vert{};
            memcpy(vert.position, v.position, sizeof(vert.position));
            memcpy(vert.normal,   v.normal,   sizeof(vert.normal));
            memcpy(vert.uv,       v.uv,       sizeof(vert.uv));
            memcpy(vert.color,    v.color,    sizeof(vert.color));
            handles.push_back(submesh.mesh.addVert(vert));
        }

        for (size_t i = 0; i + 2 < sm.indices.size(); i += 3)
            submesh.mesh.addFace(handles[sm.indices[i]], handles[sm.indices[i + 1]], handles[sm.indices[i + 2]]);

        submesh.mesh.upload(ctx);
        out.submeshes.push_back(std::move(submesh));
    }

    return out;
}

} // namespace Dust
