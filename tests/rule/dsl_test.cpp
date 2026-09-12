#include "rule/dsl.hpp"
#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

namespace {

// Reads an outer-totalistic table entry for a binary rule.
uint8_t binaryEntry(const RuleIR& ir, uint8_t own, uint32_t k) {
    const TableLayout L(ir.kind, ir.states, neighbourCount(ir.dimensions, ir.neighbourhood));
    const uint32_t counts[1] = {k};
    return std::get<Table>(ir.transition).entries[L.indexOuterTotalistic(own, counts)];
}

// Reads an entry for a multi-state rule given the full count vector.
uint8_t entry(const RuleIR& ir, uint8_t own, std::vector<uint32_t> counts) {
    const TableLayout L(ir.kind, ir.states, neighbourCount(ir.dimensions, ir.neighbourhood));
    return std::get<Table>(ir.transition).entries[L.indexOuterTotalistic(own, counts)];
}

}  // namespace

TEST_CASE("B3/S23 is Conway's Life", "[dsl]") {
    const auto r = parseDsl("B3/S23");
    REQUIRE(r);
    const RuleIR& ir = *r.ir;
    CHECK(ir.dimensions == 2);
    CHECK(ir.states == 2);
    CHECK(ir.kind == Kind::OuterTotalistic);
    CHECK(ir.neighbourhood == Neighbourhood{NeighbourhoodType::Moore, 1});
    CHECK(ir.boundary == Boundary::Wrap);
    CHECK(ir.metadata.source_notation == "B3/S23");
    REQUIRE(std::get<Table>(ir.transition).entries.size() == 18);
    for (uint32_t k = 0; k <= 8; ++k) {
        CHECK(binaryEntry(ir, 0, k) == (k == 3 ? 1 : 0));
        CHECK(binaryEntry(ir, 1, k) == ((k == 2 || k == 3) ? 1 : 0));
    }
}

TEST_CASE("B/S accepts empty digit lists, whitespace and lower case", "[dsl]") {
    CHECK(parseDsl("B/S"));
    CHECK(parseDsl("  B3/S23\n"));
    CHECK(parseDsl("b36/s23"));
    const auto r = parseDsl("B/S");
    for (uint32_t k = 0; k <= 8; ++k) {
        CHECK(binaryEntry(*r.ir, 0, k) == 0);
        CHECK(binaryEntry(*r.ir, 1, k) == 0);
    }
}

TEST_CASE("B/S follows the session's dimensionality and boundary", "[dsl]") {
    DslContext ctx;
    ctx.dimensions = 3;
    ctx.boundary = Boundary::Mirror;
    const auto r = parseDsl("B5/S45", ctx);
    REQUIRE(r);
    CHECK(r.ir->dimensions == 3);
    CHECK(r.ir->boundary == Boundary::Mirror);
    CHECK(std::get<Table>(r.ir->transition).entries.size() == 2 * 27);
    CHECK(binaryEntry(*r.ir, 0, 5) == 1);
    CHECK(binaryEntry(*r.ir, 1, 4) == 1);

    ctx.dimensions = 1;
    const auto one = parseDsl("B1/S1", ctx);
    REQUIRE(one);
    CHECK(std::get<Table>(one.ir->transition).entries.size() == 2 * 3);

    const auto bad = parseDsl("B3/S23", ctx);   // count 3 in a 2-neighbour world
    REQUIRE_FALSE(bad);
    CHECK(bad.error->column == 2);
    CHECK(bad.error->message.find("exceeds the neighbourhood size 2") != std::string::npos);
}

TEST_CASE("B/S syntax errors carry a column", "[dsl]") {
    auto r = parseDsl("B3S23");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 1);
    CHECK(r.error->column == 3);

    r = parseDsl("B3/X23");
    REQUIRE_FALSE(r);
    CHECK(r.error->column == 4);

    r = parseDsl("B3/S23/C");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("state count") != std::string::npos);

    r = parseDsl("B3/S23/C1");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("2..256") != std::string::npos);

    r = parseDsl("B3/S23x");
    REQUIRE_FALSE(r);
    CHECK(r.error->column == 7);
}

