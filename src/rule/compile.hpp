// Rule compilation (SPEC §5, SPEC §6, D-004).
//
// Turns an IR into the form both execution paths consume. Which form depends
// on the rule: a table with the layout that indexes it and whatever auxiliary
// data that layout needs, or — where no finite table will serve — an
// expression, with the GLSL the GPU path will compile. Nothing here touches
// GL; the GPU path uploads or compiles what this produces.

#pragma once

#include "rule/ir.hpp"
#include "rule/table_layout.hpp"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace aether::rule {

enum class Backend : uint8_t { Lut, Codegen };

// D-004: automatic, from IR shape alone. Expression and Kernel forms, and
// tables over the threshold, go to codegen.
Backend selectBackend(const RuleIR& ir);

struct CompileError {
    std::string message;
};

struct CompiledRule {
    Backend             backend = Backend::Lut;
    uint64_t            ir_hash;
    uint8_t             dimensions;
    uint16_t            states;
    Kind                kind;
    Neighbourhood       neighbourhood;
    std::vector<StateSet> counted;   // CountedTotalistic only, one per state
    Boundary            boundary;
    std::vector<Offset> offsets;     // canonical order, SPEC §3
    TableLayout         layout;
    std::vector<uint8_t>  table;     // exactly layout.size() entries
    // Whatever else the kind's index arithmetic needs, uploaded as one
    // buffer: the (N+1) x S compositions for OuterTotalistic, eight words of
    // state mask per own state for CountedTotalistic, empty otherwise.
    std::vector<uint32_t> aux;

    // Codegen only: the tree the CPU path walks, the types the validator
    // inferred for it, and the GLSL the GPU path compiles.
    Expression            expression;
    std::vector<ExprType> expressionTypes;
    std::string           glsl;

    uint32_t neighbourCount() const { return static_cast<uint32_t>(offsets.size()); }
};

// Compiles through whichever backend the IR's shape selects (D-004). Fails
// for f32 rules, for a table over the threshold that has no expression form
// to fall back on, and for kernels. A failure allocates no table (AV-010).
std::variant<CompiledRule, CompileError> compileRule(const RuleIR& ir);

}  // namespace aether::rule
