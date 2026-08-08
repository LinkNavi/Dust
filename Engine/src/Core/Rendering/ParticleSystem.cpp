#include "Core/Rendering/ParticleSystem.hpp"
#include <cstring>
#include <algorithm>

namespace Dust {

void ParticleSystem::init(VmaAllocator allocator, VkDevice device,
                          uint32_t graphicsFamily, VkQueue graphicsQueue,
                          uint32_t maxParticles) {
    m_allocator      = allocator;
    m_device         = device;
    m_graphicsFamily = graphicsFamily;
    m_graphicsQueue  = graphicsQueue;
    m_maxParticles   = maxParticles;

    // Dual-use buffer: STORAGE for the compute shader to write,
    // VERTEX for the graphics pipeline to read as instance data.
    // Persistently mapped so emit() can write new particles from the CPU.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = sizeof(Particle) * m_maxParticles;
    bufferInfo.usage       = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT  |
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultInfo{};
    vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo,
                    &m_particleBuffer, &m_allocation, &resultInfo);
    m_mappedParticles = static_cast<Particle*>(resultInfo.pMappedData);

    // Zero the buffer — all slots start dead (life <= 0)
    if (m_mappedParticles)
        memset(m_mappedParticles, 0, sizeof(Particle) * m_maxParticles);
}

void ParticleSystem::cleanup() {
    // The buffer is still referenced by the last submitted frame's command
    // buffer — destroying it before the GPU drains is a use-after-free.
    if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
    if (m_particleBuffer != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_particleBuffer, m_allocation);
        m_particleBuffer  = VK_NULL_HANDLE;
        m_allocation      = VK_NULL_HANDLE;
        m_mappedParticles = nullptr;
    }
}

void ParticleSystem::emit(const Particle& particle) {
    if (!m_mappedParticles) return;
    // Round-robin through slots — find a dead slot, write directly into the
    // GPU buffer. No memcpy needed; this IS the GPU buffer.
    for (uint32_t i = 0; i < m_maxParticles; ++i) {
        uint32_t slot = (m_emitCursor + i) % m_maxParticles;
        if (m_mappedParticles[slot].life <= 0.0f) {
            m_mappedParticles[slot] = particle;
            m_emitCursor = (slot + 1) % m_maxParticles;
            return;
        }
    }
    // All slots alive — overwrite cursor slot (oldest wraps around)
    m_mappedParticles[m_emitCursor] = particle;
    m_emitCursor = (m_emitCursor + 1) % m_maxParticles;
}

// Binding 1, locations 4-8, matching particle.vert and particles.comp std430 layout.
std::vector<InstanceAttrib> ParticleSystem::getInstanceAttribs() {
    return {
        { 4, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Particle, pos)   },  // loc 4: instPos
        { 5, VK_FORMAT_R32_SFLOAT,          offsetof(Particle, life)  },  // loc 5: instLife
        { 6, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Particle, vel)   },  // loc 6: instVel (stride filler)
        { 7, VK_FORMAT_R32_SFLOAT,          offsetof(Particle, size)  },  // loc 7: instSize
        { 8, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Particle, color) },  // loc 8: instColor
    };
}

uint32_t ParticleSystem::instanceStride() {
    return sizeof(Particle);
}

} // namespace Dust
