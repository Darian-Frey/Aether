// Lua rule front end (F-008, D-003, SPEC §8).
//
// A script runs exactly once, at compile time, and returns a table that
// describes a rule. The interpreter is created and destroyed inside the
// call, and the chunk runs with its own environment, so nothing Lua-owned
// survives the compile and no step loop has an interpreter to call into
// even by accident (AV-008).

#pragma once

#include "rule/ir.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace aether::rule {

// SPEC §8. The instruction budget is counted rather than timed so the abort
// point does not move with machine speed; the memory budget exists because a
// script can exhaust memory well inside the instruction budget.
constexpr uint64_t kLuaInstructionBudget = 50'000'000;
constexpr size_t   kLuaMemoryBudget      = 256u * 1024u * 1024u;

struct LuaError {
    std::string message;
};

struct LuaContext {
    uint8_t  dimensions = 2;      // when the script does not say
    Boundary boundary   = Boundary::Wrap;
    uint64_t instructionBudget = kLuaInstructionBudget;
    size_t   memoryBudget      = kLuaMemoryBudget;
};

// Runs `source` and converts what it returns. The result is validated, so a
// caller never sees a malformed IR.
std::variant<RuleIR, LuaError> compileLua(std::string_view source, const LuaContext& ctx = {});

}  // namespace aether::rule
