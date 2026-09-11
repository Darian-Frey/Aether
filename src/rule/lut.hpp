// Lookup-table backend (SPEC §5, D-004).
//
// Turns a table-backed IR into the form both execution paths consume: the
// table bytes, the layout that indexes them, the canonical neighbour offsets,
// and the W table the multi-state ranking needs. Nothing here touches GL; the
// GPU path uploads what this produces.

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

struct LutRule {
    uint64_t            ir_hash;
    uint8_t             dimensions;
    uint16_t            states;
    Kind                kind;
    Neighbourhood       neighbourhood;
    Boundary            boundary;
    std::vector<Offset> offsets;     // canonical order, SPEC §3
    TableLayout         layout;
    std::vector<uint8_t>  table;     // exactly layout.size() entries
    std::vector<uint32_t> w;         // (N+1) x S compositions, row-major by n;
                                     // empty unless kind == OuterTotalistic

    uint32_t neighbourCount() const { return static_cast<uint32_t>(offsets.size()); }
};

// Fails for IRs the table backend does not serve: non-table forms, f32, or a
// table over the threshold. A failure allocates no table (AV-010).
std::variant<LutRule, CompileError> compileLut(const RuleIR& ir);

}  // namespace aether::rule
