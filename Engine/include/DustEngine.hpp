// DustEngine.hpp
#pragma once
#include "Core/Window.hpp"
#include "DustECS.hpp"
#include "Core/Systems/Entity.hpp"
#include "Core/Rendering/VulkanContext.hpp"
#include <functional>
#include <list>

namespace Dust {

struct DustEngine {
    WindowManager  windows;
        VulkanContext  vulkan;
        ecs::Registry  ecs;
        std::list<Entity> entities;
        Entity         root;
        bool           running = true;

    Entity* createEntity(const char* name, Entity* parent = nullptr);
    bool    init(const char* appName = "Dust");
    void    shutdown();
    void    run(std::function<void(float dt)> onUpdate);
    void    stop();
};

} // namespace Dust
