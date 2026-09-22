#include "sim/pattern.hpp"

#include "rule/ir_json.hpp"
#include "sim/session.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <format>
#include <utility>

namespace aether::sim {

using nlohmann::json;

std::string_view toString(Lattice l) {
    switch (l) {
        case Lattice::Square:     return "square";
        case Lattice::Hexagonal:  return "hexagonal";
    }
    return "?";
}

std::optional<Lattice> parseLattice(std::string_view s) {
    if (s == "square")     return Lattice::Square;
    if (s == "hexagonal")  return Lattice::Hexagonal;
    return std::nullopt;
}

std::vector<std::string> Pattern::problems() const {
    std::vector<std::string> out;
    if (dimensions < 1 || dimensions > 3) {
        out.push_back(std::format("dimensions must be 1, 2 or 3 (got {})", dimensions));
        return out;
    }
    if (width == 0 || height == 0 || depth == 0) out.push_back("every extent must be at least 1");
    if (dimensions < 2 && height != 1) out.push_back("height must be 1 below 2D");
    if (dimensions < 3 && depth != 1) out.push_back("depth must be 1 below 3D");
    if (lattice == Lattice::Hexagonal && dimensions != 2) {
        out.push_back("a hexagonal pattern is 2D; hex neighbourhoods have no third axis");
    }
    if (cell_type == core::CellType::U8 && (states < 2 || states > 256)) {
        out.push_back(std::format("states must be in 2..256 (got {})", states));
    }
    if (!out.empty()) return out;

    if (cells.size() != bytesNeeded()) {
        out.push_back(std::format("{} bytes of cells for an extent needing {}", cells.size(), bytesNeeded()));
        return out;
    }
    if (cell_type == core::CellType::U8) {
        for (uint8_t c : cells) {
            if (c >= states) {
                out.push_back(std::format("a cell holds state {}, above the {} the pattern declares", c, states));
                break;
            }
        }
    }
    return out;
}

Format formatFor(const Pattern& p) {
    const bool rleCan = p.dimensions == 2 && p.lattice == Lattice::Square &&
                        p.cell_type == core::CellType::U8 && p.states <= 256;
    return rleCan ? Format::Rle : Format::Native;
}

std::string_view extensionFor(Format f) {
    return f == Format::Rle ? "rle" : "pattern";
}

std::string pathFor(std::string_view name, Format f, std::string_view dir) {
    std::string out = name.empty() ? std::string("pattern") : std::string(name);
    if (out.find('/') == std::string::npos && !dir.empty()) out = std::string(dir) + "/" + out;
    const std::string ext = "." + std::string(extensionFor(f));
    if (!out.ends_with(ext)) out += ext;
    return out;
}

// --- Extended RLE ------------------------------------------------------------------
//
// State 0 is `.` (or `b`), states 1..24 are `A`..`X`, and above that a prefix
// letter multiplies: `pA` is 25, `qA` is 49, up to `yX` at 255. Two-state
// patterns conventionally use `b` and `o`, which is what every Life tool
// writes and what this writes back for a two-state pattern.

namespace {

char stateTail(uint16_t s) { return static_cast<char>('A' + (s - 1) % 24); }
char statePrefix(uint16_t s) { return static_cast<char>('p' + (s - 1) / 24 - 1); }

std::string encodeState(uint16_t s, bool twoState) {
    if (s == 0) return twoState ? "b" : ".";
    if (twoState) return "o";
    if (s <= 24) return std::string(1, stateTail(s));
    return std::string(1, statePrefix(s)) + stateTail(s);
}

bool isRleHeaderLine(std::string_view line) {
    return line.starts_with("x") || line.starts_with("X");
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

}  // namespace

std::variant<Pattern, PatternError> parseRle(std::string_view text) {
    Pattern p;
    p.dimensions = 2;
    p.lattice = Lattice::Square;
    p.cell_type = core::CellType::U8;

    std::string body;
    bool haveHeader = false;
    std::string comments;

    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string_view line = trim(text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (line.empty()) continue;

        if (line.front() == '#') {
            // #N names, #C and #D comment, #r is an old-style rule line.
            const char kind = line.size() > 1 ? line[1] : ' ';
            std::string_view rest = trim(line.substr(std::min<size_t>(2, line.size())));
            if (kind == 'N') p.name = std::string(rest);
            else if (kind == 'r') p.rule = std::string(rest);
            else if (!rest.empty()) {
                if (!comments.empty()) comments += '\n';
                comments += rest;
            }
            continue;
        }
        if (!haveHeader && isRleHeaderLine(line)) {
            // x = W, y = H, rule = R
            haveHeader = true;
            size_t i = 0;
            while (i < line.size()) {
                while (i < line.size() && (line[i] == ' ' || line[i] == ',')) ++i;
                const size_t keyStart = i;
                while (i < line.size() && line[i] != '=' && line[i] != ',') ++i;
                if (i >= line.size() || line[i] != '=') break;
                std::string_view key = trim(line.substr(keyStart, i - keyStart));
                ++i;
                while (i < line.size() && line[i] == ' ') ++i;
                const size_t valStart = i;
                while (i < line.size() && line[i] != ',') ++i;
                std::string_view val = trim(line.substr(valStart, i - valStart));
                if (key == "x" || key == "X") {
                    uint32_t v = 0;
                    if (std::from_chars(val.data(), val.data() + val.size(), v).ec != std::errc{}) {
                        return PatternError{std::format("RLE header has a bad x: '{}'", val)};
                    }
                    p.width = v;
                } else if (key == "y" || key == "Y") {
                    uint32_t v = 0;
                    if (std::from_chars(val.data(), val.data() + val.size(), v).ec != std::errc{}) {
                        return PatternError{std::format("RLE header has a bad y: '{}'", val)};
                    }
                    p.height = v;
                } else if (key == "rule") {
                    p.rule = std::string(val);
                }
            }
            continue;
        }
        body += std::string(line);
    }

    if (!haveHeader) return PatternError{"not an RLE pattern: no 'x = ..., y = ...' header"};
    if (p.width == 0 || p.height == 0) return PatternError{"RLE header gives an empty extent"};
    if (!comments.empty()) p.comment = comments;

    // The body is runs of (count, tag), `$` ending a row and `!` the pattern.
    // Rows are padded to the declared width; a short row is dead cells, which
    // is how every RLE in the wild is written.
    std::vector<uint8_t> cells(size_t{p.width} * p.height, 0);
    uint32_t x = 0, y = 0;
    uint32_t count = 0;
    uint16_t highest = 0;
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c >= '0' && c <= '9') {
            count = count * 10 + static_cast<uint32_t>(c - '0');
            continue;
        }
        const uint32_t run = count == 0 ? 1 : count;
        count = 0;
        if (c == '!') break;
        if (c == '$') {
            y += run;
            x = 0;
            continue;
        }
        uint16_t state = 0;
        if (c == 'b' || c == '.') {
            state = 0;
        } else if (c == 'o') {
            state = 1;
        } else if (c >= 'A' && c <= 'X') {
            state = static_cast<uint16_t>(c - 'A' + 1);
        } else if (c >= 'p' && c <= 'y') {
            if (i + 1 >= body.size() || body[i + 1] < 'A' || body[i + 1] > 'X') {
                return PatternError{std::format("RLE has a '{}' not followed by a state letter", c)};
            }
            state = static_cast<uint16_t>((c - 'p' + 1) * 24 + (body[i + 1] - 'A' + 1));
            ++i;
        } else {
            return PatternError{std::format("RLE has an unexpected character '{}'", c)};
        }
        highest = std::max(highest, state);
        for (uint32_t k = 0; k < run; ++k) {
            if (y >= p.height) return PatternError{"RLE body has more rows than its header declares"};
            if (x >= p.width) return PatternError{"RLE body has a row longer than its header declares"};
            if (state != 0) cells[size_t{y} * p.width + x] = static_cast<uint8_t>(state);
            ++x;
        }
    }

