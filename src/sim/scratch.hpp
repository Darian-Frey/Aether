// The pattern editor's scratch pad (F-029, D-018).
//
// A small grid of its own, stepped on the CPU path, with no GPU copy, no
// journal and no place in the session. That is the whole point of it: the
// scratch pad is not part of the run, so drawing on it neither replays nor
// perturbs replay, and experimenting with a creature does not disturb the
// simulation being watched. D-018 records why this was preferred to editing
// the live grid in place.
//
// Being small and host-side it can afford what the main grid cannot. Stepping
// backwards is the example: a ring of previous generations costs nothing at
// 64x64 and would be unthinkable at 256^3.
//
// Cell mutation is deliberately not applied here. An editor whose grid changes
// under you when you step forward and back over the same generation is not an
// editor, and authoring wants the rule alone.
//
// Nothing in this file touches GL, so the scratch pad is testable with no
// display — which most of `ui/` is not.

#pragma once

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"
#include "sim/pattern.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace aether::sim {

class Scratch {
public:
    // How many generations can be stepped back through. A ring, so the oldest
    // is dropped rather than the newest refused, and the buffers are allocated
    // once and reused: stepping is as free of allocation as the real loop is
    // (ARCHITECTURE §Key invariants 8).
    static constexpr size_t kHistory = 64;

    // Fails only if the rule will not compile; a caller holding an IR the
    // simulation is already running cannot hit that.
    static std::variant<Scratch, PatternError> make(core::GridSpec spec, const rule::RuleIR& ir);

    const core::GridSpec&     spec() const { return grid_.spec(); }
    const rule::RuleIR&       ir()   const { return ir_; }
    const rule::CompiledRule& rule() const { return compiled_; }
    const core::HostGrid&     grid() const { return grid_; }
    uint64_t                  generation() const { return generation_; }

    // How many generations are available to step back through.
    size_t history() const { return historyCount_; }

    // A new extent, keeping whatever of the drawing still fits in it. Resets
    // the generation and forgets the history: those describe a grid that no
    // longer exists. Refuses an extent of zero or a change of dimensionality
    // away from the rule's.
    std::optional<PatternError> resize(uint32_t w, uint32_t h, uint32_t d);

    // Adopt a rule. Keeps the drawing, and any cell holding a state the new
    // rule does not have is reset to 0 — the same treatment a live grid gets
    // on a rule change, since a state index outside the table is not a thing
    // the stepper can evaluate. Forgets the history, whose generations were
    // produced by a different rule. Refuses a rule of another dimensionality
    // or cell type; `resize` and a fresh pad are how those change.
    std::optional<PatternError> setRule(const rule::RuleIR& ir);

    uint8_t get(uint32_t x, uint32_t y, uint32_t z = 0) const { return grid_.get(x, y, z); }

    // Paint one cell. Out-of-range coordinates and states are ignored rather
    // than clamped: a brush that runs off the edge should do nothing there,
    // not smear along it.
    void set(uint32_t x, uint32_t y, uint32_t z, uint8_t state);

    // Everything to state 0, generation to 0, history forgotten.
    void clear();

    // One generation forward, the current one pushed onto the history first.
    void step();

    // Back one generation. False when the history is empty, which it is at
    // generation 0 and after kHistory steps have pushed the rest out.
    bool stepBack();

    // The whole pad as a pattern, or a region of it. What comes out is an
    // ordinary pattern as SPEC §14 defines one — there is no editor format.
    Pattern toPattern() const;
    std::variant<Pattern, PatternError> toPattern(uint32_t x, uint32_t y, uint32_t z,
                                                  uint32_t w, uint32_t h, uint32_t d) const;

    // A pattern into the pad at (x, y, z), checked against the pad's own rule
    // and extent exactly as a placement into the live grid is checked. Resets
    // the generation and the history: what is on the pad is no longer the
    // descendant of what was.
    std::optional<PatternError> place(const Pattern&, uint32_t x, uint32_t y, uint32_t z);

private:
    Scratch(core::GridSpec spec, rule::RuleIR ir, rule::CompiledRule compiled);

    void forgetHistory();

    core::HostGrid     grid_;
    rule::RuleIR       ir_;
    rule::CompiledRule compiled_;
    StepScratch        scratch_;
    uint64_t           generation_ = 0;

    // A ring of previous generations. `historyHead_` is where the next push
    // goes, `historyCount_` how many of the slots hold a generation.
    std::vector<std::vector<uint8_t>> history_;
    size_t                            historyHead_  = 0;
    size_t                            historyCount_ = 0;
};

}  // namespace aether::sim
