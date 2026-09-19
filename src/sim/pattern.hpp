// Pattern files (F-012, D-017, SPEC §14).
//
// A pattern is a fragment of a grid: an extent, a lattice, a state count and
// the cells, with no rule and no history. Two formats carry it, and which one
// a pattern goes into is decided by what the pattern is rather than by the
// caller (D-017):
//
//   .rle      Golly's extended RLE. Reads the plain two-state form every Life
//             tool writes and the multi-state form with its `.A-X`, `pA-pX`
//             alphabet, so Generations, cyclic and Wireworld patterns pass
//             both ways. 2D square lattices only, u8 only — that is what the
//             format can say, not a limitation of ours.
//   .pattern  Native. JSON with the cells through the session codec, so it
//             carries hexagonal lattices, 3D extents, any state count and f32
//             cells. A separate extension rather than a private RLE dialect:
//             a file claiming to be RLE and failing to open in Golly would
//             read as somebody else's defect.
//
// Lives beside `session` because it shares that file's cell codec and is the
// same kind of thing — grid data on its way to and from disk. Placing a
// pattern into a running grid belongs to `Simulation`, which has to journal it.

#pragma once

#include "core/cell.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace aether::sim {

// Patterns are stored on a lattice, and a hexagonal one is not interchangeable
// with a square one even at the same extent: the offsets differ, so the same
// cells mean a different shape (SPEC §3).
enum class Lattice : uint8_t { Square, Hexagonal };

std::string_view          toString(Lattice);
std::optional<Lattice>    parseLattice(std::string_view);

struct Pattern {
    uint8_t        dimensions = 2;
    uint32_t       width = 1, height = 1, depth = 1;
    Lattice        lattice   = Lattice::Square;
    core::CellType cell_type = core::CellType::U8;
    // The highest state the cells use, plus one. Not the rule's state count:
    // a pattern drawn for a 14-state rule that happens to use three of them
    // fits any rule with at least three, and refusing it would be pedantry.
    uint16_t       states    = 2;

    // Row-major, x fastest then y then z, as HostGrid stores a grid. Bytes,
    // so an f32 pattern holds four per cell (SPEC §1).
    std::vector<uint8_t> cells;

    std::optional<std::string> name;
    std::optional<std::string> rule;      // the `rule =` header, kept as written
    std::optional<std::string> comment;

    uint64_t cellCount() const { return uint64_t{width} * height * depth; }
    uint64_t bytesNeeded() const { return cellCount() * core::cellBytes(cell_type); }

    // Empty when well-formed.
    std::vector<std::string> problems() const;

    bool operator==(const Pattern&) const = default;
};

struct PatternError {
    std::string message;
};

enum class Format : uint8_t { Rle, Native };

// Which format can carry this pattern. RLE where it reaches, native where it
// does not — the choice is the pattern's, not the caller's.
Format formatFor(const Pattern&);

// Text in, pattern out. `parsePattern` sniffs the format rather than trusting
// an extension, since a pattern downloaded from anywhere may be named badly.
std::variant<Pattern, PatternError> parseRle(std::string_view text);
std::variant<Pattern, PatternError> parseNative(std::string_view text);
std::variant<Pattern, PatternError> parsePattern(std::string_view text);

// Pattern out, text in the named format. Writing a pattern as RLE that RLE
// cannot carry is an error rather than a silent truncation.
std::variant<std::string, PatternError> writeRle(const Pattern&);
std::string                             writeNative(const Pattern&);
std::variant<std::string, PatternError> writePattern(const Pattern&, Format);

// The file extension a format uses, without the dot.
std::string_view extensionFor(Format);

}  // namespace aether::sim