TEST_CASE("B2/S/C3 is Brian's Brain", "[dsl]") {
    const auto r = parseDsl("B2/S/C3");
    REQUIRE(r);
    const RuleIR& ir = *r.ir;
    CHECK(ir.states == 3);
    CHECK(ir.kind == Kind::OuterTotalistic);
    REQUIRE(std::get<Table>(ir.transition).entries.size() == 3 * 45);

    // Dead cell with exactly two firing neighbours is born, regardless of
    // how many refractory neighbours there are.
    CHECK(entry(ir, 0, {2, 0}) == 1);
    CHECK(entry(ir, 0, {2, 5}) == 1);
    CHECK(entry(ir, 0, {3, 0}) == 0);
    // Firing cells always become refractory (empty S list).
    CHECK(entry(ir, 1, {2, 0}) == 2);
    CHECK(entry(ir, 1, {0, 0}) == 2);
    // Refractory cells always die.
    CHECK(entry(ir, 2, {0, 0}) == 0);
    CHECK(entry(ir, 2, {8, 0}) == 0);
}

TEST_CASE("Generations refractory chain advances and wraps to zero", "[dsl]") {
    const auto r = parseDsl("B2/S23/C5");
    REQUIRE(r);
    const RuleIR& ir = *r.ir;
    CHECK(entry(ir, 1, {2, 0, 0, 0}) == 1);   // survives
    CHECK(entry(ir, 1, {4, 0, 0, 0}) == 2);   // starts dying
    CHECK(entry(ir, 2, {0, 0, 0, 0}) == 3);
    CHECK(entry(ir, 3, {0, 0, 0, 0}) == 4);
    CHECK(entry(ir, 4, {0, 0, 0, 0}) == 0);
}

TEST_CASE("B/S/C2 is identical to plain B/S", "[dsl]") {
    const auto a = parseDsl("B3/S23");
    const auto b = parseDsl("B3/S23/C2");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(irHash(*a.ir) == irHash(*b.ir));
}

TEST_CASE("a large Generations rule lowers to an expression", "[dsl]") {
    // 25 states: table would be 25 * C(32,24) entries, far over the threshold.
    const auto r = parseDsl("B2/S/C25");
    REQUIRE(r);
    CHECK(r.ir->kind == Kind::Expression);
    CHECK(std::holds_alternative<Expression>(r.ir->transition));
    CHECK(isValid(*r.ir));
}

TEST_CASE("table block: Life written longhand equals B3/S23", "[dsl]") {
    const char* src = R"(
        states 2;
        neighbourhood moore 1;
        0: n(1) == 3 -> 1;
        1: n(1) < 2 -> 0;
        1: n(1) > 3 -> 0;
    )";
    const auto block = parseDsl(src);
    REQUIRE(block);
    const auto bs = parseDsl("B3/S23");
    REQUIRE(bs);
    CHECK(std::get<Table>(block.ir->transition) == std::get<Table>(bs.ir->transition));
    CHECK(irHash(*block.ir) == irHash(*bs.ir));
}

TEST_CASE("table block: first matching statement wins, no match retains", "[dsl]") {
    const char* src = R"(
        states 3;
        neighbourhood von_neumann 1;
        0: n(1) >= 1 -> 1;
        0: n(1) >= 2 -> 2;   # shadowed by the line above
        1: n(2) == 0 and n(1) == 0 -> 0;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(r.ir->neighbourhood == Neighbourhood{NeighbourhoodType::VonNeumann, 1});
    CHECK(std::get<Table>(r.ir->transition).entries.size() == 3 * 15);
    CHECK(entry(*r.ir, 0, {2, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 3}) == 0);   // retains
    CHECK(entry(*r.ir, 1, {0, 0}) == 0);
    CHECK(entry(*r.ir, 1, {1, 0}) == 1);   // retains
    CHECK(entry(*r.ir, 2, {0, 0}) == 2);   // no statements for state 2
}

TEST_CASE("table block: n(0) counts quiescent neighbours; and binds tighter than or", "[dsl]") {
    const char* src = R"(
        states 3;
        neighbourhood moore 1;
        0: n(0) == 8 or n(1) == 1 and n(2) == 1 -> 2;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(entry(*r.ir, 0, {0, 0}) == 2);   // n(0) == 8
    CHECK(entry(*r.ir, 0, {1, 1}) == 2);   // n(1)==1 and n(2)==1
    CHECK(entry(*r.ir, 0, {1, 0}) == 0);   // neither
    CHECK(entry(*r.ir, 0, {0, 1}) == 0);
}

