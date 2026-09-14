#include "rule/decay.hpp"
#include "rule/dsl.hpp"
#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

namespace {

uint8_t entry(const RuleIR& ir, uint8_t own, std::vector<uint32_t> counts) {
    const TableLayout L(ir.kind, ir.states, neighbourCount(ir.dimensions, ir.neighbourhood));
    counts.resize(ir.states - 1u, 0);
    return std::get<Table>(ir.transition).entries[L.indexOuterTotalistic(own, counts)];
}

}  // namespace

TEST_CASE("a decay tail written longhand equals the Generations shorthand", "[decay]") {
    const char* src = R"(
        states 2;
        neighbourhood moore 1;
        decay 1;
        0: n(1) == 2 -> 1;
        1: n(1) >= 0 -> 0;
    )";
    const auto block = parseDsl(src);
    REQUIRE(block);
    const auto brain = parseDsl("B2/S/C3");
    REQUIRE(brain);
    CHECK(block.ir->states == 3);
    CHECK(std::get<Table>(block.ir->transition) == std::get<Table>(brain.ir->transition));
    CHECK(block.ir->metadata.decay_from == 2);
}

TEST_CASE("decay appends states and routes every death through the tail", "[decay]") {
    const auto r = parseDsl("states 2; neighbourhood moore 1; decay 3; 0: n(1) == 3 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(r);
    const RuleIR& ir = *r.ir;
    CHECK(ir.states == 5);            // 2 base + 3 tail
    CHECK(ir.metadata.decay_from == 2);
    CHECK(ir.kind == Kind::OuterTotalistic);

    CHECK(entry(ir, 1, {1}) == 2);    // lonely live cell enters the tail, not 0
    CHECK(entry(ir, 1, {2}) == 1);    // survives as before
    CHECK(entry(ir, 0, {3}) == 1);    // born as before
    CHECK(entry(ir, 0, {1}) == 0);    // a dead cell stays dead; it does not enter the tail
    for (uint32_t k = 0; k <= 8; ++k) {
        CHECK(entry(ir, 2, {k}) == 3);   // the tail advances whatever the neighbours do
        CHECK(entry(ir, 3, {k}) == 4);
        CHECK(entry(ir, 4, {k}) == 0);
    }
}

TEST_CASE("tail states count as quiescent, so a fading cell feeds no birth", "[decay]") {
    const auto r = parseDsl("states 2; neighbourhood moore 1; decay 2; 0: n(1) == 3 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(r);
    const RuleIR& ir = *r.ir;
    CHECK(entry(ir, 0, {3, 0, 0}) == 1);   // three live neighbours: born
    CHECK(entry(ir, 0, {0, 3, 0}) == 0);   // three fading neighbours: nothing
    CHECK(entry(ir, 0, {2, 1, 0}) == 0);   // two live and one fading: still nothing
    CHECK(entry(ir, 1, {2, 4, 0}) == 1);   // survival counts live neighbours only
    CHECK(entry(ir, 1, {1, 4, 0}) == 2);
}

TEST_CASE("decay on a multi-state rule keeps the rule's own states distinct", "[decay]") {
    const char* src = R"(
        states 3;
        neighbourhood von_neumann 1;
        decay 2;
        0: n(1) >= 1 -> 1;
        1: n(2) >= 1 -> 2;
        2: n(1) >= 1 -> 0;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(r.ir->states == 5);
    CHECK(r.ir->metadata.decay_from == 3);
    CHECK(entry(*r.ir, 2, {1, 0, 0, 0}) == 3);   // would have died; fades instead
    CHECK(entry(*r.ir, 0, {1, 0, 0, 0}) == 1);   // base transitions survive the transform
    CHECK(entry(*r.ir, 1, {0, 1, 0, 0}) == 2);
    CHECK(entry(*r.ir, 3, {0, 0, 0, 0}) == 4);
    CHECK(entry(*r.ir, 4, {0, 0, 0, 0}) == 0);
}

TEST_CASE("decay 0 is the identity and the hint stays out of the hash", "[decay]") {
    const auto plain = parseDsl("states 2; neighbourhood moore 1; 0: n(1) == 3 -> 1;");
    REQUIRE(plain);
    auto same = applyDecay(*plain.ir, 0);
    REQUIRE(std::holds_alternative<RuleIR>(same));
    CHECK(std::get<RuleIR>(same) == *plain.ir);

    const auto decayed = parseDsl("states 2; neighbourhood moore 1; decay 2; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;");
    REQUIRE(decayed);
    RuleIR stripped = *decayed.ir;
    stripped.metadata.decay_from.reset();
    CHECK(irHash(stripped) == irHash(*decayed.ir));   // metadata is not hashed
}

TEST_CASE("a tail too long for the table is refused, and the message names the longest that fits", "[decay]") {
    const auto base = parseDsl("states 2; neighbourhood moore 1; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;");
    REQUIRE(base);
    CHECK(maxDecay(*base.ir) == 6);
    CHECK(std::holds_alternative<RuleIR>(applyDecay(*base.ir, 6)));
    auto tooLong = applyDecay(*base.ir, 7);
    REQUIRE(std::holds_alternative<std::string>(tooLong));
    CHECK(std::get<std::string>(tooLong).find("longest tail this neighbourhood allows is 6") != std::string::npos);

    const auto r = parseDsl("states 2;\nneighbourhood moore 1;\ndecay 9;\n0: n(1) == 3 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 3);
    CHECK(r.error->message.find("allows is 6") != std::string::npos);
}

TEST_CASE("sparser neighbourhoods allow longer tails", "[decay]") {
    const auto hex = parseDsl("states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 3 -> 0;");
    REQUIRE(hex);
    CHECK(maxDecay(*hex.ir) == 8);
    const auto vn = parseDsl("states 2; neighbourhood von_neumann 1; 0: n(1) == 1 -> 1; 1: n(1) < 1 -> 0;");
    REQUIRE(vn);
    CHECK(maxDecay(*vn.ir) == 14);
    CHECK(std::holds_alternative<RuleIR>(applyDecay(*vn.ir, 14)));
}

TEST_CASE("decay is refused on forms it cannot transform", "[decay]") {
    const auto expr = parseDsl("B2/S/C25");
    REQUIRE(expr);
    REQUIRE(std::holds_alternative<Expression>(expr.ir->transition));
    CHECK(std::holds_alternative<std::string>(applyDecay(*expr.ir, 1)));

    const auto r = parseDsl("states 16; neighbourhood moore 1; decay 2; 0: n(1) == 3 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("already too large") != std::string::npos);
}
