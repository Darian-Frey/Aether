// Patchy scalar noise for seeding a resource field (F-032).
//
// The design note is explicit about why this exists rather than a uniform fill:
// "uniform resources produce uniform populations and nothing interesting
// happens". A world needs somewhere rich and somewhere poor before a strategy
// has anything to be better at.
//
// Value noise summed over a few octaves. A coarse lattice of random values drawn
// from stream A, interpolated smoothly, then finer lattices at half the
// amplitude each. Not Perlin — Perlin interpolates gradients and needs a
// permutation table; value noise needs neither and is patchy enough for a
// carrying capacity, which is a landscape rather than a texture.
//
// Pure, and no GL: it runs once at seed time on the host and the result is
// uploaded like any other buffer. That is also what keeps it reproducible —
// the two execution paths receive identical bytes rather than each computing
// them.

#pragma once

#include "core/grid.hpp"
#include "sim/rng.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace aether::sim {

// How patchy, and how much detail. Travels in the journal, so these are the
// numbers a replay needs and nothing about them may be inferred from elsewhere.
struct NoiseParams {
    // Lattice cells across the grid's widest extent. Low is a few big patches,
    // high is a fine mottle. Clamped to at least 1.
    uint32_t frequency = 4;
    // How many times to halve the scale and the amplitude. One octave is smooth
    // blobs; four is a landscape with detail in it. Clamped to 1..8, because the
    // amplitude of the ninth is below what an f32 distinguishes at this range.
    uint32_t octaves = 3;
    // The low and high ends of the output. A floor above zero is what stops a
    // patch of the world being permanently dead rather than merely poor.
    float    low  = 0.0f;
    float    high = 1.0f;

    bool operator==(const NoiseParams&) const = default;
};

// Fills `out` with one float per cell of `spec`, in the grid's own index order.
// Draws from stream A: the number of draws is a function of the parameters and
// the grid alone, never of the values drawn, so every later draw in the session
// stays where it was (the same discipline `fillRandom` keeps).
//
// `out` must be spec.cellCount() long. Works in 1, 2 and 3 dimensions.
void fillNoise(const core::GridSpec& spec, const NoiseParams& params,
               Pcg32& streamA, std::span<float> out);

}  // namespace aether::sim
