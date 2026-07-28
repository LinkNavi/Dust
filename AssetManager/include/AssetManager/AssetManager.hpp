#pragma once

#include "AssetManager/PackFormat.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Dust {

// Static per-file identity — indices don't get reused, so unlike Mesh's
// VertHandle this doesn't need a generation counter, just a sentinel.
struct AssetHandle {
    uint32_t index = UINT32_MAX;
};
inline bool valid(AssetHandle h) { return h.index != UINT32_MAX; }

// Runtime reader for .pack files written by DustPacker. Opens a vault with
// a key, then loads assets lazily and ref-counted — nothing is decrypted or
// decompressed until load() is actually called for it, and bytes are freed
// again once the last reference is released.
//
// This is v1: refcount-to-zero eviction, no LRU/budget yet. Fine for now;
// revisit once there's an actual memory budget to hit (see DeviceTier).
class AssetManager {
public:
    ~AssetManager();

    // Opens `path`, deriving the decrypt key from `passphrase` (must match
    // what DustPacker packed it with). Reads + decrypts the TOC only —
    // no asset payloads are touched here.
    bool open(const std::string& path, const std::string& passphrase);
    void close();

    // Decrypts + decompresses on first call for a given name; each further
    // call just bumps a refcount and returns the same handle's data.
    // Returns an invalid handle (see valid()) on failure.
    AssetHandle load(const std::string& name);

    // Drops a reference; frees the decoded bytes once nobody holds it.
    void release(AssetHandle h);

    // Decoded bytes for a handle, or nullptr if invalid/not currently loaded.
    const std::vector<uint8_t>* data(AssetHandle h) const;

    bool   has(const std::string& name) const;
    size_t assetCount() const { return toc.size(); }
    bool   isOpen() const { return openFlag; }

private:
    struct Slot {
        std::vector<uint8_t> bytes;
        uint32_t             refCount = 0;
        bool                 loaded   = false;
    };

    std::vector<Pack::TocEntry>            toc;
    std::vector<Slot>                      slots;      // parallel to toc
    std::unordered_map<uint64_t, uint32_t> nameToIndex; // hashName(name) -> toc index

    std::string path;
    uint8_t     key[Pack::kKeyBytes] = {};
    bool        openFlag = false;
};

} // namespace Dust
