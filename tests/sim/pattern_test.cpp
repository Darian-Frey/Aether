// Pattern files (F-012, D-017, SPEC §14).

#include "sim/pattern.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <vector>

using namespace aether;
using sim::Format;
using sim::Lattice;
using sim::Pattern;

namespace {

Pattern ok(std::string_view text) {
    auto p = sim::parsePattern(text);
    if (const auto* e = std::get_if<sim::PatternError>(&p)) FAIL(e->message);
    return std::get<Pattern>(std::move(p));
}

std::string errorOf(std::string_view text) {
    auto p = sim::parsePattern(text);
    if (std::holds_alternative<Pattern>(p)) return "<parsed>";
    return std::get<sim::PatternError>(p).message;
}

std::string written(const Pattern& p, Format f) {
    auto t = sim::writePattern(p, f);
    if (const auto* e = std::get_if<sim::PatternError>(&t)) FAIL(e->message);
    return std::get<std::string>(std::move(t));
}

uint8_t at(const Pattern& p, uint32_t x, uint32_t y, uint32_t z = 0) {
    return p.cells[(size_t{z} * p.height + y) * p.width + x];
}

}  // namespace

TEST_CASE("a plain two-state RLE reads as every Life tool writes it", "[pattern]") {
    // The glider, exactly as it appears in a .rle from anywhere.
    const Pattern p = ok("#N Glider\n#C The smallest spaceship.\nx = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n");
    CHECK(p.width == 3);
    CHECK(p.height == 3);
    CHECK(p.states == 2);
    CHECK(p.lattice == Lattice::Square);
    CHECK(p.name == "Glider");
    CHECK(p.rule == "B3/S23");
    CHECK(p.comment == "The smallest spaceship.");

    CHECK(at(p, 1, 0) == 1);
    CHECK(at(p, 0, 0) == 0);
    CHECK(at(p, 2, 1) == 1);
    CHECK(at(p, 0, 2) == 1);
    CHECK(at(p, 1, 2) == 1);
    CHECK(at(p, 2, 2) == 1);
}

TEST_CASE("a glider round-trips through RLE unchanged", "[pattern]") {
    const Pattern p = ok("x = 3, y = 3, rule = B3/S23\nbob$2bo$3o!\n");
    const std::string text = written(p, Format::Rle);
    INFO(text);
    CHECK(text.find("x = 3, y = 3, rule = B3/S23") != std::string::npos);
    // Written back canonically: the input's trailing `b` on row 0 is a dead
    // cell the row break already implies, so it does not survive the trip.
    CHECK(text.find("bo$2bo$3o!") != std::string::npos);    // two-state uses b and o
    CHECK(ok(text) == p);
}

TEST_CASE("multi-state RLE uses the state alphabet both ways", "[pattern]") {
    // `.` is dead, A..X are 1..24, and a prefix letter carries the rest.
    const Pattern p = ok("x = 4, y = 1, rule = Wireworld\n.ABC!\n");
    CHECK(p.states == 4);
    CHECK(at(p, 0, 0) == 0);
    CHECK(at(p, 1, 0) == 1);
    CHECK(at(p, 2, 0) == 2);
    CHECK(at(p, 3, 0) == 3);
    const std::string text = written(p, Format::Rle);
    INFO(text);
    CHECK(text.find(".ABC!") != std::string::npos);
    CHECK(ok(text) == p);

    // Above 24 a prefix letter carries the rest: pA is 25, and the alphabet
    // stops at yO, which is 255 — `yX` would be 264 and is not a state.
    const Pattern high = ok("x = 3, y = 1, rule = test\nXpAyO!\n");
    CHECK(at(high, 0, 0) == 24);
    CHECK(at(high, 1, 0) == 25);
    CHECK(at(high, 2, 0) == 255);
    CHECK(high.states == 256);
    CHECK(errorOf("x = 1, y = 1, rule = test\nyX!\n").find("states must be in 2..256") != std::string::npos);
    CHECK(ok(written(high, Format::Rle)) == high);
}

