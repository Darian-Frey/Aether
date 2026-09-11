// Cell storage types (SPEC §1).
//
// Header-only so that rule/ can name the type without linking core/.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace aether::core {

enum class CellType : uint8_t { U8, F32 };

constexpr uint32_t cellBytes(CellType t) {
    return t == CellType::F32 ? 4u : 1u;
}

constexpr std::string_view toString(CellType v) {
    switch (v) {
        case CellType::U8:  return "u8";
        case CellType::F32: return "f32";
    }
    return "?";
}

constexpr std::optional<CellType> parseCellType(std::string_view s) {
    if (s == "u8")  return CellType::U8;
    if (s == "f32") return CellType::F32;
    return std::nullopt;
}

}  // namespace aether::core
