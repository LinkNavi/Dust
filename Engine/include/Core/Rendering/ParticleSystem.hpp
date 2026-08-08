#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "Core/Rendering/PipelineBuilder.hpp" // InstanceAttrib

namespace Dust {

// Particle — std430 layout, matches particles.comp and particle.vert bindings.
// DO NOT reorder without updating the compute shader and particle.vert.
struct Particle {
    glm::vec3 pos{0.0f};   // offset  0  — location 4 in particle.vert
    float     life{0.0f};  // offset 12  — location 5
    glm::vec3 vel{0.0f};   // offset 16  — location 6 (unused in vert, stride filler)
    float     size{1.0f};  // offset 28  — location 7
    glm::vec4 color{1.0f}; // offset 32  — location 8 (read by billboard.vert; particle uses white+alpha)
};

// ParticleSystem owns the GPU buffer that both the compute shader writes to
// and the graphics pipeline reads as instance data. CPU side only handles
// emit() — physics runs entirely in particles.comp.
class ParticleSystem {
public:
    ParticleSystem() = default;
    ~ParticleSystem() = default;

    // Pass vulkan handles needed to dispatch the compute shader each frame.
    void init(VmaAllocator allocator, VkDevice device,
              uint32_t graphicsFamily, VkQueue graphicsQueue,
              uint32_t maxParticles = 10000);
    void cleanup();

    // Write a new particle directly into the GPU-mapped buffer.
    // Thread-safe as long as only one thread calls emit() at a time.
    void emit(const Particle& particle);

    uint32_t maxParticles()    const { return m_maxParticles; }
    VkBuffer particleBuffer()  const { return m_particleBuffer; }

    // PipelineBuilder helpers — binding 1, locations 4-8
    static std::vector<InstanceAttrib> getInstanceAttribs();
    static uint32_t                    instanceStride();

private:
    VmaAllocator m_allocator      = VK_NULL_HANDLE;
    VkDevice     m_device         = VK_NULL_HANDLE;
    uint32_t     m_graphicsFamily = 0;
    VkQueue      m_graphicsQueue  = VK_NULL_HANDLE;

    uint32_t  m_maxParticles   = 0;
    uint32_t  m_emitCursor     = 0;

    VkBuffer      m_particleBuffer  = VK_NULL_HANDLE;
    VmaAllocation m_allocation      = VK_NULL_HANDLE;
    Particle*     m_mappedParticles = nullptr;
};

} // namespace Dust
