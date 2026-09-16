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
    uint8_t               cellMutationBlock = 0;

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

// A buffer as it goes into the file. Two inline encodings exist and the writer
// picks whichever comes out shorter: `rle` is byte run-length pairs
// (count <= 255, value), `bytes` is the buffer itself. Both are then base64.
// A run-length pass costs two bytes per run, so it doubles data that has no
// runs, which is what float grids are (IMP-006); `bytes` is the floor.
// The third encoding, `raw`, names a sidecar file and is chosen by
// saveSession rather than here.
struct EncodedCells {
    std::string encoding;
    std::string data;
};

// Round-trips exactly. These work in bytes, not cells, so an f32 grid encodes
// its raw float bytes and needs no separate path; `expectedBytes` is
// GridSpec::bytesPerBuffer(), which is the cell count only for u8.
EncodedCells encodeCells(const std::vector<uint8_t>& cells);
std::variant<std::vector<uint8_t>, SessionError> decodeCells(const std::string& encoding,
                                                             const std::string& text,
                                                             size_t expectedBytes);

std::string sessionToJson(const Session& s);
std::variant<Session, SessionError> sessionFromJson(const std::string& text);

// Grids whose buffer exceeds this many bytes go to a raw sidecar file
// `<path>.grid` rather than inline text. Counted in bytes rather than cells so
// that an f32 grid, which is four times the size for the same extent and whose
// float bytes barely run-length encode at all, reaches the sidecar where a u8
// grid of the same extent still fits inline.
constexpr uint64_t kInlineByteLimit = 4u * 1024u * 1024u;

std::optional<SessionError> saveSession(const std::string& path, const Session& s);
std::variant<Session, SessionError> loadSession(const std::string& path);

}  // namespace aether::sim
