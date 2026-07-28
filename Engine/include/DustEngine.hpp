// DustEngine.hpp
#pragma once
#include "core/Window.hpp"
#include "DustECS.hpp"
#include "core/systems/Entity.hpp"
#include <functional>
#include <list>

namespace Dust {

struct DustEngine {
    Entity              root;
    WindowManager       windows;
    ecs::Registry       ecs;
    std::list<Entity>   entities;
    bool                running = true;

    Entity* createEntity(const char* name, Entity* parent = nullptr);
    bool    init(const char* appName = "Dust");
    void    shutdown();
    void    run(std::function<void(float dt)> onUpdate);
    void    stop();
};

} // namespace Dust
