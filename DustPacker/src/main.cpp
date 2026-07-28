// DustPacker — offline tool that packs a folder of assets into a single
// encrypted, compressed .pack file (format shared with AssetManager via
// PackFormat.hpp). Not linked into the game; this never ships.
//
// Usage: DustPacker <assetFolder> <output.pack> <passphrase>

#include "AssetManager/PackFormat.hpp"
#include <sodium.h>
#include <zstd.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace Dust::Pack;

namespace {

std::vector<uint8_t> readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf((size_t)size);
    if (size > 0 && !f.read((char*)buf.data(), size)) return {};
    return buf;
}

// Compress `raw` with zstd, then AEAD-encrypt it with a fresh random nonce.
// Fills in everything on `entry` except name/offset.
bool packOne(const std::vector<uint8_t>& raw, const uint8_t key[kKeyBytes],
             TocEntry& entry, std::vector<uint8_t>& outCipher) {
    size_t bound = ZSTD_compressBound(raw.size());
    std::vector<uint8_t> compressed(bound);
    size_t compSize = ZSTD_compress(compressed.data(), bound,
                                    raw.data(), raw.size(), 19 /* offline, favor ratio */);
    if (ZSTD_isError(compSize)) {
        fprintf(stderr, "dustpacker: zstd compress failed: %s\n", ZSTD_getErrorName(compSize));
        return false;
    }
    compressed.resize(compSize);

    randombytes_buf(entry.nonce, kNonceBytes);
    outCipher.resize(compSize + kTagBytes);
    unsigned long long cipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        outCipher.data(), &cipherLen,
        compressed.data(), compressed.size(),
        nullptr, 0, nullptr, entry.nonce, key);
    outCipher.resize((size_t)cipherLen);

    entry.compressedSize   = outCipher.size();
    entry.uncompressedSize = raw.size();
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: DustPacker <assetFolder> <output.pack> <passphrase>\n");
        return 1;
    }
    fs::path inputDir  = argv[1];
    fs::path outputPath = argv[2];
    std::string passphrase = argv[3];

    if (sodium_init() < 0) {
        fprintf(stderr, "dustpacker: libsodium init failed\n");
        return 1;
    }
    if (!fs::is_directory(inputDir)) {
        fprintf(stderr, "dustpacker: '%s' is not a directory\n", inputDir.string().c_str());
        return 1;
    }

    uint8_t key[kKeyBytes];
    crypto_generichash(key, kKeyBytes,
                        (const unsigned char*)passphrase.data(), passphrase.size(),
                        nullptr, 0);

    std::vector<TocEntry>            entries;
    std::vector<std::vector<uint8_t>> blobs;

    for (auto& p : fs::recursive_directory_iterator(inputDir)) {
        if (!p.is_regular_file()) continue;

        fs::path rel = fs::relative(p.path(), inputDir);
        std::string name = rel.generic_string(); // forward slashes, portable

        std::vector<uint8_t> raw = readFile(p.path());

        TocEntry entry;
        entry.name = name;
        std::vector<uint8_t> cipher;
        if (!packOne(raw, key, entry, cipher)) {
            fprintf(stderr, "dustpacker: failed to pack '%s'\n", name.c_str());
            return 1;
        }

        printf("dustpacker: %-40s %8zu -> %8zu bytes\n", name.c_str(), raw.size(), cipher.size());
        entries.push_back(std::move(entry));
        blobs.push_back(std::move(cipher));
    }

    if (entries.empty()) {
        fprintf(stderr, "dustpacker: no files found under '%s'\n", inputDir.string().c_str());
        return 1;
    }

    // Offsets are placeholders (0) for this pass — the serialized TOC size
    // doesn't depend on their value (fixed-width u64 fields), only on the
    // count and name lengths, so this is safe to do before offsets exist.
    std::vector<uint8_t> tocPlainSized = serializeToc(entries);

    FileHeader header{};
    memcpy(header.magic, kMagic, 4);
    header.version      = kVersion;
    header.assetCount   = (uint32_t)entries.size();
    header.tocCipherLen = tocPlainSized.size() + kTagBytes;

    uint64_t offset = sizeof(FileHeader) + header.tocCipherLen;
    for (size_t i = 0; i < entries.size(); i++) {
        entries[i].offset = offset;
        offset += blobs[i].size();
    }

    // Re-serialize now that real offsets are filled in, then encrypt.
    std::vector<uint8_t> tocPlain = serializeToc(entries);
    randombytes_buf(header.tocNonce, kNonceBytes);
    std::vector<uint8_t> tocCipher(tocPlain.size() + kTagBytes);
    unsigned long long tocCipherLen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        tocCipher.data(), &tocCipherLen,
        tocPlain.data(), tocPlain.size(),
        nullptr, 0, nullptr, header.tocNonce, key);
    tocCipher.resize((size_t)tocCipherLen);
    header.tocCipherLen = tocCipher.size();

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) {
        fprintf(stderr, "dustpacker: failed to open '%s' for writing\n", outputPath.string().c_str());
        return 1;
    }
    out.write((const char*)&header, sizeof(header));
    out.write((const char*)tocCipher.data(), (std::streamsize)tocCipher.size());
    for (auto& blob : blobs)
        out.write((const char*)blob.data(), (std::streamsize)blob.size());

    printf("dustpacker: wrote '%s' — %zu assets\n", outputPath.string().c_str(), entries.size());
    return 0;
}
