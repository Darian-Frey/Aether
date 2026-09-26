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

// A continuous rule's growth function at one convolution value (Phase 5).
// The same arena walk the discrete path uses, so the two cannot drift; `types`
// comes from expressionTypes(growth, 0, 0, true), where the trailing flag is
// what makes Self the convolution result rather than an own state (BUG-010).
float evalGrowth(const rule::Expression& growth, std::span<const rule::ExprType> types,
                 float convolution, std::vector<ExprValue>& scratch);

// The auxiliary field buffers of one generation, in the rule's declaration
// order (F-031). Each span is bytes whatever the field's cell type, exactly as
// HostGrid's own buffers are, and holds spec.cellCount() values. An empty list
// is the grid this engine had before F-031, which is why every one of these is
// a defaulted argument: a caller that knows nothing about fields keeps its
// meaning.
using FieldReads  = std::span<const std::span<const uint8_t>>;
using FieldWrites = std::span<const std::span<uint8_t>>;

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

    // Auxiliary fields (F-031). Gathered and produced here rather than
    // returned in CellTransition because a vector per cell is exactly the
    // allocation invariant 8 forbids.
    std::vector<ExprValue> fieldSelf;     // F entries: field f at this site
    std::vector<ExprValue> fieldNbr;      // F*N entries: field f at neighbour i
                                          // at f*N + i, canonical order
    std::vector<ExprValue> fieldNext;     // F entries: what each field becomes.
                                          // A field the rule does not write
                                          // holds its own value, because on a
                                          // ping-pong pair keeping a value
                                          // means copying it rather than
                                          // leaving it alone.
};

// What one cell's transition produced. Plain data, returned by value.
// A continuous rule fills the float members and leaves the state members at
// zero; a discrete one does the reverse.
struct CellTransition {
    // Continuous only (Phase 5).
    float    ownValue    = 0.0f;   // the value that was read
    float    convolution = 0.0f;   // the kernel's response at this cell
    float    increment   = 0.0f;   // what the growth function returned
    float    nextValue   = 0.0f;   // what is written, clamped to [0, 1]

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
                        StepScratch& scratch, FieldReads fields = {});

// One generation: reads `current`, writes `next`. The two must be distinct
// buffers of spec.bytesPerBuffer() bytes; passing the same span twice is the
// AV-004 defect and is rejected. Does not swap. `generation` is the index of
// the generation being read; cell mutation hashes it (SPEC §9.2).
// `fields` and `fieldsNext` are the auxiliary field pairs, one entry per
// declared field, and must both be present when the rule declares any: a
// field is read from the first and written to the second whether or not the
// rule writes it, so the pair swaps with the state's pair and stays in step
// with it.
void cpuStep(const rule::CompiledRule& rule, const core::GridSpec& spec,
             std::span<const uint8_t> current, std::span<uint8_t> next,
             uint64_t generation = 0, CellMutation mutation = {},
             FieldReads fields = {}, FieldWrites fieldsNext = {});

// One generation on a HostGrid, then swap, so the result is grid.current().
void cpuStep(const rule::CompiledRule& rule, core::HostGrid& grid,
             uint64_t generation = 0, CellMutation mutation = {});

}  // namespace aether::sim