TEST_CASE("RLE rows pad and repeat the way the format says", "[pattern]") {
    // A short row is dead cells to the end, and `3$` is three row breaks:
    // it ends row 0 and skips rows 1 and 2, landing the rest on row 3.
    const Pattern p = ok("x = 4, y = 4, rule = B3/S23\no3$2o!\n");
    CHECK(at(p, 0, 0) == 1);
    CHECK(at(p, 3, 0) == 0);
    CHECK(at(p, 0, 1) == 0);
    CHECK(at(p, 0, 2) == 0);
    CHECK(at(p, 0, 3) == 1);
    CHECK(at(p, 1, 3) == 1);
    CHECK(ok(written(p, Format::Rle)) == p);
}

TEST_CASE("the native format carries what RLE cannot", "[pattern]") {
    SECTION("a hexagonal pattern") {
        Pattern p;
        p.lattice = Lattice::Hexagonal;
        p.width = 3; p.height = 2;
        p.states = 3;
        p.cells = {0, 1, 2, 2, 1, 0};
        p.name = "hex thing";
        REQUIRE(p.problems().empty());
        CHECK(sim::formatFor(p) == Format::Native);
        CHECK(ok(written(p, Format::Native)) == p);
    }
    SECTION("a 3D pattern") {
        Pattern p;
        p.dimensions = 3;
        p.width = 2; p.height = 2; p.depth = 2;
        p.states = 2;
        p.cells = {1, 0, 0, 1, 0, 1, 1, 0};
        REQUIRE(p.problems().empty());
        CHECK(sim::formatFor(p) == Format::Native);
        const Pattern back = ok(written(p, Format::Native));
        CHECK(back == p);
        CHECK(at(back, 0, 0, 0) == 1);
        CHECK(at(back, 1, 1, 1) == 0);
    }
    SECTION("a continuous pattern") {
        Pattern p;
        p.cell_type = core::CellType::F32;
        p.width = 2; p.height = 1;
        p.cells.resize(8);
        const float vs[2] = {0.25f, 0.75f};
        std::memcpy(p.cells.data(), vs, sizeof(vs));
        REQUIRE(p.problems().empty());
        CHECK(sim::formatFor(p) == Format::Native);
        CHECK(ok(written(p, Format::Native)) == p);
    }
}

TEST_CASE("the format follows the pattern, not the caller", "[pattern]") {
    Pattern square;
    square.width = 2; square.height = 2;
    square.cells = {1, 0, 0, 1};
    CHECK(sim::formatFor(square) == Format::Rle);
    CHECK(sim::extensionFor(Format::Rle) == "rle");
    CHECK(sim::extensionFor(Format::Native) == "pattern");

    // Asked for RLE anyway, a pattern RLE cannot carry says so rather than
    // dropping the part that does not fit.
    Pattern hex = square;
    hex.lattice = Lattice::Hexagonal;
    auto refused = sim::writePattern(hex, Format::Rle);
    REQUIRE(std::holds_alternative<sim::PatternError>(refused));
    CHECK(std::get<sim::PatternError>(refused).message.find("hexagonal") != std::string::npos);
}

TEST_CASE("a pattern's path follows its name and its format", "[pattern]") {
    // A bare name goes where the library looks; the extension comes from the
    // format, which came from the pattern.
    CHECK(sim::pathFor("glider", Format::Rle) == "patterns/glider.rle");
    CHECK(sim::pathFor("spiral", Format::Native) == "patterns/spiral.pattern");
    CHECK(sim::pathFor("glider.rle", Format::Rle) == "patterns/glider.rle");      // not doubled
    CHECK(sim::pathFor("/tmp/thing", Format::Rle) == "/tmp/thing.rle");           // a path is a path
    CHECK(sim::pathFor("sub/dir/x.pattern", Format::Native) == "sub/dir/x.pattern");
    CHECK(sim::pathFor("", Format::Rle) == "patterns/pattern.rle");               // something rather than nothing
    CHECK(sim::pathFor("x", Format::Rle, "") == "x.rle");
    // A name ending in the *other* format's extension still gains its own:
    // the format decides, and a .rle holding native JSON would be a lie.
    CHECK(sim::pathFor("thing.rle", Format::Native) == "patterns/thing.rle.pattern");
}

