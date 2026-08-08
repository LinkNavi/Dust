#pragma once

// Offline .ttf/.otf -> DustFont (MSDF atlas) conversion — see
// AssetManager/FontFormat.hpp for the binary layout this produces.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace DustPacker {

// Converts a font file into serialized DustFont binary bytes (ready to hand
// to the pack/compress/encrypt pipeline). Returns empty on failure — caller
// logs and skips packing that asset.
std::vector<uint8_t> convertFontToDustBinary(const std::filesystem::path& path);

} // namespace DustPacker
