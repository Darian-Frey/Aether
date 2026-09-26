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

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aether::rule {

constexpr uint16_t kIrVersion = 1;

using core::CellType;   // storage type is core's; the IR names it
enum class Boundary : uint8_t { Wrap, Zero, Mirror };
enum class Kind : uint8_t { OuterTotalistic, Totalistic, NonTotalistic, Expression, Continuous,
                           CountedTotalistic };

// A set of states, as a 256-bit mask. Used by CountedTotalistic, where the
// transition depends on how many neighbours fall in a set chosen by the
// cell's own state (D-016): counting one set rather than every state
// separately is the difference between S·(N+1) table entries and
// S·C(N+S−1, S−1).
struct StateSet {
    std::array<uint32_t, 8> bits{};

    constexpr bool test(uint16_t s) const { return ((bits[s >> 5] >> (s & 31u)) & 1u) != 0u; }
    constexpr void set(uint16_t s) { bits[s >> 5] |= 1u << (s & 31u); }
    constexpr bool empty() const {
        for (uint32_t w : bits) if (w != 0) return false;
        return true;
    }
    bool operator==(const StateSet&) const = default;
};

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
    // Auxiliary fields (F-031, D-022). `a` is the field index, and the field's
    // declared cell type decides whether the node is Int or Float — which is
    // why a field a rule never declared is a validation error rather than a
    // read of zero: there would be no type to give it.
    FieldSelf,                 // field[a] at this site
    FieldNeighbour,            // field[a] at neighbour b, canonical order
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

// What an expression node evaluates to. The validator and the GLSL
// generator share this so that the two cannot disagree about a tree's
// typing (SPEC §6).
enum class ExprType : uint8_t { Int, Float, Bool, Invalid };

// One entry per node, Invalid where the node is ill-typed. `neighbours` and
// `states` bound the Neighbour and Count indices. `selfIsFloat` says which
// thing Self is: the convolution result inside a growth expression, the own
// state everywhere else (BUG-010).
std::vector<ExprType> expressionTypes(const Expression& e, uint32_t neighbours, uint16_t states,
                                      bool selfIsFloat = false,
                                      std::span<const CellType> fieldTypes = {});

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

// --- Fields (F-031, D-022) ---------------------------------------------------
//
// A site may carry more than one value: the state, which every rule has and
// which keeps the storage and the layout it has always had, plus any number of
// declared auxiliary fields, each its own texture. D-019 chose this over
// widening the cell into a record because it is additive — SPEC §1 still says
// a cell holds one value, and what gained a dimension is the site.
//
// An empty list is exactly the grid this engine has had since Phase 1, so
// every existing rule hashes to what it hashed to before.
struct Field {
    std::string    name;                        // how a front end refers to it
    CellType       cell_type = CellType::U8;
    // A rule need not write every field it declares; one it leaves alone keeps
    // its value, which is what makes a read-only field (the resource of F-032
    // seen by a rule that does not consume it) cost nothing to express.
    std::optional<Expression> write;

    bool operator==(const Field&) const = default;
};

// --- The IR ------------------------------------------------------------------

struct Metadata {
    std::optional<std::string> name;
    std::optional<std::string> author;
    std::optional<std::string> source_notation;
    // First state of the ageing tail, when the rule has one (SPEC §7 decay).
    // A presentation hint for palettes and age shading, excluded from the
    // hash with the rest of the metadata and carrying no semantics.
    std::optional<uint16_t>    decay_from;

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
    // For CountedTotalistic: which states each own state counts. One entry
    // per state; empty for every other kind.
    std::vector<StateSet> counted;
    // Auxiliary fields, beyond the state. Empty for every rule written before
    // F-031 and for every rule that wants one field, which is most of them.
    std::vector<Field>    fields;
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
