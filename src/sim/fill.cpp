#include "sim/fill.hpp"

#include <algorithm>
#include <vector>

namespace aether::sim {

std::vector<double> defaultDensity(const rule::RuleIR& ir) {
    // A continuous rule has no states to share out: the one weight is the
    // fraction of cells seeded at all (SPEC §1).
    if (ir.cell_type == core::CellType::F32) return {0.3};

    // Tail states take no share: the weights are indexed by state - 1, so
    // entries from decay_from onward stay zero.
    const uint16_t live = ir.metadata.decay_from.value_or(ir.states);
    std::vector<double> out(ir.states > 1 ? ir.states - 1u : 0u, 0.0);
    if (live < 2 || out.empty()) return out;
    if (live == 2) {
        out[0] = 0.3;
        return out;
    }
    const double share = 1.0 / live;   // state 0 keeps the same share
    for (uint16_t s = 1; s < live; ++s) out[s - 1u] = share;
    return out;
}

void fillRandomRegion(core::HostGrid& grid, uint32_t x, uint32_t y, uint32_t z,
                      uint32_t w, uint32_t h, uint32_t d,
                      std::span<const double> density, Pcg32& streamA) {
    const auto& spec = grid.spec();
    if (x >= spec.width || y >= spec.height || z >= spec.depth) return;
    w = std::min(w, spec.width - x);
    h = std::min(h, spec.height - y);
    d = std::min(d, spec.depth - z);

    if (spec.cell_type == core::CellType::F32) {
        // The first weight is the fraction of cells seeded; a seeded cell
        // takes a uniform value. Two draws a cell either way, so the stream
        // advances by the same amount whatever the density.
        const double p = density.empty() ? 0.0 : std::clamp(density[0], 0.0, 1.0);
        auto cells = grid.currentFloats();
        for (uint32_t cz = z; cz < z + d; ++cz) {
            for (uint32_t cy = y; cy < y + h; ++cy) {
                for (uint32_t cx = x; cx < x + w; ++cx) {
                    const double u = streamA.unit();
                    const double v = streamA.unit();
                    cells[grid.index(cx, cy, cz)] = u < p ? static_cast<float>(v) : 0.0f;
                }
            }
        }
        return;
    }

    // Cumulative thresholds so one draw decides the state.
    std::vector<double> cumulative(density.size());
    double acc = 0.0;
    for (size_t i = 0; i < density.size(); ++i) {
        acc += density[i] < 0.0 ? 0.0 : density[i];
        cumulative[i] = acc;
    }
    auto cells = grid.current();
    for (uint32_t cz = z; cz < z + d; ++cz) {
        for (uint32_t cy = y; cy < y + h; ++cy) {
            for (uint32_t cx = x; cx < x + w; ++cx) {
                const double u = streamA.unit();
                uint8_t state = 0;
                for (size_t i = 0; i < cumulative.size(); ++i) {
                    if (u < cumulative[i]) { state = static_cast<uint8_t>(i + 1); break; }
                }
                cells[grid.index(cx, cy, cz)] = state;
            }
        }
    }
}

void fillRandom(core::HostGrid& grid, std::span<const double> density, Pcg32& streamA) {
    // The whole grid is the box that covers it, and walking it x-fastest is
    // the order this always walked, so the stream is consumed identically.
    const auto& s = grid.spec();
    fillRandomRegion(grid, 0, 0, 0, s.width, s.height, s.depth, density, streamA);
}

}  // namespace aether::sim
