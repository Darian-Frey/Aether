// Population and field readouts (F-036, AV-002, AV-018).
//
// What the window plots and what the conservation books are read from. Three
// things make this harder than "add up the grid", and all three are about where
// the addition happens.
//
// **It must not read the grid back.** A statistics panel fed by a per-generation
// `glGetTexImage` is AV-002 in the one place nobody would look for it: the
// simulation still works, just slowly, and the cause is a feature nobody
// suspects. So the sum is computed on the GPU and what comes back is a fixed
// handful of numbers — a count per state and a total per field — regardless of
// how large the grid is.
//
// **It must be deterministic.** Float addition is not associative, so a sum
// whose order depends on how the driver scheduled its workgroups gives a
// different answer on two runs of the same session. Atomics are therefore out,
// which is what AV-018 records: the conservation figure is useless as detection
// if it wobbles on its own. The order is fixed here instead, by structure.
//
// **Both paths must agree.** Which means the CPU oracle cannot simply walk the
// grid: it has to add up in the same order the shader does, or the two readouts
// disagree in the last places and there is no saying which is right.
//
// So the order is defined once, here, and both implementations follow it:
//
//   The grid is divided into **tiles** of `kTileCells` cells in row-major cell
//   order. Each tile is summed in cell order; the tile totals are then summed in
//   tile order. Nothing else is allowed to reorder, and in particular nothing
//   accumulates into a shared variable from more than one invocation.
//
// This is an *observation*. It is not in the session, it does not feed the step,
// and nothing about a run changes if it is never taken. What is in the session
// is how often it is taken (SPEC §11), because that is a parameter.

#pragma once

#include "core/grid.hpp"
#include "rule/compile.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace aether::sim {

// Cells per tile. One workgroup's worth on the GPU side, and the unit the CPU
// side brackets its sums into so the two agree. A power of two so the tile
// count is a shift away, and 256 because that is the workgroup size the step
// shaders already use on this hardware.
inline constexpr uint32_t kTileCells = 256;

// How many buckets a genome is reduced to for the population plot. A genome is
// 32 bits and cannot be histogrammed directly; it is bucketed by the *same*
// hash the palette colours it with, so a band in the graph is the colour of the
// cells it counts rather than an unrelated grouping (F-033).
inline constexpr uint32_t kGenomeBuckets = 16;

struct GridStats {
    // One entry per state, including state 0, so the totals add to the cell
    // count and a plot can show what is dead as well as what is alive. Exact:
    // these are integers and integer addition does not care about order.
    std::vector<uint64_t> stateCounts;

    // One per declared field, in the rule's order. Doubles here because the
    // host has them to spare and the summation order — not the width — is what
    // makes the two paths agree; the GPU sums in f32 and the CPU mirrors that
    // exactly, so this holds an f32-faithful value in a wider box.
    std::vector<double> fieldTotals;

    // Live cells per genome bucket, empty for a rule without a genome.
    std::vector<uint64_t> genomeBuckets;

    uint64_t generation = 0;

    uint64_t live() const {
        uint64_t n = 0;
        for (size_t s = 1; s < stateCounts.size(); ++s) n += stateCounts[s];
        return n;
    }
};

// The bucket a genome falls in. The palette's hash, taken over the live bits,
// so the plot and the grid group cells the same way. Presentational like the
// colour it mirrors, and deliberately not `sim::mix32` by a different name —
// see `render/palette`'s note on the relaxed twin.
uint32_t genomeBucket(uint32_t genome, uint32_t bits);

// The reduction, on the host. `fields` is one span per declared field in the
// rule's order, each holding the field's raw bytes for the whole grid.
//
// This is the oracle for the GPU reduction in the same sense `cpuStep` is the
// oracle for the step: the shader is written to match it, and a test compares
// them. Summing in tile order rather than cell order costs nothing here and is
// the whole reason the comparison can be an equality.
GridStats reduce(const rule::CompiledRule& rule, const core::GridSpec& spec,
                 std::span<const uint8_t> cells,
                 std::span<const std::span<const uint8_t>> fields = {});

}  // namespace aether::sim
