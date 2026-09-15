// Random grid seeding (F-013).

#pragma once

#include "core/grid.hpp"
#include "rule/ir.hpp"
#include "sim/rng.hpp"

#include <span>
#include <vector>

namespace aether::sim {

// What a fresh grid is seeded with when nothing says otherwise: an even
// spread over the states the rule actually lives in, leaving the ageing
// tail of SPEC §7 empty, since a half-faded cell is not a sensible thing to
// start a run with. A two-state rule gets the 30% soup Life-like rules are
// conventionally seeded at. The weights always sum to at most one, which is
// what BUG-008 was about: `fillRandom` walks them cumulatively, so anything
// past one silently starves the last states.
std::vector<double> defaultDensity(const rule::RuleIR& ir);

// Fills the current buffer. `density[s]` is the probability of state s+1
// for s in 0..S-2; the remainder is state 0. Draws exactly one value per
// cell from stream A regardless of outcome, so the number of draws is a
// function of the grid size alone and later draws stay reproducible.
void fillRandom(core::HostGrid& grid, std::span<const double> density, Pcg32& streamA);

}  // namespace aether::sim
