#pragma once

// On-disk format for a .pack file, shared by the DustPacker writer and the
// AssetManager reader so they can never drift apart. Header-only — no
// crypto/zstd calls here, just layout + (de)serialization of the TOC.
//
// File layout:
//   [FileHeader]                 fixed size, unencrypted
//   [TOC ciphertext]             header.tocCipherLen bytes
//   [asset 0 ciphertext][asset 1 ciphertext]...
//
// Each asset's ciphertext is zstd-compressed, then AEAD-encrypted
// (XChaCha20-Poly1305, IETF) with its own random nonce — compress-then-
// encrypt, since encrypted bytes don't compress. The TOC itself is
// encrypted the same way with its own nonce, so nothing (not even asset
// names) is readable without the key.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Dust::Pack {

inline constexpr char     kMagic[4]   = { 'D', 'P', 'K', '1' };
inline constexpr uint32_t kVersion    = 1;
inline constexpr size_t   kNonceBytes = 24; // crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
inline constexpr size_t   kKeyBytes   = 32; // crypto_aead_xchacha20poly1305_ietf_KEYBYTES
inline constexpr size_t   kTagBytes   = 16; // crypto_aead_xchacha20poly1305_ietf_ABYTES

#pragma pack(push, 1)
struct FileHeader {
    char     magic[4];
    uint32_t version;
    uint8_t  tocNonce[kNonceBytes];
    uint64_t tocCipherLen; // includes kTagBytes
    uint32_t assetCount;
};
#pragma pack(pop)

// In-memory TOC entry. On disk the name is length-prefixed (variable size),
// so entries are serialized as a flat byte stream rather than a raw struct
// array — see serializeToc/deserializeToc below.
struct TocEntry {
    std::string name;
    uint64_t    offset           = 0; // absolute byte offset of this asset's ciphertext
    uint64_t    compressedSize   = 0; // ciphertext length, includes kTagBytes
    uint64_t    uncompressedSize = 0; // original asset size
    uint8_t     nonce[kNonceBytes] = {};
};

// FNV-1a 64-bit — fast, good enough for a name -> index table; collisions
// aren't security-relevant here since the key is what gates access.
inline uint64_t hashName(const std::string& name) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : name) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

inline std::vector<uint8_t> serializeToc(const std::vector<TocEntry>& entries) {
    std::vector<uint8_t> out;
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        out.insert(out.end(), b, b + n);
    };
    for (auto& e : entries) {
        uint32_t nameLen = (uint32_t)e.name.size();
        put(&nameLen, 4);
        put(e.name.data(), nameLen);
        put(&e.offset, 8);
        put(&e.compressedSize, 8);
        put(&e.uncompressedSize, 8);
        put(e.nonce, kNonceBytes);
    }
    return out;
}

inline bool deserializeToc(const uint8_t* data, size_t size, uint32_t count,
                           std::vector<TocEntry>& out) {
    size_t off = 0;
    auto get = [&](void* p, size_t n) -> bool {
        if (off + n > size) return false;
        memcpy(p, data + off, n);
        off += n;
        return true;
    };

    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nameLen;
        if (!get(&nameLen, 4) || off + nameLen > size) return false;

        TocEntry e;
        e.name.assign((const char*)data + off, nameLen);
        off += nameLen;

        if (!get(&e.offset, 8))           return false;
        if (!get(&e.compressedSize, 8))   return false;
        if (!get(&e.uncompressedSize, 8)) return false;
        if (!get(e.nonce, kNonceBytes))   return false;

        out.push_back(std::move(e));
    }
    return true;
}

} // namespace Dust::Pack
