// Pattern files (F-012, D-017, SPEC §14).

#include "sim/pattern.hpp"

#include <catch2/catch_test_macros.hpp>

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
