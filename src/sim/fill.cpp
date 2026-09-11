#include "sim/fill.hpp"

#include <vector>

namespace aether::sim {

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