TEST_CASE("a malformed pattern is refused rather than half-read", "[pattern]") {
    CHECK(errorOf("bob$2bo$3o!").find("no 'x = ") != std::string::npos);        // no header
    CHECK(errorOf("x = 3, y = 3\nbob$2bo$3oZ!").find("unexpected character") != std::string::npos);
    CHECK(errorOf("x = 2, y = 1\nooo!").find("longer than its header") != std::string::npos);
    CHECK(errorOf("x = 1, y = 1\no$$$o!").find("more rows than its header") != std::string::npos);
    CHECK(errorOf("x = q, y = 1\no!").find("bad x") != std::string::npos);
    CHECK(errorOf("{\"format\": \"something-else\"}").find("not an Aether pattern") != std::string::npos);
    CHECK(errorOf("{\"format\": \"aether-pattern\", \"version\": 9}").find("version 9") != std::string::npos);
    CHECK(errorOf("{\"format\": \"aether-pattern\", \"version\": 1}").find("malformed") != std::string::npos);
}

TEST_CASE("parsePattern picks the format from the content", "[pattern]") {
    Pattern p;
    p.width = 2; p.height = 1;
    p.cells = {1, 0};
    // Native is JSON and RLE is not, so neither can be mistaken for the other
    // however the file happens to be named.
    CHECK(ok(sim::writeNative(p)).cells == p.cells);
    CHECK(ok(written(p, Format::Rle)).cells == p.cells);
    CHECK(ok("\n\n  {\"format\":\"aether-pattern\",\"version\":1,\"extent\":{\"dimensions\":2,\"w\":1,\"h\":1},"
             "\"states\":2,\"cells\":{\"encoding\":\"bytes\",\"data\":\"AQ==\"}}").cells == std::vector<uint8_t>{1});
}

// --- Placement into a running grid (F-012) ---------------------------------

#include "core/gpu_grid.hpp"
#include "rule/dsl.hpp"
#include "sim/simulation.hpp"
#include "support/gl_context.hpp"

namespace {

Pattern glider() {
    auto p = sim::parsePattern("x = 3, y = 3, rule = B3/S23\nbo$2bo$3o!\n");
    return std::get<Pattern>(std::move(p));
}

rule::RuleIR life() {
    auto r = rule::parseDsl("B3/S23");
    REQUIRE(r);
    return *r.ir;
}

sim::Simulation freshGrid(uint32_t w = 16, uint32_t h = 16) {
    auto made = sim::Simulation::create(core::GridSpec{2, w, h, 1}, life(), sim::Path::Cpu, 1, 2);
    if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
    return std::get<sim::Simulation>(std::move(made));
}

}  // namespace

TEST_CASE("a pattern lands where it is put, on both copies of the grid", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    sim::Simulation s = freshGrid();
    REQUIRE_FALSE(s.placePattern(glider(), 5, 4, 0).has_value());

    // The glider's own (0,0) is at (5,4), so its cells are offset by that.
    CHECK(s.host().get(6, 4) == 1);
    CHECK(s.host().get(5, 4) == 0);
    CHECK(s.host().get(7, 5) == 1);
    CHECK(s.host().get(5, 6) == 1);
    CHECK(s.host().get(6, 6) == 1);
    CHECK(s.host().get(7, 6) == 1);
    CHECK(s.host().get(0, 0) == 0);          // nothing else touched

    // And the same cells come back out, which exercises the round trip rather
    // than only the write.
    const auto extracted = s.extractPattern(5, 4, 0, 3, 3, 1);
    REQUIRE(std::holds_alternative<Pattern>(extracted));
    CHECK(std::get<Pattern>(extracted).cells == glider().cells);
}

