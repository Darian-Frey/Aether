// The journal: every externally driven change to a running simulation,
// stamped with the generation at which it happened (SPEC §11).
//
// Rule mutations are not journaled — they regenerate from stream A. Everything
// else that moves the run is: user rule changes, rewinds, painting, fills,
// clears and mutation-parameter changes. Replaying the journal against the
// initial state and the seeds reproduces the run; this is what makes the
// session's "mutation schedule" concrete.

#pragma once

#include "rule/ir.hpp"
#include "sim/rule_mutation.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aether::sim {

struct EvSetRule     { rule::RuleIR ir; };
struct EvRewind      { size_t entry; };
struct EvPaint       { uint32_t x0, x1, y, z; uint8_t state; };
struct EvFill        { std::vector<double> density; };
struct EvClear       {};
struct EvCellMutation{ double p; uint8_t blockShift = 0; };
struct EvRuleMutation{ RuleMutationParams params; };

using EventBody = std::variant<EvSetRule, EvRewind, EvPaint, EvFill, EvClear, EvCellMutation, EvRuleMutation>;

struct Event {
    uint64_t  generation;
    EventBody body;
};

using Journal = std::vector<Event>;

}  // namespace aether::sim
