#include "DustECS.hpp"

namespace ecs {

Entity Registry::create() {
    if (!free_.empty()) {
        uint32_t idx = free_.back();
        free_.pop_back();
        return { idx, gens_[idx] };
    }
    uint32_t idx = (uint32_t)gens_.size();
    gens_.push_back(1);
    return { idx, 1 };
}

void Registry::destroy(Entity e) {
    if (!alive(e)) return;
    for (auto& [_, pool] : pools_)
        pool->remove(e.index);
    ++gens_[e.index];
    free_.push_back(e.index);
}

bool Registry::alive(Entity e) const {
    return e.index < gens_.size() && gens_[e.index] == e.gen;
}

void Registry::clear() {
    pools_.clear();
    gens_.clear();
    free_.clear();
}

size_t Registry::size() const {
    return gens_.size() - free_.size();
}

} // namespace ecs
