// Rule mutation (F-015, SPEC §9.1): point edits on the IR, drawn from
// stream A, validated, redrawn on failure up to a fixed cap.
//
// Produces an IR and nothing else. Recompilation and the lineage append
// happen in Simulation::setRule, which is the only way a rule reaches the
// engine, so a mutation cannot bypass the log.

#pragma once

#include "rule/ir.hpp"
#include "sim/rng.hpp"

#include <cstdint>
#include <optional>

namespace aether::sim {

constexpr int kMutationAttempts = 8;

struct RuleMutationParams {
    bool     enabled   = false;
    uint32_t interval  = 250;   // generations between events, >= 1
    uint32_t magnitude = 1;     // point edits per event, >= 1
};

struct MutationResult {
    std::optional<rule::RuleIR> ir;      // absent if every attempt was invalid
    int                         attempts = 0;
};

// Applies `magnitude` point edits to a copy of `ir`. Each attempt draws
// fresh edits from stream A; the number of draws per attempt depends only
// on the IR's form, so a replay with the same seed reproduces the sequence.
MutationResult mutateRule(const rule::RuleIR& ir, uint32_t magnitude, Pcg32& streamA);

}  // namespace aether::sim
