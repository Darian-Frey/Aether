#include "rule/decay.hpp"
#include "rule/dsl.hpp"
#include "rule/table_layout.hpp"
#include "support/table.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

namespace {

uint8_t entry(const RuleIR& ir, uint8_t own, std::vector<uint32_t> counts) {
    return aether::test::tableEntry(ir, own, std::move(counts));
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
    CHECK(ir.kind == Kind::CountedTotalistic);   // one count is enough for this rule (D-016)

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

TEST_CASE("a counted rule takes a tail of any length up to the state limit", "[decay]") {
    // Before D-016 a Moore r=1 tail was capped at six states by the
    // combinatorial table. Counting one state makes it S·(N+1) instead, and
    // the only limit left is the 256 states of SPEC §1.
    const auto base = parseDsl("states 2; neighbourhood moore 1; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;");
    REQUIRE(base);
    CHECK(maxDecay(*base.ir) == 254);

    auto longTail = applyDecay(*base.ir, 60);
    REQUIRE(std::holds_alternative<RuleIR>(longTail));
    const RuleIR& ir = std::get<RuleIR>(longTail);
    CHECK(ir.states == 62);
    CHECK(ir.kind == Kind::CountedTotalistic);
    CHECK(std::get<Table>(ir.transition).entries.size() == 62 * 9);
    CHECK(isValid(ir));
    CHECK(entry(ir, 1, {1}) == 2);
    for (uint16_t t = 2; t < 61; ++t) CHECK(entry(ir, static_cast<uint8_t>(t), {0}) == t + 1);
    CHECK(entry(ir, 61, {0}) == 0);

    auto tooLong = applyDecay(*base.ir, 255);
    REQUIRE(std::holds_alternative<std::string>(tooLong));
    CHECK(std::get<std::string>(tooLong).find("256") != std::string::npos);

    const auto hundred = parseDsl("states 2; neighbourhood moore 1; decay 100; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;");
    REQUIRE(hundred);
    CHECK(hundred.ir->states == 102);
}

TEST_CASE("every lattice takes a long tail now", "[decay]") {
    for (const char* src : {"states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 3 -> 0;",
                            "states 2; neighbourhood von_neumann 1; 0: n(1) == 1 -> 1; 1: n(1) < 1 -> 0;",
                            "states 2; neighbourhood moore 2; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;"}) {
        const auto r = parseDsl(src);
        REQUIRE(r);
        CHECK(maxDecay(*r.ir) == 254);
        CHECK(std::holds_alternative<RuleIR>(applyDecay(*r.ir, 32)));
    }
}

TEST_CASE("decay is refused on forms it cannot transform", "[decay]") {
    RuleIR expr;
    expr.kind = Kind::Expression;
    Expression e;
    e.nodes = {{ExprOp::Self}};
    expr.transition = e;
    CHECK(std::holds_alternative<std::string>(applyDecay(expr, 1)));

    // A rule whose own state needs two separate counts keeps the full count
    // vector, and with sixteen states that leaves no room for a tail.
    const auto r = parseDsl("states 16; neighbourhood moore 1; decay 2; 0: n(1) == 3 and n(2) == 0 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("already too large") != std::string::npos);
}
