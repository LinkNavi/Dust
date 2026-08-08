#include "Core/Rendering/ParticleSystem.hpp"
#include <cstring>
#include <algorithm>

namespace Dust {

void ParticleSystem::init(VmaAllocator allocator, uint32_t maxParticles) {
    m_allocator = allocator;
    m_maxParticles = maxParticles;

    m_particles.resize(m_maxParticles);
    m_instances.resize(m_maxParticles);

    // 1. Configure VkBufferCreateInfo
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(ParticleInstance) * m_maxParticles;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // 2. Configure VMA Allocation
    // HOST_ACCESS_SEQUENTIAL_WRITE + MAPPED_BIT creates a host-visible, persistently mapped buffer
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo resultAllocInfo{};
    vmaCreateBuffer(
        m_allocator,
        &bufferInfo,
        &allocInfo,
        &m_instanceBuffer,
        &m_allocation,
        &resultAllocInfo
    );

    // Store persistently mapped pointer
    m_mappedData = resultAllocInfo.pMappedData;
}

void ParticleSystem::cleanup() {
    if (m_instanceBuffer != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, m_instanceBuffer, m_allocation);
        m_instanceBuffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mappedData = nullptr;
    }
}

void ParticleSystem::emit(const Particle& particle) {
    // Find first available slot (life <= 0) or replace oldest
    for (auto& p : m_particles) {
        if (p.life <= 0.0f) {
            p = particle;
            return;
        }
    }
}

void ParticleSystem::update(float dt) {
    m_activeParticleCount = 0;

    for (auto& p : m_particles) {
        if (p.life <= 0.0f) continue;

        // Simulate physics on CPU
        p.life -= dt;
        p.pos += p.vel * dt;

        // Pack alive particles for the GPU instance stream
        if (p.life > 0.0f) {
            ParticleInstance& inst = m_instances[m_activeParticleCount++];
            inst.pos = p.pos;
            inst.size = p.size;
            inst.color = p.color;
            inst.life = p.life;
        }
    }

    // Direct memory write to GPU without needing vkMapMemory/vkUnmapMemory calls
    if (m_mappedData && m_activeParticleCount > 0) {
        std::memcpy(
            m_mappedData,
            m_instances.data(),
            sizeof(ParticleInstance) * m_activeParticleCount
        );
    }
}

void ParticleSystem::render(VkCommandBuffer cmd) {
    if (m_activeParticleCount == 0) return;

    // 1. Bind instance buffer as vertex buffer 0
    VkBuffer buffers[] = { m_instanceBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    // 2. Draw 6 procedural vertices per active particle instance
    vkCmdDraw(cmd, 6, m_activeParticleCount, 0, 0);
}

VkVertexInputBindingDescription ParticleSystem::getBindingDescription() {
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(ParticleInstance);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE; // Read per instance
    return bindingDesc;
}

std::vector<VkVertexInputAttributeDescription> ParticleSystem::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attribs(4);

    // location 0: vec3 instPosition
    attribs[0].binding = 0;
    attribs[0].location = 0;
    attribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attribs[0].offset = offsetof(ParticleInstance, pos);

    // location 1: float instSize
    attribs[1].binding = 0;
    attribs[1].location = 1;
    attribs[1].format = VK_FORMAT_R32_SFLOAT;
    attribs[1].offset = offsetof(ParticleInstance, size);

    // location 2: vec4 instColor
    attribs[2].binding = 0;
    attribs[2].location = 2;
    attribs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribs[2].offset = offsetof(ParticleInstance, color);

    // location 3: float instLife
    attribs[3].binding = 0;
    attribs[3].location = 3;
    attribs[3].format = VK_FORMAT_R32_SFLOAT;
    attribs[3].offset = offsetof(ParticleInstance, life);

    return attribs;
}

} // namespace Dust
