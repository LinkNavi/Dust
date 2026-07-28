#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <functional>
#include <typeindex>
#include <memory>
#include <cassert>

namespace ecs {

// ─── ENTITY ───────────────────────────────────

struct Entity {
    uint32_t index;
    uint32_t gen;

    bool operator==(const Entity& o) const { return index == o.index && gen == o.gen; }
    bool operator!=(const Entity& o) const { return !(*this == o); }
};

constexpr Entity NULL_ENTITY = { UINT32_MAX, 0 };
inline bool valid(Entity e) { return e.index != UINT32_MAX; }

// ─── SPARSE SET ───────────────────────────────

struct SparseSetBase {
    virtual ~SparseSetBase() = default;
    virtual void remove(uint32_t index) = 0;
    virtual bool has(uint32_t index) const = 0;
};

template<typename T>
struct SparseSet : SparseSetBase {
    static constexpr uint32_t EMPTY = UINT32_MAX;

    std::vector<uint32_t> sparse;
    std::vector<uint32_t> dense;
    std::vector<T>        components;

    void ensure(uint32_t idx);
    void add(uint32_t idx, T component);
    void remove(uint32_t idx) override;
    bool has(uint32_t idx) const override;
    T*   get(uint32_t idx);
    void each(std::function<void(uint32_t, T&)> fn);
};

// ─── REGISTRY ─────────────────────────────────

class Registry {
public:
    Entity create();
    void   destroy(Entity e);
    bool   alive(Entity e) const;
    void   clear();
    size_t size() const;

    template<typename T> void add(Entity e, T component);
    template<typename T> T*   get(Entity e);
    template<typename T> bool has(Entity e) const;
    template<typename T> void remove(Entity e);

    template<typename T>
    void view(std::function<void(Entity, T&)> fn);

    template<typename A, typename B>
    void view(std::function<void(Entity, A&, B&)> fn);

    template<typename A, typename B, typename C>
    void view(std::function<void(Entity, A&, B&, C&)> fn);

private:
    std::vector<uint32_t>                                  gens_;
    std::vector<uint32_t>                                  free_;
    std::unordered_map<std::type_index,
                       std::unique_ptr<SparseSetBase>>     pools_;

    template<typename T>
    SparseSet<T>& pool();
};

// ─── SPARSE SET IMPL (template — must stay in header) ─────────────────────

template<typename T>
void SparseSet<T>::ensure(uint32_t idx) {
    if (idx >= sparse.size())
        sparse.resize(idx + 1, EMPTY);
}

template<typename T>
void SparseSet<T>::add(uint32_t idx, T component) {
    ensure(idx);
    if (sparse[idx] != EMPTY) {
        components[sparse[idx]] = std::move(component);
        return;
    }
    sparse[idx] = (uint32_t)dense.size();
    dense.push_back(idx);
    components.push_back(std::move(component));
}

template<typename T>
void SparseSet<T>::remove(uint32_t idx) {
    if (idx >= sparse.size() || sparse[idx] == EMPTY) return;
    uint32_t denseIdx   = sparse[idx];
    uint32_t lastEntity = dense.back();
    components[denseIdx] = std::move(components.back());
    dense[denseIdx]      = lastEntity;
    sparse[lastEntity]   = denseIdx;
    components.pop_back();
    dense.pop_back();
    sparse[idx] = EMPTY;
}

template<typename T>
bool SparseSet<T>::has(uint32_t idx) const {
    return idx < sparse.size() && sparse[idx] != EMPTY;
}

template<typename T>
T* SparseSet<T>::get(uint32_t idx) {
    if (!has(idx)) return nullptr;
    return &components[sparse[idx]];
}

template<typename T>
void SparseSet<T>::each(std::function<void(uint32_t, T&)> fn) {
    for (size_t i = 0; i < dense.size(); ++i)
        fn(dense[i], components[i]);
}

// ─── REGISTRY TEMPLATE IMPL (must stay in header) ─────────────────────────

template<typename T>
SparseSet<T>& Registry::pool() {
    auto key = std::type_index(typeid(T));
    auto it  = pools_.find(key);
    if (it == pools_.end()) {
        pools_[key] = std::make_unique<SparseSet<T>>();
        return *static_cast<SparseSet<T>*>(pools_[key].get());
    }
    return *static_cast<SparseSet<T>*>(it->second.get());
}

template<typename T>
void Registry::add(Entity e, T component) {
    assert(alive(e));
    pool<T>().add(e.index, std::move(component));
}

template<typename T>
T* Registry::get(Entity e) {
    if (!alive(e)) return nullptr;
    return pool<T>().get(e.index);
}

template<typename T>
bool Registry::has(Entity e) const {
    if (!alive(e)) return false;
    auto it = pools_.find(std::type_index(typeid(T)));
    if (it == pools_.end()) return false;
    return it->second->has(e.index);
}

template<typename T>
void Registry::remove(Entity e) {
    if (!alive(e)) return;
    pool<T>().remove(e.index);
}

template<typename T>
void Registry::view(std::function<void(Entity, T&)> fn) {
    pool<T>().each([&](uint32_t idx, T& c) {
        fn({ idx, gens_[idx] }, c);
    });
}

template<typename A, typename B>
void Registry::view(std::function<void(Entity, A&, B&)> fn) {
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    pa.each([&](uint32_t idx, A& a) {
        B* b = pb.get(idx);
        if (!b) return;
        fn({ idx, gens_[idx] }, a, *b);
    });
}

template<typename A, typename B, typename C>
void Registry::view(std::function<void(Entity, A&, B&, C&)> fn) {
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    auto& pc = pool<C>();
    pa.each([&](uint32_t idx, A& a) {
        B* b = pb.get(idx);
        if (!b) return;
        C* c = pc.get(idx);
        if (!c) return;
        fn({ idx, gens_[idx] }, a, *b, *c);
    });
}

} // namespace ecs
