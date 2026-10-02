// Hard cell lifespan (SPEC §7 `lifespan`, F-034, D-019).
//
// A front-end transform, IR in and IR out, and the mirror of `decay`. Where
// `decay N` appends states *after* a death so a cell has somewhere to fade to,
// `lifespan L` inserts states *before* one so a cell has a deadline: a cell is
// born at age 1, advances an age for every generation it survives, and dies at
// age L whatever its neighbours say.
//
// The two are not alternatives and compose in that order — ages, then a tail —
// which is why F-034's acceptance says "alongside" rather than "instead of".
//
// Nothing downstream learns a new concept, which is the whole point of the route:
// an age is an ordinary state and the result is an ordinary table rule. A cell's
// age being its state index is also what makes the design note's fertility windows
// and juvenile periods need no feature at all — they are conditions over states.
//
// The result is `counted_totalistic` rather than `outer_totalistic` whatever the
// input, and that is forced rather than chosen: every age must count as a live
// neighbour, and counting a *set* of states as one thing is exactly what the
// counted kind is for (D-016). An outer-totalistic table would need a separate
// count per age and would grow combinatorially in L for no gain.

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <string>
#include <variant>

namespace aether::rule {

// Longest lifespan that still fits LUT_MAX_ENTRIES and SPEC §1 for this shape.
uint16_t maxLifespan(const RuleIR& base);

// `ages` <= 1 returns the rule unchanged: a lifespan of one is what a Life-like
// rule already has, since a cell either survives into the same state or dies.
// Fails, with a message naming the longest lifespan that fits, when the result
// would exceed 256 states or the table threshold.
std::variant<RuleIR, std::string> applyLifespan(const RuleIR& base, uint16_t ages);

}  // namespace aether::rule
