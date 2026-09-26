// GLSL codegen (F-009, D-004, SPEC §6).
//
// Turns an expression IR into the body of the function the compute step
// calls. What comes out obeys the contract in SPEC §6: no loops with
// data-dependent bounds, no side effects, no texture access, integer
// arithmetic only for u8 rules. It is a string; nothing here touches GL.

#pragma once

#include "rule/ir.hpp"

#include <string>
#include <string_view>
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

// --- The multi-field shape (F-031, D-022) ------------------------------------
//
// A rule declaring auxiliary fields generates a struct and one function per
// written field beside the transition:
//
//     struct AetherFields { int f0_self; int f0_nbr[N]; float f1_self; ... };
//     uint  aether_rule(uint self, uint nbr[N], AetherFields fld);
//     int   aether_field_0(uint self, uint nbr[N], AetherFields fld);
//     float aether_field_1(uint self, uint nbr[N], AetherFields fld);
//
// Everything still arrives as a parameter, so SPEC §6's prohibition on texture
// access and global writes inside a rule function holds: one struct is gathered
// once and handed to every function, which is also what makes "decided against
// one reading of the world" literally true rather than merely intended.
//
// The struct is filled by code `sim/gpu_step` generates, because the image
// bindings are its business and not this file's. That makes two generators that
// must agree on every name — so they agree by both calling the four functions
// below rather than by both spelling them out. Nothing else may spell them.
std::string glslFieldsStruct();                  // "AetherFields"
std::string glslFieldSelfMember(size_t index);   // "f0_self"
std::string glslFieldNbrMember(size_t index);    // "f0_nbr"
std::string glslFieldFunction(size_t index);     // "aether_field_0"

// The GLSL type a field of this cell type reads and writes as: `int` for u8,
// `float` for f32, matching the type the validator gives a read of it.
std::string glslFieldType(CellType type);

// --- Flush-to-zero (SPEC §6, BUG-021) ----------------------------------------
//
// FLT_MIN, the smallest normal float, as GLSL text, and the statement that
// zeroes anything smaller. This is the only place that number is written:
// `sim/gpu_step` turns it into the `AETHER_FTZ` macro both shaders use, and
// `sim/cpu_step`'s twin reads it as `std::numeric_limits<float>::min()`, which
// is the same value. GLSL need not support subnormals and both GPUs measured
// here flush them where C++ does not, so a float expression that decays parts
// company with the oracle unless both sides flush deliberately.
std::string glslSubnormalMin();
std::string glslFlushStatement(std::string_view variable);

}  // namespace aether::rule