    p.states = static_cast<uint16_t>(std::max<uint16_t>(2, highest + 1));
    p.cells = std::move(cells);
    if (const auto bad = p.problems(); !bad.empty()) return PatternError{bad.front()};
    return p;
}

std::variant<std::string, PatternError> writeRle(const Pattern& p) {
    if (formatFor(p) != Format::Rle) {
        return PatternError{std::format(
            "RLE carries 2D square u8 patterns; this one is {}D {} {}",
            p.dimensions, toString(p.lattice), core::toString(p.cell_type))};
    }
    if (const auto bad = p.problems(); !bad.empty()) return PatternError{bad.front()};

    const bool twoState = p.states <= 2;
    std::string out;
    if (p.name) out += std::format("#N {}\n", *p.name);
    if (p.comment) {
        size_t pos = 0;
        while (pos <= p.comment->size()) {
            const size_t nl = p.comment->find('\n', pos);
            out += std::format("#C {}\n", p.comment->substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    out += std::format("x = {}, y = {}", p.width, p.height);
    if (p.rule) out += std::format(", rule = {}", *p.rule);
    out += "\n";

    // Runs never cross a row: a row break is its own token, and keeping runs
    // inside a row is what every reader expects.
    std::string body;
    uint32_t blankRows = 0;
    for (uint32_t y = 0; y < p.height; ++y) {
        // Runs first, then emit. Only a *trailing* dead run is implied by the
        // row break; a leading one has to be written or every live cell in the
        // row shifts left by however many were dropped.
        std::vector<std::pair<uint16_t, uint32_t>> runs;
        for (uint32_t x = 0; x < p.width; ++x) {
            const uint16_t s = p.cells[size_t{y} * p.width + x];
            if (!runs.empty() && runs.back().first == s) ++runs.back().second;
            else runs.push_back({s, 1});
        }
        if (!runs.empty() && runs.back().first == 0) runs.pop_back();

        std::string row;
        for (const auto& [state, run] : runs) {
            if (run > 1) row += std::to_string(run);
            row += encodeState(state, twoState);
        }
        if (row.empty()) { ++blankRows; continue; }
        if (!body.empty()) {
            body += blankRows > 0 ? std::to_string(blankRows + 1) + "$" : "$";
        }
        blankRows = 0;
        body += row;
    }
    body += "!";

    // Wrapped at 70 columns, which is the convention every tool writes.
    for (size_t i = 0; i < body.size(); i += 70) {
        out += body.substr(i, 70);
        out += "\n";
    }
    return out;
}

// --- Native ------------------------------------------------------------------------

std::string writeNative(const Pattern& p) {
    json j;
    j["format"] = "aether-pattern";
    j["version"] = 1;
    j["extent"] = {{"dimensions", p.dimensions}, {"w", p.width}, {"h", p.height}, {"d", p.depth}};
    j["lattice"] = std::string(toString(p.lattice));
    j["cell_type"] = std::string(core::toString(p.cell_type));
    j["states"] = p.states;
    const EncodedCells enc = encodeCells(p.cells);
    j["cells"] = {{"encoding", enc.encoding}, {"data", enc.data}};
    if (p.name) j["name"] = *p.name;
    if (p.rule) j["rule"] = *p.rule;
    if (p.comment) j["comment"] = *p.comment;
    return j.dump(1);
}

std::variant<Pattern, PatternError> parseNative(std::string_view text) {
    try {
        const json j = json::parse(text);
        if (j.value("format", std::string{}) != "aether-pattern") {
            return PatternError{"not an Aether pattern: the 'format' field does not say so"};
        }
        if (j.value("version", 0) != 1) {
            return PatternError{std::format("pattern version {} is not one this build reads", j.value("version", 0))};
        }
        Pattern p;
        const json& e = j.at("extent");
        p.dimensions = e.at("dimensions").get<uint8_t>();
        p.width = e.at("w").get<uint32_t>();
        p.height = e.value("h", 1u);
        p.depth = e.value("d", 1u);
        const auto lat = parseLattice(j.value("lattice", "square"));
        if (!lat) return PatternError{"unknown lattice"};
        p.lattice = *lat;
        const auto ct = core::parseCellType(j.value("cell_type", "u8"));
        if (!ct) return PatternError{"unknown cell_type"};
        p.cell_type = *ct;
        p.states = j.value("states", uint16_t{2});
        if (j.contains("name")) p.name = j["name"].get<std::string>();
        if (j.contains("rule")) p.rule = j["rule"].get<std::string>();
        if (j.contains("comment")) p.comment = j["comment"].get<std::string>();

        auto cells = decodeCells(j.at("cells").at("encoding").get<std::string>(),
                                 j.at("cells").at("data").get<std::string>(), p.bytesNeeded());
        if (const auto* err = std::get_if<SessionError>(&cells)) return PatternError{err->message};
        p.cells = std::move(std::get<std::vector<uint8_t>>(cells));

        if (const auto bad = p.problems(); !bad.empty()) return PatternError{bad.front()};
        return p;
    } catch (const json::exception& ex) {
        return PatternError{std::format("malformed pattern: {}", ex.what())};
    }
}

// --- Either --------------------------------------------------------------------------

std::variant<Pattern, PatternError> parsePattern(std::string_view text) {
    // Sniffed rather than taken from the extension: a pattern fetched from
    // anywhere may be named anything, and the two formats are unmistakable.
    std::string_view head = text;
    while (!head.empty() && (head.front() == ' ' || head.front() == '\n' ||
                             head.front() == '\r' || head.front() == '\t')) {
        head.remove_prefix(1);
    }
    if (head.starts_with("{")) return parseNative(text);
    return parseRle(text);
}

std::variant<std::string, PatternError> writePattern(const Pattern& p, Format f) {
    if (f == Format::Rle) return writeRle(p);
    if (const auto bad = p.problems(); !bad.empty()) return PatternError{bad.front()};
    return writeNative(p);
}

// --- Patterns against a grid ------------------------------------------------

Lattice latticeFor(const rule::RuleIR& ir) {
    return ir.neighbourhood.type == rule::NeighbourhoodType::Hexagonal ? Lattice::Hexagonal
                                                                       : Lattice::Square;
}

std::optional<PatternError> patternFits(const Pattern& p, const rule::RuleIR& ir,
                                        const core::GridSpec& spec,
                                        uint32_t x, uint32_t y, uint32_t z) {
    if (const auto bad = p.problems(); !bad.empty()) return PatternError{"pattern: " + bad.front()};
    if (p.lattice != latticeFor(ir)) {
        return PatternError{std::format("pattern is {} but the grid is {}",
                                        toString(p.lattice), toString(latticeFor(ir)))};
    }
    if (p.cell_type != spec.cell_type) {
        return PatternError{std::format("pattern holds {} cells but the grid holds {}",
                                        core::toString(p.cell_type), core::toString(spec.cell_type))};
    }
    if (p.cell_type == core::CellType::U8 && p.states > ir.states) {
        return PatternError{std::format("pattern uses {} states but the rule has {}", p.states, ir.states)};
    }
    if (uint64_t{x} + p.width > spec.width || uint64_t{y} + p.height > spec.height ||
        uint64_t{z} + p.depth > spec.depth) {
        return PatternError{std::format("a {}x{}x{} pattern at ({},{},{}) hangs over the edge of a {}x{}x{} grid",
                                        p.width, p.height, p.depth, x, y, z,
                                        spec.width, spec.height, spec.depth)};
    }
    return std::nullopt;
}

namespace {

// Cell index within a buffer of this spec. The same arithmetic HostGrid::index
// does, reachable from a bare span.
size_t cellIndex(const core::GridSpec& spec, uint32_t x, uint32_t y, uint32_t z) {
    return (size_t{z} * spec.height + y) * spec.width + x;
}

}  // namespace

void blitPattern(const Pattern& p, const core::GridSpec& spec, std::span<uint8_t> cells,
                 uint32_t x, uint32_t y, uint32_t z) {
    assert(uint64_t{x} + p.width <= spec.width && uint64_t{y} + p.height <= spec.height &&
           uint64_t{z} + p.depth <= spec.depth);
    assert(p.cell_type == spec.cell_type);
    const uint32_t bytes = core::cellBytes(spec.cell_type);
    for (uint32_t pz = 0; pz < p.depth; ++pz) {
        for (uint32_t py = 0; py < p.height; ++py) {
            const size_t dst = cellIndex(spec, x, y + py, z + pz) * bytes;
            const size_t src = (size_t{pz} * p.height + py) * p.width * bytes;
            std::copy_n(p.cells.begin() + static_cast<std::ptrdiff_t>(src),
                        size_t{p.width} * bytes, cells.begin() + static_cast<std::ptrdiff_t>(dst));
        }
    }
}

std::variant<Pattern, PatternError> extractRegion(const rule::RuleIR& ir, const core::GridSpec& spec,
                                                  std::span<const uint8_t> cells,
                                                  uint32_t x, uint32_t y, uint32_t z,
                                                  uint32_t w, uint32_t h, uint32_t d) {
    if (w == 0 || h == 0 || d == 0) return PatternError{"a pattern needs a region of at least one cell"};
    if (uint64_t{x} + w > spec.width || uint64_t{y} + h > spec.height || uint64_t{z} + d > spec.depth) {
        return PatternError{"the region reaches outside the grid"};
    }

    Pattern p;
    p.dimensions = spec.dimensions;
    p.width = w; p.height = h; p.depth = d;
    p.lattice = latticeFor(ir);
    p.cell_type = spec.cell_type;
    p.rule = ir.metadata.source_notation.value_or(ir.metadata.name.value_or(std::string{}));
    if (p.rule->empty()) p.rule.reset();

    const uint32_t bytes = core::cellBytes(spec.cell_type);
    p.cells.resize(size_t{w} * h * d * bytes);
    for (uint32_t pz = 0; pz < d; ++pz) {
        for (uint32_t py = 0; py < h; ++py) {
            const size_t src = cellIndex(spec, x, y + py, z + pz) * bytes;
            const size_t dst = (size_t{pz} * h + py) * w * bytes;
            std::copy_n(cells.begin() + static_cast<std::ptrdiff_t>(src), size_t{w} * bytes,
                        p.cells.begin() + static_cast<std::ptrdiff_t>(dst));
        }
    }
    if (p.cell_type == core::CellType::U8) {
        uint16_t highest = 0;
        for (uint8_t c : p.cells) highest = std::max<uint16_t>(highest, c);
        p.states = static_cast<uint16_t>(std::max<uint16_t>(2, highest + 1));
    }
    if (const auto bad = p.problems(); !bad.empty()) return PatternError{"extracted pattern: " + bad.front()};
    return p;
}

}  // namespace aether::sim