TEST_CASE("table block: header boundary overrides the session's", "[dsl]") {
    const char* src = "states 2; neighbourhood moore 1; boundary zero;";
    DslContext ctx;
    ctx.boundary = Boundary::Wrap;
    const auto r = parseDsl(src, ctx);
    REQUIRE(r);
    CHECK(r.ir->boundary == Boundary::Zero);
    const auto d = parseDsl("states 2; neighbourhood moore 1;", ctx);
    REQUIRE(d);
    CHECK(d.ir->boundary == Boundary::Wrap);
}

TEST_CASE("table block: Wireworld", "[dsl]") {
    // 0 empty, 1 head, 2 tail, 3 wire.
    const char* src = R"(
        states 4;
        neighbourhood moore 1;
        1: n(0) >= 0 -> 2;
        2: n(0) >= 0 -> 3;
        3: n(1) == 1 or n(1) == 2 -> 1;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(entry(*r.ir, 1, {0, 0, 0}) == 2);
    CHECK(entry(*r.ir, 2, {0, 0, 0}) == 3);
    CHECK(entry(*r.ir, 3, {1, 0, 4}) == 1);
    CHECK(entry(*r.ir, 3, {2, 1, 1}) == 1);
    CHECK(entry(*r.ir, 3, {3, 0, 0}) == 3);
    CHECK(entry(*r.ir, 0, {8, 0, 0}) == 0);
}

TEST_CASE("table block: errors report line and column", "[dsl]") {
    auto r = parseDsl("states 2;\nneighbourhood moore 1;\n0: n(1) = 3 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 3);
    CHECK(r.error->column == 9);

    r = parseDsl("states 2;\nneighbourhood penrose 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 2);
    CHECK(r.error->column == 15);
    CHECK(r.error->message.find("penrose") != std::string::npos);

    r = parseDsl("states 2;\nneighbourhood moore 1;\n0: n(1) == 3 -> 2;");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 3);
    CHECK(r.error->message.find("state must be in 0..1") != std::string::npos);

    r = parseDsl("states 2;\nneighbourhood moore 1;\n0: n(5) == 3 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->column == 6);

    r = parseDsl("states 300; neighbourhood moore 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("2..256") != std::string::npos);

    r = parseDsl("states 2; neighbourhood moore 1; 0: n(1) == 3 -> 1");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("';'") != std::string::npos);

    r = parseDsl("states 2; neighbourhood moore 1; 0: n(1) == 3 -> 1; $");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("unexpected character") != std::string::npos);
}

TEST_CASE("table block: too large for a table lowers to an expression", "[dsl]") {
    // 16 states, Moore r=1: 16 * C(23,15) = 7.8M entries.
    const char* src = R"(
        states 16;
        neighbourhood moore 1;
        0: n(1) == 3 -> 1;
        1: n(1) < 2 or n(1) > 3 -> 2;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(r.ir->kind == Kind::Expression);
    CHECK(isValid(*r.ir));
}

TEST_CASE("empty input and nonsense are rejected without an IR", "[dsl]") {
    CHECK_FALSE(parseDsl(""));
    CHECK_FALSE(parseDsl("   \n"));
    CHECK_FALSE(parseDsl("hello"));
    DslContext ctx;
    ctx.dimensions = 4;
    CHECK_FALSE(parseDsl("B3/S23", ctx));
}

TEST_CASE("table block: hexagonal neighbourhood", "[dsl][hex]") {
    const auto r = parseDsl("states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(r);
    CHECK(r.ir->neighbourhood == Neighbourhood{NeighbourhoodType::Hexagonal, 1});
    CHECK(std::get<Table>(r.ir->transition).entries.size() == 2 * 7);
    CHECK(binaryEntry(*r.ir, 0, 2) == 1);
    CHECK(binaryEntry(*r.ir, 1, 3) == 1);
    CHECK(binaryEntry(*r.ir, 1, 4) == 0);
    const auto h2 = parseDsl("states 3; neighbourhood hexagonal 2;");
    REQUIRE(h2);
    CHECK(neighbourCount(2, h2.ir->neighbourhood) == 18);
    DslContext ctx;
    ctx.dimensions = 3;
    CHECK_FALSE(parseDsl("states 2; neighbourhood hex 1;", ctx));
}