TEST_CASE("a pattern that does not belong is refused, not coerced", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);
    sim::Simulation s = freshGrid(8, 8);

    auto over = s.placePattern(glider(), 6, 6, 0);
    REQUIRE(over.has_value());
    CHECK(over->message.find("hangs over the edge") != std::string::npos);

    Pattern hex = glider();
    hex.lattice = Lattice::Hexagonal;
    auto wrongLattice = s.placePattern(hex, 0, 0, 0);
    REQUIRE(wrongLattice.has_value());
    CHECK(wrongLattice->message.find("hexagonal") != std::string::npos);

    Pattern manyStates = glider();
    manyStates.states = 8;
    manyStates.cells[0] = 7;
    auto tooManyStates = s.placePattern(manyStates, 0, 0, 0);
    REQUIRE(tooManyStates.has_value());
    CHECK(tooManyStates->message.find("8 states but the rule has 2") != std::string::npos);

    // Refused means untouched: not one cell of a rejected paste lands.
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) CHECK(s.host().get(x, y) == 0);
    }

    // canPlace answers the same question without doing anything, which is
    // what lets the interface show a refusal before the click (BUG-013).
    CHECK(s.canPlace(glider(), 6, 6, 0).has_value());
    CHECK(s.canPlace(hex, 0, 0, 0)->message == wrongLattice->message);
    CHECK(s.canPlace(manyStates, 0, 0, 0)->message == tooManyStates->message);
    CHECK_FALSE(s.canPlace(glider(), 0, 0, 0).has_value());
}

TEST_CASE("a placed pattern replays from the journal (D-013)", "[gpu][pattern][replay]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    sim::Simulation s = freshGrid(24, 24);
    for (int i = 0; i < 5; ++i) s.step();
    REQUIRE_FALSE(s.placePattern(glider(), 3, 3, 0).has_value());
    for (int i = 0; i < 20; ++i) s.step();
    const sim::Session snap = s.session();
    CHECK(snap.journal.size() == 1);          // the paste, and nothing else

    // Through the file, so the event's serialisation is under test too.
    const std::string text = sim::sessionToJson(snap);
    auto parsed = sim::sessionFromJson(text);
    if (const auto* e = std::get_if<sim::SessionError>(&parsed)) FAIL(e->message);

    auto replayed = sim::Simulation::replay(std::get<sim::Session>(parsed), {snap.generation}, sim::Path::Cpu);
    if (const auto* e = std::get_if<core::Error>(&replayed)) FAIL(e->message);
    auto& r = std::get<sim::Simulation>(replayed);
    CHECK(r.generation() == snap.generation);
    CHECK(std::vector<uint8_t>(r.host().current().begin(), r.host().current().end()) == snap.current);
}

TEST_CASE("a region of the grid comes back out as a pattern", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);
    sim::Simulation s = freshGrid();
    REQUIRE_FALSE(s.placePattern(glider(), 2, 2, 0).has_value());

    auto got = s.extractPattern(2, 2, 0, 3, 3, 1);
    REQUIRE(std::holds_alternative<Pattern>(got));
    const Pattern& p = std::get<Pattern>(got);
    CHECK(p.width == 3);
    CHECK(p.height == 3);
    CHECK(p.states == 2);                     // what the cells use, not the rule's count
    CHECK(p.rule == "B3/S23");                // the rule travels as a hint
    CHECK(p.cells == glider().cells);

    // Round-trip: written out, read back, placed again, same grid.
    auto text = sim::writePattern(p, sim::formatFor(p));
    REQUIRE(std::holds_alternative<std::string>(text));
    const Pattern back = ok(std::get<std::string>(text));
    sim::Simulation other = freshGrid();
    REQUIRE_FALSE(other.placePattern(back, 2, 2, 0).has_value());
    CHECK(std::vector<uint8_t>(other.host().current().begin(), other.host().current().end()) ==
          std::vector<uint8_t>(s.host().current().begin(), s.host().current().end()));

    CHECK(std::holds_alternative<core::Error>(s.extractPattern(14, 14, 0, 8, 8, 1)));
    CHECK(std::holds_alternative<core::Error>(s.extractPattern(0, 0, 0, 0, 1, 1)));
}

