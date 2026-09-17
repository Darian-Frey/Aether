// Resolving a Kernel's profile onto the neighbourhood (F-006, Phase 5).
//
// A kernel is authored as a shape and a list of numbers. The step needs one
// weight per neighbour offset, plus one for the cell itself, which the
// neighbourhood never includes (SPEC §3). Fixing that mapping in one place is
// what makes a continuous rule mean the same thing on both execution paths:
// the weights are resolved once at compile time and both steppers are handed
// the same numbers rather than each deriving them (AV-005).
//
// Weights are normalised to sum to 1, so the convolution of cells in [0, 1]
// lands in [0, 1] and a growth function's `mu` means the same thing whatever
// kernel it is paired with.

#pragma once

#include "rule/ir.hpp"

#include <string>
#include <variant>
#include <vector>

namespace aether::rule {

struct ResolvedKernel {
    std::vector<float> weights;      // one per offset, canonical order
    float              self = 0.0f;  // the centre, which is not an offset
};

// The profile as the step will read it, or why it cannot be read that way.
std::variant<ResolvedKernel, std::string> resolveKernel(const RuleIR& ir);

}  // namespace aether::rule
