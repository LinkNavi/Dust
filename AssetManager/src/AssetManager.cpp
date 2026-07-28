#include "AssetManager/AssetManager.hpp"
#include <sodium.h>
#include <zstd.h>
#include <cstdio>
#include <cstring>

namespace Dust {

using namespace Pack;

AssetManager::~AssetManager() { close(); }

bool AssetManager::open(const std::string& filePath, const std::string& passphrase) {
    close();

    if (sodium_init() < 0) {
        fprintf(stderr, "assetmanager: libsodium init failed\n");
        return false;
    }

    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "assetmanager: failed to open '%s'\n", filePath.c_str());
        return false;
    }

    FileHeader header{};
    if (fread(&header, sizeof(header), 1, f) != 1 || memcmp(header.magic, kMagic, 4) != 0) {
        fprintf(stderr, "assetmanager: '%s' is not a valid .pack file\n", filePath.c_str());
        fclose(f);
        return false;
    }
    if (header.version != kVersion) {
        fprintf(stderr, "assetmanager: '%s' is pack version %u, expected %u\n",
                filePath.c_str(), header.version, kVersion);
        fclose(f);
        return false;
    }

    std::vector<uint8_t> tocCipher(header.tocCipherLen);
    bool readOk = fread(tocCipher.data(), 1, tocCipher.size(), f) == tocCipher.size();
    fclose(f);
    if (!readOk) {
        fprintf(stderr, "assetmanager: truncated TOC in '%s'\n", filePath.c_str());
        return false;
    }

    uint8_t derivedKey[kKeyBytes];
    crypto_generichash(derivedKey, kKeyBytes,
                        (const unsigned char*)passphrase.data(), passphrase.size(),
                        nullptr, 0);

    if (tocCipher.size() < kTagBytes) {
        fprintf(stderr, "assetmanager: malformed TOC in '%s'\n", filePath.c_str());
        return false;
    }
    std::vector<uint8_t> tocPlain(tocCipher.size() - kTagBytes);
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            tocPlain.data(), &plainLen, nullptr,
            tocCipher.data(), tocCipher.size(),
            nullptr, 0, header.tocNonce, derivedKey) != 0) {
        fprintf(stderr, "assetmanager: wrong key or corrupt '%s'\n", filePath.c_str());
        return false;
    }

    std::vector<TocEntry> newToc;
    if (!deserializeToc(tocPlain.data(), (size_t)plainLen, header.assetCount, newToc)) {
        fprintf(stderr, "assetmanager: malformed TOC in '%s'\n", filePath.c_str());
        return false;
    }

    toc = std::move(newToc);
    slots.assign(toc.size(), Slot{});
    nameToIndex.clear();
    for (uint32_t i = 0; i < toc.size(); i++)
        nameToIndex[hashName(toc[i].name)] = i;

    memcpy(key, derivedKey, kKeyBytes);
    path     = filePath;
    openFlag = true;
    return true;
}

void AssetManager::close() {
    toc.clear();
    slots.clear();
    nameToIndex.clear();
    path.clear();
    sodium_memzero(key, sizeof(key));
    openFlag = false;
}

bool AssetManager::has(const std::string& name) const {
    return nameToIndex.find(hashName(name)) != nameToIndex.end();
}

AssetHandle AssetManager::load(const std::string& name) {
    if (!openFlag) return {};

    auto it = nameToIndex.find(hashName(name));
    if (it == nameToIndex.end()) {
        fprintf(stderr, "assetmanager: no such asset '%s'\n", name.c_str());
        return {};
    }

    uint32_t idx = it->second;
    Slot& slot = slots[idx];

    if (slot.loaded) {
        slot.refCount++;
        return { idx };
    }

    const TocEntry& e = toc[idx];

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    if (fseek(f, (long)e.offset, SEEK_SET) != 0) {
        fclose(f);
        return {};
    }

    std::vector<uint8_t> cipher(e.compressedSize);
    bool readOk = fread(cipher.data(), 1, cipher.size(), f) == cipher.size();
    fclose(f);
    if (!readOk || cipher.size() < kTagBytes) return {};

    std::vector<uint8_t> compressed(cipher.size() - kTagBytes);
    unsigned long long plainLen = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            compressed.data(), &plainLen, nullptr,
            cipher.data(), cipher.size(),
            nullptr, 0, e.nonce, key) != 0) {
        fprintf(stderr, "assetmanager: decrypt failed for '%s' (wrong key or corrupt file)\n",
                e.name.c_str());
        return {};
    }

    slot.bytes.resize(e.uncompressedSize);
    size_t decompressed = ZSTD_decompress(slot.bytes.data(), slot.bytes.size(),
                                          compressed.data(), (size_t)plainLen);
    if (ZSTD_isError(decompressed) || decompressed != e.uncompressedSize) {
        fprintf(stderr, "assetmanager: decompress failed for '%s'\n", e.name.c_str());
        slot.bytes.clear();
        return {};
    }

    slot.loaded   = true;
    slot.refCount = 1;
    return { idx };
}

void AssetManager::release(AssetHandle h) {
    if (!valid(h) || h.index >= slots.size()) return;
    Slot& slot = slots[h.index];
    if (!slot.loaded || slot.refCount == 0) return;

    if (--slot.refCount == 0) {
        slot.bytes.clear();
        slot.bytes.shrink_to_fit();
        slot.loaded = false;
    }
}

const std::vector<uint8_t>* AssetManager::data(AssetHandle h) const {
    if (!valid(h) || h.index >= slots.size()) return nullptr;
    const Slot& slot = slots[h.index];
    return slot.loaded ? &slot.bytes : nullptr;
}

} // namespace Dust