TEST_CASE("a region goes out to a file and comes back the same", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    // The composition the Patterns panel performs: extract, choose the format
    // from the pattern, write it where pathFor says, read it back, place it.
    // The pieces are each tested above; this is the chain they make.
    sim::Simulation s = freshGrid();
    REQUIRE_FALSE(s.placePattern(glider(), 4, 4, 0).has_value());

    auto got = s.extractPattern(4, 4, 0, 3, 3, 1);
    REQUIRE(std::holds_alternative<Pattern>(got));
    Pattern p = std::get<Pattern>(std::move(got));
    p.name = "test glider";

    const sim::Format f = sim::formatFor(p);
    const std::string path = sim::pathFor("aether_test_glider", f, std::filesystem::temp_directory_path().string());
    auto text = sim::writePattern(p, f);
    REQUIRE(std::holds_alternative<std::string>(text));
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out << std::get<std::string>(text);
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    const Pattern back = ok(ss.str());
    CHECK(back.cells == p.cells);
    CHECK(back.name == "test glider");
    CHECK(back.rule == "B3/S23");

    sim::Simulation other = freshGrid();
    REQUIRE_FALSE(other.placePattern(back, 4, 4, 0).has_value());
    CHECK(std::vector<uint8_t>(other.host().current().begin(), other.host().current().end()) ==
          std::vector<uint8_t>(s.host().current().begin(), s.host().current().end()));
    std::filesystem::remove(path);
}

// --- Region seeding (F-028) ------------------------------------------------

TEST_CASE("seeding a region leaves the rest of the grid alone", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    sim::Simulation s = freshGrid(32, 32);
    const std::vector<double> density{1.0};        // every cell in the box becomes state 1
    s.fillRegion(8, 4, 0, 6, 5, 1, density);

    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const bool inside = x >= 8 && x < 14 && y >= 4 && y < 9;
            INFO("cell " << x << "," << y);
            CHECK(s.host().get(x, y) == (inside ? 1 : 0));
        }
    }
}

TEST_CASE("a region fill is the whole-grid fill over a smaller box", "[gpu][pattern]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    // Same seed, same densities: filling the whole grid and filling a box
    // that happens to be the whole grid must draw from stream A identically,
    // or every session written before F-028 replays differently.
    const std::vector<double> density{0.3, 0.2};
    sim::Simulation a = freshGrid(16, 16);
    a.fillRandom(density);
    sim::Simulation b = freshGrid(16, 16);
    b.fillRegion(0, 0, 0, 16, 16, 1, density);
    CHECK(std::vector<uint8_t>(a.host().current().begin(), a.host().current().end()) ==
          std::vector<uint8_t>(b.host().current().begin(), b.host().current().end()));
}

TEST_CASE("a seeded region replays from the journal", "[gpu][pattern][replay]") {
    aether::test::GlContext gl;
    aether::test::requireGl(gl);

    sim::Simulation s = freshGrid(24, 24);
    for (int i = 0; i < 4; ++i) s.step();
    s.fillRegion(3, 3, 0, 8, 8, 1, std::vector<double>{0.5});
    for (int i = 0; i < 15; ++i) s.step();
    const sim::Session snap = s.session();
    CHECK(snap.journal.size() == 1);

    const std::string text = sim::sessionToJson(snap);
    auto parsed = sim::sessionFromJson(text);
    if (const auto* e = std::get_if<sim::SessionError>(&parsed)) FAIL(e->message);
    auto replayed = sim::Simulation::replay(std::get<sim::Session>(parsed), {snap.generation}, sim::Path::Cpu);
    if (const auto* e = std::get_if<core::Error>(&replayed)) FAIL(e->message);
    CHECK(std::vector<uint8_t>(std::get<sim::Simulation>(replayed).host().current().begin(),
                               std::get<sim::Simulation>(replayed).host().current().end()) == snap.current);
}
