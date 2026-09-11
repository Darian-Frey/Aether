// Rule DSL front end (SPEC §7).
//
// Three notations, tried in order: Life-like B/S, Generations B/S/C, and the
// table block. All three produce a RuleIR and nothing else; this module never
// sees a backend.

#pragma once

#include "rule/ir.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace aether::rule {

// Things the notation does not carry and the session must supply.
struct DslContext {
    uint8_t  dimensions = 2;
    Boundary boundary   = Boundary::Wrap;   // overridable by a table block header
};

struct ParseError {
    uint32_t    line   = 1;
    uint32_t    column = 1;
    std::string message;
};

struct DslResult {
    std::optional<RuleIR>     ir;
    std::optional<ParseError> error;

    explicit operator bool() const { return ir.has_value(); }
};

// Parses `source` into a validated IR. On failure the result carries a
// position and a message and no IR; the caller's previously compiled rule is
// untouched because nothing here touches the caller (AV-014).
DslResult parseDsl(std::string_view source, const DslContext& ctx = {});

}  // namespace aether::rule
