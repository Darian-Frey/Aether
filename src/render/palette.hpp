// State palette (SPEC §13): 256 RGBA entries indexed by state.

#pragma once

#include <array>
#include <cstdint>

namespace aether::render {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba&) const = default;
};

struct Palette {
    std::array<Rgba, 256> entries{};

    // State 0 near-black, state 1 near-white, further states around the
    // hue circle so that generations rules read as a trail.
    static Palette defaultFor(uint16_t states);
};

}  // namespace aether::render
