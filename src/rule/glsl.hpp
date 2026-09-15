// GLSL codegen (F-009, D-004, SPEC §6).
//
// Turns an expression IR into the body of the function the compute step
// calls. What comes out obeys the contract in SPEC §6: no loops with
// data-dependent bounds, no side effects, no texture access, integer
// arithmetic only for u8 rules. It is a string; nothing here touches GL.

#pragma once

#include "rule/ir.hpp"

#include <string>
#include <variant>

namespace aether::rule {

struct GlslError {
    std::string message;
};

// The complete definition of
//
//     uint aether_rule(uint self, uint nbr[N]);
//
// for an expression-form `u8` rule. Every value it can return is a state:
// the result is clamped, because nothing can prove in general that an
// arithmetic tree stays in range, and a cell outside `0 … S-1` would index
// past the next generation's count array (SPEC §6).
std::variant<std::string, GlslError> generateGlsl(const RuleIR& ir);

}  // namespace aether::rule
