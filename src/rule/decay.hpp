// Ageing tail (SPEC §7 `decay`, F-025, D-014).
//
// A front-end transform: IR in, IR out. `decay N` appends N states to a
// rule and routes every death through them, one state per generation, so a
// cell that stops being supported fades instead of vanishing. The result is
// an ordinary outer-totalistic IR, which is why nothing downstream — the
// backends, both steppers, mutation, lineage, sessions — needs to know the
// feature exists.

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace aether::rule {

// Largest tail that still fits LUT_MAX_ENTRIES for this rule's shape.
uint16_t maxDecay(const RuleIR& base);

// `extra` = 0 returns the rule unchanged. Fails, with a message naming the
// largest tail that fits, when the result would exceed 256 states or the
// table threshold.
std::variant<RuleIR, std::string> applyDecay(const RuleIR& base, uint16_t extra);

}  // namespace aether::rule
