// Random grid seeding (F-013).

#pragma once

#include "core/grid.hpp"
#include "sim/rng.hpp"

#include <span>

namespace aether::sim {

// Fills the current buffer. `density[s]` is the probability of state s+1
// for s in 0..S-2; the remainder is state 0. Draws exactly one value per
// cell from stream A regardless of outcome, so the number of draws is a
// function of the grid size alone and later draws stay reproducible.
void fillRandom(core::HostGrid& grid, std::span<const double> density, Pcg32& streamA);

}  // namespace aether::sim
