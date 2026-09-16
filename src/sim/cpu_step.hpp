// CPU reference stepper (D-011, F-002).
//
// Serial, unoptimised, and meant to be obviously correct: this is the oracle
// the GPU path is measured against. It executes a CompiledRule over a HostGrid.
// Cell mutation (SPEC §9.2) arrives in Phase 2 as a parameter here and in
// the shader together.

#pragma once

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "sim/hash.hpp"

#include <span>
#include <vector>

namespace aether::sim {

// One node's value while an expression is walked. Exposed only because the
// arena lives in StepScratch; nothing outside cpu_step interprets it.
struct ExprValue {
    int32_t i = 0;
    float   f = 0.0f;
    bool    b = false;
};

// Working room for a cell's transition, sized from the rule. Make one and
// reuse it for every cell: the step loop allocates nothing (ARCHITECTURE
// §Key invariants 8). After a stepCell call, `neighbours` and `counts` hold
// what that cell gathered and counted, which is the part worth reading back.
struct StepScratch {
    explicit StepScratch(const rule::CompiledRule& rule);

    std::vector<uint8_t>   neighbours;    // N entries, SPEC §3 canonical order
    std::vector<uint32_t>  counts;        // states 1..S-1, outer-totalistic
    std::vector<uint32_t>  stateCounts;   // states 0..S-1, expression rules
    std::vector<ExprValue> expr;          // the expression arena
};

// What one cell's transition produced. Plain data, returned by value.
struct CellTransition {
    uint8_t  own        = 0;       // the state that was read
    uint8_t  fromRule   = 0;       // what the rule alone gives
    uint8_t  next       = 0;       // what is written: fromRule, unless mutated
    bool     mutated    = false;   // cell mutation overrode the rule (SPEC §9.2)
    bool     hasIndex   = false;   // table kinds only
    uint64_t tableIndex = 0;       // the entry that fired
    uint32_t scalar     = 0;       // the one number the kind reduced the
                                   // neighbourhood to, where it has one: the
                                   // count for counted_totalistic, the sum for
                                   // totalistic. Zero for the other kinds.
};

// One cell, gathered and boundary-resolved exactly as the step does it.
// cpuStep is a loop over this, so anything else that wants to know what a
// cell did — an inspector, a diagnostic for a failing equivalence case — asks
// the engine rather than forming a second opinion (IMP-005, AV-017).
// Reads `current` and writes nothing; `scratch` is filled as a side effect.
CellTransition stepCell(const rule::CompiledRule& rule, const core::GridSpec& spec,
                        std::span<const uint8_t> current,
                        uint32_t x, uint32_t y, uint32_t z,
                        uint64_t generation, CellMutation mutation,
                        StepScratch& scratch);

// One generation: reads `current`, writes `next`. The two must be distinct
// buffers of spec.bytesPerBuffer() bytes; passing the same span twice is the
// AV-004 defect and is rejected. Does not swap. `generation` is the index of
// the generation being read; cell mutation hashes it (SPEC §9.2).
void cpuStep(const rule::CompiledRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next,
             uint64_t generation = 0, CellMutation mutation = {});

// One generation on a HostGrid, then swap, so the result is grid.current().
void cpuStep(const rule::CompiledRule& rule, core::HostGrid& grid,
             uint64_t generation = 0, CellMutation mutation = {});

}  // namespace aether::sim
