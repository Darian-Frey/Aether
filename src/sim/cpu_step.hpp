// CPU reference stepper (D-011, F-002).
//
// Serial, unoptimised, and meant to be obviously correct: this is the oracle
// the GPU path is measured against. It executes a LutRule over a HostGrid.
// Cell mutation (SPEC §9.2) arrives in Phase 2 as a parameter here and in
// the shader together.

#pragma once

#include "core/grid.hpp"
#include "rule/lut.hpp"

#include <span>

namespace aether::sim {

// One generation: reads `current`, writes `next`. The two must be distinct
// buffers of spec.bytesPerBuffer() bytes; passing the same span twice is the
// AV-004 defect and is rejected. Does not swap.
void cpuStep(const rule::LutRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next);

// One generation on a HostGrid, then swap, so the result is grid.current().
void cpuStep(const rule::LutRule& rule, core::HostGrid& grid);

}  // namespace aether::sim
