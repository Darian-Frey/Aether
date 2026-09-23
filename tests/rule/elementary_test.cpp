// Wolfram's elementary rules (F-005).
//
// The numbering is the whole of the feature: bit i of the rule number answers
// the neighbourhood whose (left, centre, right) read as binary is i. A rule
// that ran but meant something other than its number would look perfectly
// plausible, so these check the transitions rather than that it compiled.

#include "rule/dsl.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using namespace aether;

namespace {

rule::RuleIR ir(const std::string& text) {
    rule::DslContext ctx;
    ctx.dimensions = 1;
    auto r = rule::parseDsl(text, ctx);
    REQUIRE(r);
    return *r.ir;
}

// What the rule does to every one of the eight neighbourhoods, read out of
// the compiled table through the layout rather than by index arithmetic here.
std::array<uint8_t, 8> outputs(const rule::RuleIR& in) {
    auto c = rule::compileRule(in);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(c));
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(c));
    std::array<uint8_t, 8> out{};
    for (uint8_t left = 0; left < 2; ++left) {
        for (uint8_t centre = 0; centre < 2; ++centre) {
            for (uint8_t right = 0; right < 2; ++right) {
                const uint8_t nbr[2] = {left, right};
                const uint64_t at = rule.layout.indexNonTotalistic(centre, nbr);
                out[static_cast<size_t>(left * 4 + centre * 2 + right)] = rule.table[at];
            }
        }
    }
    return out;
}

// One generation of a 1D row under a rule, through the oracle.
std::string step(const std::string& text, const std::string& row, rule::Boundary b = rule::Boundary::Wrap) {
    rule::RuleIR in = ir(text);
    in.boundary = b;
    auto c = rule::compileRule(in);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(c));
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(c));

    const core::GridSpec spec{1, static_cast<uint32_t>(row.size()), 1, 1};
    core::HostGrid g(spec);
    for (size_t i = 0; i < row.size(); ++i) g.set(static_cast<uint32_t>(i), 0, 0, row[i] == '#' ? 1 : 0);
    sim::cpuStep(rule, g);
    std::string out(row.size(), '.');
    for (size_t i = 0; i < row.size(); ++i) out[i] = g.get(static_cast<uint32_t>(i)) ? '#' : '.';
    return out;
}

}  // namespace

TEST_CASE("an elementary rule is one-dimensional whatever the session is", "[elementary]") {
    rule::DslContext ctx;
    ctx.dimensions = 2;                       // the session says two
    auto r = rule::parseDsl("W110", ctx);
    REQUIRE(r);
    CHECK(r.ir->dimensions == 1);             // the rule says one
    CHECK(r.ir->states == 2);
    CHECK(r.ir->kind == rule::Kind::NonTotalistic);
    CHECK(r.ir->metadata.source_notation == "W110");
}

TEST_CASE("the rule number's bits are the eight transitions", "[elementary]") {
    // Rule 110: 01101110 read from bit 7 down, so bits 1,2,3,5,6 are set.
    CHECK(outputs(ir("W110")) == std::array<uint8_t, 8>{0, 1, 1, 1, 0, 1, 1, 0});
    // Rule 30: 00011110.
    CHECK(outputs(ir("W30")) == std::array<uint8_t, 8>{0, 1, 1, 1, 1, 0, 0, 0});
    // Rule 90, the Sierpinski rule: left XOR right, ignoring the centre.
    CHECK(outputs(ir("W90")) == std::array<uint8_t, 8>{0, 1, 0, 1, 1, 0, 1, 0});
    // The extremes.
    CHECK(outputs(ir("W0")) == std::array<uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(outputs(ir("W255")) == std::array<uint8_t, 8>{1, 1, 1, 1, 1, 1, 1, 1});
}

TEST_CASE("rule 90 is left xor right, which makes Sierpinski's triangle", "[elementary]") {
    // A single cell under rule 90 doubles outwards: the classic construction.
    CHECK(step("W90", ".....#.....") == "....#.#....");
    CHECK(step("W90", "....#.#....") == "...#...#...");
    CHECK(step("W90", "...#...#...") == "..#.#.#.#..");
}

TEST_CASE("rule 110 moves structure leftwards as it is known to", "[elementary]") {
    // Rule 110's characteristic behaviour on a lone cell: it grows to the
    // left. Two generations, checked against the transitions by hand.
    CHECK(step("W110", ".......#...") == "......##...");
    CHECK(step("W110", "......##...") == ".....###...");
}

TEST_CASE("rule 0 empties the row and rule 255 fills it", "[elementary]") {
    CHECK(step("W0", "..##.#..") == "........");
    CHECK(step("W255", "..##.#..") == "########");
}

TEST_CASE("rule 204 is the identity, so nothing moves", "[elementary]") {
    // 204 = 11001100: the output is the centre cell whatever the neighbours.
    CHECK(step("W204", "..#.##..#.") == "..#.##..#.");
}

TEST_CASE("rule 170 shifts the row one cell left", "[elementary]") {
    // 170 = 10101010: the output is the right neighbour.
    CHECK(step("W170", ".#..##....") == "#..##.....");
}

TEST_CASE("a number outside 0 to 255 is refused", "[elementary]") {
    rule::DslContext ctx;
    ctx.dimensions = 1;
    auto r = rule::parseDsl("W256", ctx);
    CHECK_FALSE(r);
    REQUIRE(r.error);
    CHECK(r.error->message.find("0 to 255") != std::string::npos);

    CHECK_FALSE(rule::parseDsl("W9999", ctx));
}

TEST_CASE("something that merely starts with w is left to the other notations", "[elementary]") {
    rule::DslContext ctx;
    ctx.dimensions = 1;
    // Not an elementary rule, and not valid in any other notation either —
    // what matters is that the error is the table parser's, not a claim that
    // this was a malformed rule number.
    auto r = rule::parseDsl("wobble", ctx);
    CHECK_FALSE(r);
    REQUIRE(r.error);
    CHECK(r.error->message.find("0 to 255") == std::string::npos);
}
