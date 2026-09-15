#include "sim/fill.hpp"

#include <vector>

namespace aether::sim {

std::vector<double> defaultDensity(const rule::RuleIR& ir) {
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

void fillRandom(core::HostGrid& grid, std::span<const double> density, Pcg32& streamA) {
    // Cumulative thresholds so one draw decides the state.
    std::vector<double> cumulative(density.size());
    double acc = 0.0;
    for (size_t i = 0; i < density.size(); ++i) {
        acc += density[i] < 0.0 ? 0.0 : density[i];
        cumulative[i] = acc;
    }
    for (uint8_t& cell : grid.current()) {
        const double u = streamA.unit();
        uint8_t state = 0;
        for (size_t i = 0; i < cumulative.size(); ++i) {
            if (u < cumulative[i]) { state = static_cast<uint8_t>(i + 1); break; }
        }
        cell = state;
    }
}

}  // namespace aether::sim
