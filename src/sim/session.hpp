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

// One auxiliary field's contents in a session (F-031). The name and cell type
// travel with the buffer so that a session can be checked against the rule it
// names rather than trusted positionally: a field list that disagrees with the
// rule's would otherwise read one field's bytes as another's.
//
// There is no `initial` here. A field starts at zero — nothing seeds one, and
// the rule writes it from the state — so generation 0 is derivable and replay
// reconstructs it by zeroing. When field seeding arrives (F-032, F-036) this
// gains an initial buffer and the journal gains an event for it.
struct SessionField {
    std::string          name;
    core::CellType       cell_type = core::CellType::U8;
    std::vector<uint8_t> current;   // at `generation`; empty if not stored
};

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

    // Auxiliary fields at `generation`, in the rule's declaration order and as
    // long as `rule.fields`. Empty for every session written before F-031 and
    // for every rule that declares no field, which is why the JSON key is
    // optional: an older session loads exactly as it always did.
    std::vector<SessionField> fields;
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

// Whether this session's buffers go to a sidecar rather than inline text.
// The state's buffer keeps the threshold it has always had, so which files get
// a sidecar is unchanged for a rule that declares no field; the fields' total
// is tested the same way and separately, because three small fields beside a
// small grid is still a lot of base64 (F-031).
//
// The sidecar's layout is the state's initial, the state's current, then each
// field's current, in declaration order. It is written and read in that one
// order rather than as two agreeing sets of offsets.
bool     usesSidecar(const Session& s);
uint64_t sessionFieldBytes(const Session& s);

std::optional<SessionError> saveSession(const std::string& path, const Session& s);
std::variant<Session, SessionError> loadSession(const std::string& path);

}  // namespace aether::sim
