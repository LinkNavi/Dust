#define VMA_IMPLEMENTATION
#include "VulkanContext.hpp"
#include <cstdio>

namespace Dust {

bool VulkanContext::init(const char* appName, bool enableValidation) {
    validationEnabled = enableValidation;

    // ── Instance ──
    vkb::InstanceBuilder builder;
    builder.set_app_name(appName)
           .require_api_version(1, 0, 0); // baseline — auto-selects highest available

    if (enableValidation)
        builder.request_validation_layers()
               .use_default_debug_messenger();

    auto instResult = builder.build();
    if (!instResult) {
        fprintf(stderr, "dust: failed to create Vulkan instance: %s\n",
                instResult.error().message().c_str());
        return false;
    }
    vkbInstance = instResult.value();
    instance    = vkbInstance.instance;
    if (enableValidation)
        debugMessenger = vkbInstance.debug_messenger;

    // ── Physical device ──
    vkb::PhysicalDeviceSelector selector{ vkbInstance };
    selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
            .allow_any_gpu_device_type()
            .require_present();

    auto physResult = selector.select();
    if (!physResult) {
        fprintf(stderr, "dust: failed to select physical device: %s\n",
                physResult.error().message().c_str());
        return false;
    }
    vkbPhysical    = physResult.value();
    physicalDevice = vkbPhysical.physical_device;

    // ── Detect tier ──
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice, &props);

    uint32_t ver       = props.apiVersion;
    tier.vulkanMajor   = VK_VERSION_MAJOR(ver);
    tier.vulkanMinor   = VK_VERSION_MINOR(ver);
    tier.vulkanPatch   = VK_VERSION_PATCH(ver);
    tier.maxDescriptorSets = props.limits.maxBoundDescriptorSets;
    tier.maxTextureSize    = props.limits.maxImageDimension2D;

    // Memory budget
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    tier.deviceLocalMemoryBytes = 0;
    for (uint32_t i = 0; i < memProps.memoryHeapCount; i++)
        if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            tier.deviceLocalMemoryBytes += memProps.memoryHeaps[i].size;

    // Extension / version feature flags
    tier.dynamicRendering   = tier.atLeast(1, 3) ||
        vkbPhysical.is_extension_present(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    tier.descriptorIndexing = tier.atLeast(1, 2);
    tier.meshShaders        = vkbPhysical.is_extension_present(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    tier.raytracing         = vkbPhysical.is_extension_present(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);

    fprintf(stdout, "dust: GPU: %s | Vulkan %u.%u.%u\n",
            props.deviceName,
            tier.vulkanMajor, tier.vulkanMinor, tier.vulkanPatch);

    // ── Logical device ──
    vkb::DeviceBuilder deviceBuilder{ vkbPhysical };

    // Opt-in to supported features
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynRenderFeature{};
    dynRenderFeature.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynRenderFeature.dynamicRendering = tier.dynamicRendering ? VK_TRUE : VK_FALSE;
    if (tier.dynamicRendering)
        deviceBuilder.add_pNext(&dynRenderFeature);

    auto devResult = deviceBuilder.build();
    if (!devResult) {
        fprintf(stderr, "dust: failed to create logical device: %s\n",
                devResult.error().message().c_str());
        return false;
    }
    vkbDevice = devResult.value();
    device    = vkbDevice.device;

    graphicsQueue  = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    presentQueue   = vkbDevice.get_queue(vkb::QueueType::present).value();
    graphicsFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    presentFamily  = vkbDevice.get_queue_index(vkb::QueueType::present).value();

    // ── VMA ──
    VmaAllocatorCreateInfo vmaInfo{};
    vmaInfo.physicalDevice = physicalDevice;
    vmaInfo.device         = device;
    vmaInfo.instance       = instance;
    vmaInfo.vulkanApiVersion = VK_MAKE_VERSION(tier.vulkanMajor, tier.vulkanMinor, 0);

    if (vmaCreateAllocator(&vmaInfo, &allocator) != VK_SUCCESS) {
        fprintf(stderr, "dust: failed to create VMA allocator\n");
        return false;
    }

    return true;
}

void VulkanContext::shutdown() {
    if (allocator) {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (enableValidation && debugMessenger)
        vkb::destroy_debug_utils_messenger(instance, debugMessenger);
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

} // namespace Dust
