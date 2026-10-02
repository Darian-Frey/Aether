// Cell storage types (SPEC §1).
//
// Header-only so that rule/ can name the type without linking core/.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace aether::core {

// U32 is for auxiliary fields only, and the validator refuses it for the state
// (F-033, D-025). A genome needs more than eight bits and cannot be masked as a
// float; the state's type reaches patterns, palettes and the space-time view,
// none of which a genome touches, so it is kept out of there rather than being
// made to mean something everywhere.
enum class CellType : uint8_t { U8, F32, U32 };

constexpr uint32_t cellBytes(CellType t) {
    return t == CellType::U8 ? 1u : 4u;
}

constexpr std::string_view toString(CellType v) {
    switch (v) {
        case CellType::U8:  return "u8";
        case CellType::F32: return "f32";
        case CellType::U32: return "u32";
    }
    return "?";
}

constexpr std::optional<CellType> parseCellType(std::string_view s) {
    if (s == "u8")  return CellType::U8;
    if (s == "f32") return CellType::F32;
    if (s == "u32") return CellType::U32;
    return std::nullopt;
}

}  // namespace aether::core
