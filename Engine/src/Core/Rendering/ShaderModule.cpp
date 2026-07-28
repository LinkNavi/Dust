#include "Core/Rendering/ShaderModule.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Dust {

static VkShaderModule createModule(VkDevice device, const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info{};
    info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = size;
    info.pCode    = code;

    VkShaderModule mod;
    if (vkCreateShaderModule(device, &info, nullptr, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return mod;
}

static std::vector<uint32_t> readSpv(std::string_view path) {
    FILE* f = fopen(path.data(), "rb");
    if (!f) {
        fprintf(stderr, "dust: failed to open shader '%s'\n", path.data());
        return {};
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    rewind(f);

    std::vector<uint32_t> buf(size / 4);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

ShaderModule ShaderModule::load(VkDevice device,
                                std::string_view vertPath,
                                std::string_view fragPath) {
    ShaderModule sm;

    auto vertSpv = readSpv(vertPath);
    auto fragSpv = readSpv(fragPath);
    if (vertSpv.empty() || fragSpv.empty()) return sm;

    sm.vert = createModule(device, vertSpv.data(), vertSpv.size() * 4);
    sm.frag = createModule(device, fragSpv.data(), fragSpv.size() * 4);

    if (!sm.valid())
        fprintf(stderr, "dust: failed to create shader modules\n");

    return sm;
}

ShaderModule ShaderModule::fromBytes(VkDevice device,
                                     const uint32_t* vertData, size_t vertSize,
                                     const uint32_t* fragData, size_t fragSize) {
    ShaderModule sm;
    sm.vert = createModule(device, vertData, vertSize);
    sm.frag = createModule(device, fragData, fragSize);
    return sm;
}

void ShaderModule::destroy(VkDevice device) {
    if (vert) vkDestroyShaderModule(device, vert, nullptr);
    if (frag) vkDestroyShaderModule(device, frag, nullptr);
    vert = frag = VK_NULL_HANDLE;
}

} // namespace Dust
