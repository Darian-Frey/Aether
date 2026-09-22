// The cell inspector (F-030, D-018, AV-017).
//
// What one cell is about to do and why: the state it holds, every neighbour
// as the step gathered it, the number the rule reduced them to, the table
// entry or expression branch that answered, and the state that will be
// written.
//
// None of that is computed here. `sim::stepCell` already works all of it out
// for every cell of every generation and throws it away, so this asks the
// oracle and reads the working out of `StepScratch` (IMP-005 put the entry
// point there for exactly this). That matters more than it looks: a second
// implementation of SPEC §5's index arithmetic or SPEC §6's evaluation rules
// would be free to drift from the first while being the thing consulted
// precisely when nobody can check the answer. A divergent backend produces
// visibly odd automata; a divergent inspector produces confident prose.
//
// Host-side and GL-free, like the scratch pad it was built for, so it is
// testable with no display.

#pragma once

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"
#include "sim/hash.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace aether::sim {

// One neighbour, as the step saw it.
struct NeighbourCell {
    rule::Offset offset;              // its place in the neighbourhood, SPEC §3 order
    uint8_t      state   = 0;         // the state that was gathered
    float        value   = 0.0f;      // continuous rules
    bool         outside = false;     // a zero boundary sent it off the grid: read as 0
    bool         wrapped = false;     // it came from somewhere other than own + offset
    uint32_t     x = 0, y = 0, z = 0; // where it was actually read; meaningless if outside
};

// What the kind reduced the neighbourhood to. Which member carries meaning
// depends on the rule, which is why each says so rather than being guessed
// at from a zero.
struct Reduction {
    // OuterTotalistic: one count per state, 1 .. S-1. Expression rules: one
    // per state, 0 .. S-1, since an expression may ask about state 0.
    std::vector<uint32_t> perState;
    bool     perStateFromZero = false;
    bool     hasPerState      = false;

    // CountedTotalistic: how many neighbours were in the counted set for this
    // own state. Totalistic: the sum of own and neighbours.
    bool     hasScalar = false;
    uint32_t scalar    = 0;
    const char* scalarMeans = "";
};

struct Inspection {
    uint32_t x = 0, y = 0, z = 0;
    uint64_t generation = 0;

    // Straight from the oracle. `transition.next` is what the step will
    // write, mutation included.
    CellTransition transition;

    std::vector<NeighbourCell> neighbours;
    Reduction                  reduction;

    // Codegen rules only: the node index of the conditional whose test held
    // and supplied the answer, read out of the arena the oracle filled rather
    // than re-evaluated. Empty when the expression is not a chain of
    // conditionals, or when every test failed and the final alternative
    // answered — which is a fact about the rule worth saying plainly.
    std::optional<size_t> clause;
    bool                  clauseIsDefault = false;
};

// One cell, explained. Reads `cells` and writes nothing; `scratch` is filled
// as a side effect, exactly as a step would fill it.
Inspection inspect(const rule::CompiledRule& rule, const core::GridSpec& spec,
                   std::span<const uint8_t> cells,
                   uint32_t x, uint32_t y, uint32_t z,
                   uint64_t generation, CellMutation mutation,
                   StepScratch& scratch);

}  // namespace aether::sim
