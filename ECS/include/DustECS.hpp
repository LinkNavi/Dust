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
    template<typename Func> void each(Func&& fn);
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

    template<typename T, typename Func>
    void view(Func&& fn);

    template<typename A, typename B, typename Func>
    void view(Func&& fn);

    template<typename A, typename B, typename C, typename Func>
    void view(Func&& fn);

private:
    std::vector<uint32_t>                        gens_;
    std::vector<uint32_t>                        free_;
    std::vector<std::unique_ptr<SparseSetBase>>  pools_;

    template<typename T>
    SparseSet<T>& pool();
};

// Per-type IDs assigned on first use, process-wide — lets Registry index
// pools_ by a plain integer instead of hashing std::type_index through a
// map on every add/get/has/remove/view call.
namespace detail {
    inline size_t nextTypeId() {
        static size_t counter = 0;
        return counter++;
    }
    template<typename T>
    inline size_t typeId() {
        static size_t id = nextTypeId();
        return id;
    }
}

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
template<typename Func>
void SparseSet<T>::each(Func&& fn) {
    for (size_t i = 0; i < dense.size(); ++i)
        fn(dense[i], components[i]);
}

// ─── REGISTRY TEMPLATE IMPL (must stay in header) ─────────────────────────

template<typename T>
SparseSet<T>& Registry::pool() {
    size_t id = detail::typeId<T>();
    if (id >= pools_.size()) pools_.resize(id + 1);
    if (!pools_[id]) pools_[id] = std::make_unique<SparseSet<T>>();
    return *static_cast<SparseSet<T>*>(pools_[id].get());
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
    size_t id = detail::typeId<T>();
    if (id >= pools_.size() || !pools_[id]) return false;
    return pools_[id]->has(e.index);
}

template<typename T>
void Registry::remove(Entity e) {
    if (!alive(e)) return;
    pool<T>().remove(e.index);
}

template<typename T, typename Func>
void Registry::view(Func&& fn) {
    pool<T>().each([&](uint32_t idx, T& c) {
        fn(Entity{ idx, gens_[idx] }, c);
    });
}

template<typename A, typename B, typename Func>
void Registry::view(Func&& fn) {
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    pa.each([&](uint32_t idx, A& a) {
        B* b = pb.get(idx);
        if (!b) return;
        fn(Entity{ idx, gens_[idx] }, a, *b);
    });
}

template<typename A, typename B, typename C, typename Func>
void Registry::view(Func&& fn) {
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    auto& pc = pool<C>();
    pa.each([&](uint32_t idx, A& a) {
        B* b = pb.get(idx);
        if (!b) return;
        C* c = pc.get(idx);
        if (!c) return;
        fn(Entity{ idx, gens_[idx] }, a, *b, *c);
    });
}

} // namespace ecs
