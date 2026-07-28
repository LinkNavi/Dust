#pragma once
#include "DustECS.hpp"
#include <string>
#include <vector>
 #include <algorithm>
namespace Dust {

struct Entity {
    ecs::Entity             handle;
    std::string             name;
    ecs::Registry*          registry = nullptr;
    Entity*                 parent   = nullptr;
    std::vector<Entity*>    children;

    // Component shortcuts
    template<typename T>
    void add(T component) { registry->add<T>(handle, std::move(component)); }

    template<typename T>
    T* get() { return registry->get<T>(handle); }

    template<typename T>
    bool has() const { return registry->has<T>(handle); }

    template<typename T>
    void remove() { registry->remove<T>(handle); }

    bool alive() const { return registry && registry->alive(handle); }

    // Hierarchy


    void setParent(Entity* p) {
        if (parent) {
            auto& pc = parent->children;
            pc.erase(std::remove(pc.begin(), pc.end(), this), pc.end());
        }
        parent = p;
        if (parent)
            parent->children.push_back(this);
    }

    void addChild(Entity* child) {
        child->setParent(this);
    }

    void removeChild(Entity* child) {
        child->setParent(nullptr);
    }

    bool isRoot()  const { return parent == nullptr; }
    bool isLeaf()  const { return children.empty(); }
};

} // namespace Dust
