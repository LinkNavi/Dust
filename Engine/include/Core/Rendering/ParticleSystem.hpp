#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace Dust {

    // CPU Simulation State
    struct Particle {
        glm::vec3 pos{0.0f};
        float life{0.0f};
        glm::vec3 vel{0.0f};
        float size{1.0f};
        glm::vec4 color{1.0f};
    };

    // GPU Instance Layout (location 0..3 in vertex shader)
    struct ParticleInstance {
        glm::vec3 pos;   // location = 0
        float size;      // location = 1
        glm::vec4 color; // location = 2
        float life;      // location = 3
    };

    class ParticleSystem {
    public:
        ParticleSystem() = default;
        ~ParticleSystem() = default;

        void init(VmaAllocator allocator, uint32_t maxParticles = 10000);
        void cleanup();

        void emit(const Particle& particle);
        void update(float dt);
        void render(VkCommandBuffer cmd);

        uint32_t getActiveCount() const { return m_activeParticleCount; }

        // Helper to get pipeline vertex attribute descriptions for C++ pipeline setup
        static VkVertexInputBindingDescription getBindingDescription();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

    private:
        VmaAllocator m_allocator{VK_NULL_HANDLE};
        uint32_t m_maxParticles{0};
        uint32_t m_activeParticleCount{0};

        std::vector<Particle> m_particles;
        std::vector<ParticleInstance> m_instances;

        VkBuffer m_instanceBuffer{VK_NULL_HANDLE};
        VmaAllocation m_allocation{VK_NULL_HANDLE};
        void* m_mappedData{nullptr};
    };

} // namespace Dust
