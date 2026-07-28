#pragma once

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>

namespace Dust {

// ─── DEVICE TIER ──────────────────────────────
// Detected at init. Gate features against this.

struct DeviceTier {
    uint32_t vulkanMajor;
    uint32_t vulkanMinor;
    uint32_t vulkanPatch;

    // Feature support flags
    bool dynamicRendering;   // Vulkan 1.3 / VK_KHR_dynamic_rendering
    bool meshShaders;        // VK_EXT_mesh_shader
    bool raytracing;         // VK_KHR_ray_tracing_pipeline
    bool descriptorIndexing; // Vulkan 1.2

    uint32_t maxDescriptorSets;
    uint32_t maxTextureSize;
    uint64_t deviceLocalMemoryBytes;

    // Helpers
    bool atLeast(uint32_t major, uint32_t minor) const {
        return vulkanMajor > major || (vulkanMajor == major && vulkanMinor >= minor);
    }
};

// ─── VULKAN CONTEXT ───────────────────────────

struct VulkanContext {
    VkInstance               instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;
    uint32_t                 graphicsFamily = 0;
    uint32_t                 presentFamily  = 0;
    VmaAllocator             allocator      = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    // vk-bootstrap internals (kept for swapchain rebuilds)
    vkb::Instance            vkbInstance;
    vkb::PhysicalDevice      vkbPhysical;
    vkb::Device              vkbDevice;

    DeviceTier               tier;
    bool                     validationEnabled = false;

    // Lifecycle
    bool init(const char* appName, bool enableValidation = false);
    void shutdown();

    // Accessors
    const DeviceTier& getDeviceTier() const { return tier; }
    bool supportsVersion(uint32_t major, uint32_t minor) const { return tier.atLeast(major, minor); }
};

} // namespace Dust
