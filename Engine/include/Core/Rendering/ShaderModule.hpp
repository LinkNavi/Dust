#pragma once

#include "vulkan/vulkan_core.h"
#include <vulkan/vulkan.h>
#include <string_view>

namespace Dust {

struct ShaderModule {
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    VkShaderModule comp = VK_NULL_HANDLE;

    // Load from SPIR-V files
    static ShaderModule load(VkDevice device,
                             std::string_view vertPath,
                             std::string_view fragPath);

    // Load from embedded bytes (for default shaders)
    static ShaderModule fromBytes(VkDevice device,
                                  const uint32_t* vertData, size_t vertSize,
                                  const uint32_t* fragData, size_t fragSize);

    void destroy(VkDevice device);
    bool valid() const { return vert != VK_NULL_HANDLE && frag != VK_NULL_HANDLE; }
};

} // namespace Dust
