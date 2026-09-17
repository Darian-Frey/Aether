// State palette (SPEC §13): 256 RGBA entries indexed by state. Alpha is the
// state's opacity in the 3D view; the 2D view ignores it.

#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace aether::render {

struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba&) const = default;
};

struct Palette {
    std::array<Rgba, 256> entries{};

    // State 0 near-black, state 1 near-white, further states around the
    // hue circle so that generations rules read as a trail.
    //
    // `decayFrom` names the first state of an ageing tail (SPEC §7 decay).
    // The tail is coloured as a ramp from state 1 toward the dark, with
    // alpha falling as it ages, so a cell visibly fades: through colour in
    // 2D, through colour and opacity in 3D.
    static Palette defaultFor(uint16_t states, std::optional<uint16_t> decayFrom = std::nullopt);

    // A continuous rule's cells hold a value rather than an index, so the
    // palette is read as a ramp and interpolated across (SPEC §1, D-020).
    // Monotone from dark to bright: the hue cycling of defaultFor reads as a
    // rainbow on a smooth field, where what matters is seeing density.
    static Palette continuousRamp();
};

}  // namespace aether::render
