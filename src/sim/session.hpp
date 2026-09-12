// Session files (F-020, SPEC §11).
//
// A Session is the serialisable record of a run: grid spec, initial cells,
// seeds, the journal, the lineage, the mutation parameters, and — as
// conveniences that the replay test verifies against the record — the
// current cells and stream A's state at save time.

#pragma once

#include "core/grid.hpp"
#include "rule/ir.hpp"
#include "sim/journal.hpp"
#include "sim/lineage.hpp"
#include "sim/rng.hpp"
#include "sim/rule_mutation.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aether::sim {

constexpr int kSessionFormatVersion = 1;

struct Session {
    core::GridSpec        spec;
    rule::Boundary        boundary = rule::Boundary::Wrap;   // the session's default (SPEC §11 "grid.boundary")
    std::vector<uint8_t>  initial;          // cells at generation 0, before any journal event
    uint64_t              seedA = 0;
    uint64_t              seedB = 0;
    Journal               journal;
    std::vector<LineageEntry> lineage;
    RuleMutationParams    ruleMutation;     // current parameters
    double                cellMutationP = 0.0;

    // Conveniences: state at `generation`, derivable by replay.
    uint64_t              generation = 0;
    std::vector<uint8_t>  current;          // empty if not stored
    std::optional<Pcg32::State> streamA;
    rule::RuleIR          rule;             // the current rule
    uint64_t              ruleMutationsApplied = 0;
    uint64_t              ruleMutationsSkipped = 0;
};

struct SessionError {
    std::string message;
};

// Cells <-> the compact text form used in the file: byte run-length pairs,
// base64. Round-trips exactly.
std::string encodeCells(const std::vector<uint8_t>& cells);
std::variant<std::vector<uint8_t>, SessionError> decodeCells(const std::string& text, size_t expectedCount);

std::string sessionToJson(const Session& s);
std::variant<Session, SessionError> sessionFromJson(const std::string& text);

// Grids above this many cells go to a raw sidecar file `<path>.grid` rather
// than inline text.
constexpr uint64_t kInlineCellLimit = 4u * 1024u * 1024u;

std::optional<SessionError> saveSession(const std::string& path, const Session& s);
std::variant<Session, SessionError> loadSession(const std::string& path);

}  // namespace aether::sim
