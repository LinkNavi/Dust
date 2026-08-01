#pragma once

#include <cstdint>

// Lives directly in Dust:: (not Dust::UI::) — shared vocabulary, same as
// Camera/Mesh/Model, not something specific to the widget tree.
namespace Dust {

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    static Color fromHex(uint32_t hex) {
        // 0xRRGGBBAA
        return {
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8)  & 0xFF) / 255.0f,
            ( hex        & 0xFF) / 255.0f,
        };
    }
};

namespace Colors {
    inline constexpr Color White       { 1.0f,  1.0f,  1.0f,  1.0f };
    inline constexpr Color Black       { 0.0f,  0.0f,  0.0f,  1.0f };
    inline constexpr Color Red         { 0.85f, 0.15f, 0.15f, 1.0f };
    inline constexpr Color DarkRed     { 0.4f,  0.08f, 0.08f, 1.0f };
    inline constexpr Color Green       { 0.2f,  0.75f, 0.25f, 1.0f };
    inline constexpr Color DarkGreen   { 0.08f, 0.35f, 0.1f,  1.0f };
    inline constexpr Color Blue        { 0.2f,  0.4f,  0.85f, 1.0f };
    inline constexpr Color DarkBlue    { 0.08f, 0.15f, 0.4f,  1.0f };
    inline constexpr Color Yellow      { 0.95f, 0.85f, 0.2f,  1.0f };
    inline constexpr Color Gold        { 0.85f, 0.65f, 0.13f, 1.0f };
    inline constexpr Color DarkGold    { 0.4f,  0.3f,  0.06f, 1.0f };
    inline constexpr Color Gray        { 0.5f,  0.5f,  0.5f,  1.0f };
    inline constexpr Color DarkGray    { 0.15f, 0.15f, 0.15f, 1.0f };
    inline constexpr Color LightGray   { 0.75f, 0.75f, 0.75f, 1.0f };
    inline constexpr Color Purple      { 0.55f, 0.25f, 0.75f, 1.0f };
    inline constexpr Color Cyan        { 0.2f,  0.8f,  0.8f,  1.0f };
    inline constexpr Color Transparent { 0.0f,  0.0f,  0.0f,  0.0f };
    inline constexpr Color None        { 0.0f,  0.0f,  0.0f,  0.0f }; // alias — used for "no border" in DustUI-API.md's examples
}

} // namespace Dust
