// Rule IR (SPEC §4).
//
// The single compile target. Front ends produce it, backends consume it,
// nothing outside rule/ constructs one by hand. Every field is plain data so
// that the structure can be hashed, serialised, diffed and mutated without
// knowing which front end produced it.
//
// Changing the shape of anything in this file is a change to SPEC §4 and
// needs a DECISIONS entry (CLAUDE.md §Out of scope).

#pragma once

#include "core/cell.hpp"
#include "rule/neighbourhood.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aether::rule {

constexpr uint16_t kIrVersion = 1;

using core::CellType;   // storage type is core's; the IR names it
enum class Boundary : uint8_t { Wrap, Zero, Mirror };
enum class Kind : uint8_t { OuterTotalistic, Totalistic, NonTotalistic, Expression, Continuous };

// --- Transition forms --------------------------------------------------------

// Flat array of next-state indices, laid out per SPEC §5 (see table_layout.hpp).
struct Table {
    std::vector<uint8_t> entries;

    bool operator==(const Table&) const = default;
};

// Expression tree over own state and neighbours. Stored as an arena in which
// every node's children precede it, so the structure is acyclic by
// construction and "select a uniformly random node" (SPEC §9.1) is an index
// draw. The root is the last node.
enum class ExprOp : uint8_t {
    Self,          // own state                          -> Int
    Neighbour,     // neighbour[a]  (a = canonical index) -> Int
    Count,         // number of neighbours in state a     -> Int
    IntLiteral,    // ival                                -> Int
    FloatLiteral,  // fval                                -> Float
    Add, Sub, Mul, Div, Mod,   // (a, b) numeric, same type
    Eq, Ne, Lt, Le, Gt, Ge,    // (a, b) numeric, same type -> Bool
    And, Or,                   // (a, b) Bool
    Not,                       // (a)    Bool
    Select,                    // (a ? b : c), b and c same type
};

struct ExprNode {
    ExprOp   op   = ExprOp::IntLiteral;
    uint32_t a    = 0;   // child index, or neighbour/state index for Neighbour/Count
    uint32_t b    = 0;
    uint32_t c    = 0;
    int64_t  ival = 0;
    float    fval = 0.0f;

    bool operator==(const ExprNode&) const = default;
};

struct Expression {
    std::vector<ExprNode> nodes;

    bool operator==(const Expression&) const = default;
};

// Convolution kernel plus growth function. Specified from v1 (D-010),
// implemented in Phase 5. Backends reject it until then.
struct Kernel {
    enum class Shape : uint8_t { Radial, Explicit };

    Shape              shape = Shape::Radial;
    std::vector<float> profile;   // Radial: samples from centre to radius.
                                  // Explicit: (2r+1)^d row-major weights.
    Expression         growth;    // Self refers to the convolution result.

    bool operator==(const Kernel&) const = default;
};

using Transition = std::variant<Table, Expression, Kernel>;

// --- The IR ------------------------------------------------------------------

struct Metadata {
    std::optional<std::string> name;
    std::optional<std::string> author;
    std::optional<std::string> source_notation;

    bool operator==(const Metadata&) const = default;
};

struct RuleIR {
    uint16_t      ir_version    = kIrVersion;
    uint8_t       dimensions    = 2;
    CellType      cell_type     = CellType::U8;
    uint16_t      states        = 2;
    Neighbourhood neighbourhood = {};
    Boundary      boundary      = Boundary::Wrap;
    Kind          kind          = Kind::OuterTotalistic;
    Transition    transition    = Table{};
    Metadata      metadata      = {};

    bool operator==(const RuleIR&) const = default;
};

// --- Validation (SPEC §4 "Validation") ---------------------------------------

struct Diagnostic {
    std::string message;
};

// Empty result means valid. Runs on every IR regardless of origin, including
// mutated ones (AV-012).
std::vector<Diagnostic> validate(const RuleIR& ir);
bool isValid(const RuleIR& ir);

// --- Hash (SPEC §4 "IR hash") ------------------------------------------------

// 64-bit hash over the canonical serialisation of every field except
// metadata. Stable across platforms and processes.
uint64_t irHash(const RuleIR& ir);

// --- Names -------------------------------------------------------------------

std::string_view toString(Boundary v);
std::string_view toString(Kind v);
std::string_view toString(NeighbourhoodType v);
std::string_view toString(ExprOp v);

std::optional<Boundary>          parseBoundary(std::string_view s);
std::optional<Kind>              parseKind(std::string_view s);
std::optional<NeighbourhoodType> parseNeighbourhoodType(std::string_view s);

}  // namespace aether::rule
