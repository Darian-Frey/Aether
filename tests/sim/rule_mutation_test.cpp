#include "rule/dsl.hpp"
#include "sim/rng.hpp"
#include "sim/rule_mutation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace aether;
using sim::mutateRule;
using sim::Pcg32;

namespace {

size_t tableDiff(const rule::RuleIR& a, const rule::RuleIR& b) {
    const auto& ta = std::get<rule::Table>(a.transition).entries;
    const auto& tb = std::get<rule::Table>(b.transition).entries;
    REQUIRE(ta.size() == tb.size());
    size_t n = 0;
    for (size_t i = 0; i < ta.size(); ++i) n += ta[i] != tb[i];
    return n;
}

}  // namespace

TEST_CASE("a table point edit changes exactly one entry to a different valid state", "[mutation]") {
    const auto life = *rule::parseDsl("B3/S23").ir;
    Pcg32 rng(1);
    for (int i = 0; i < 200; ++i) {
        const auto m = mutateRule(life, 1, rng);
        REQUIRE(m.ir);
        CHECK(m.attempts == 1);
        CHECK(tableDiff(life, *m.ir) == 1);
        CHECK(rule::isValid(*m.ir));
    }
}

TEST_CASE("magnitude applies that many edits (fewer if two land on one entry)", "[mutation]") {
    const auto brain = *rule::parseDsl("B2/S/C3").ir;   // 135 entries, 3 states
    Pcg32 rng(5);
    for (int i = 0; i < 100; ++i) {
        const auto m = mutateRule(brain, 4, rng);
        REQUIRE(m.ir);
        const size_t d = tableDiff(brain, *m.ir);
        CHECK(d >= 1);
        CHECK(d <= 4);
        CHECK(rule::isValid(*m.ir));
    }
}

TEST_CASE("mutation is reproducible from the seed and differs across seeds", "[mutation]") {
    const auto life = *rule::parseDsl("B3/S23").ir;
    Pcg32 a(42), b(42), c(43);
    const auto ma = mutateRule(life, 2, a);
    const auto mb = mutateRule(life, 2, b);
    const auto mc = mutateRule(life, 2, c);
    CHECK(rule::irHash(*ma.ir) == rule::irHash(*mb.ir));
    CHECK(rule::irHash(*ma.ir) != rule::irHash(*mc.ir));
    // The streams stay in step: the next draw agrees too.
    CHECK(a.next() == b.next());
}

TEST_CASE("mutation renames the rule and drops the notation it no longer matches", "[mutation]") {
    const auto life = *rule::parseDsl("B3/S23").ir;
    Pcg32 rng(9);
    const auto m = mutateRule(life, 1, rng);
    REQUIRE(m.ir);
    CHECK_FALSE(m.ir->metadata.source_notation.has_value());
    CHECK(m.ir->metadata.name == "B3/S23*");
    const auto m2 = mutateRule(*m.ir, 1, rng);
    CHECK(m2.ir->metadata.name == "B3/S23*");
}

TEST_CASE("expression edits keep the shape and produce valid trees, redrawing when needed", "[mutation]") {
    // A large Generations rule lowers to an expression with many literals in
    // result position, so +1 on one of them is frequently invalid.
    const auto ir = *rule::parseDsl("B2/S/C25").ir;
    REQUIRE(std::holds_alternative<rule::Expression>(ir.transition));
    const auto& orig = std::get<rule::Expression>(ir.transition);
    Pcg32 rng(3);
    int redraws = 0, produced = 0;
    for (int i = 0; i < 300; ++i) {
        const auto m = mutateRule(ir, 1, rng);
        if (!m.ir) continue;   // eight invalid draws in a row; allowed
        ++produced;
        if (m.attempts > 1) ++redraws;
        CHECK(rule::isValid(*m.ir));
        const auto& e = std::get<rule::Expression>(m.ir->transition);
        REQUIRE(e.nodes.size() == orig.nodes.size());
        for (size_t k = 0; k < e.nodes.size(); ++k) {
            CHECK(e.nodes[k].a == orig.nodes[k].a);
            CHECK(e.nodes[k].b == orig.nodes[k].b);
            CHECK(e.nodes[k].c == orig.nodes[k].c);
        }
    }
    CHECK(produced > 250);
    CHECK(redraws > 0);   // the redraw path is exercised
}

TEST_CASE("fuzz: a million table edits across the fixture set never yield an invalid IR (AV-012)", "[mutation][fuzz]") {
    const char* fixtures[] = {"B3/S23", "B36/S23", "B2/S/C3", "B2/S23/C5",
                              "states 4; neighbourhood moore 1; 1: n(0) >= 0 -> 2; 2: n(0) >= 0 -> 3; 3: n(1) == 1 or n(1) == 2 -> 1;"};
    Pcg32 rng(2026);
    uint64_t edits = 0;
    while (edits < 1'000'000) {
        for (const char* f : fixtures) {
            rule::RuleIR ir = *rule::parseDsl(f).ir;
            // Walk: mutate the mutant, 1000 steps per fixture per round.
            for (int i = 0; i < 1000; ++i) {
                const auto m = mutateRule(ir, 1, rng);
                REQUIRE(m.ir);
                REQUIRE(rule::isValid(*m.ir));
                ir = std::move(*m.ir);
                ++edits;
            }
        }
    }
    CHECK(edits >= 1'000'000);
}

TEST_CASE("a kernel IR is not mutated yet", "[mutation]") {
    rule::RuleIR ir;
    ir.cell_type = core::CellType::F32;
    ir.kind = rule::Kind::Continuous;
    rule::Kernel k;
    k.profile = {1.0f};
    k.growth.nodes = {{rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
    ir.transition = k;
    Pcg32 rng(1);
    const auto m = mutateRule(ir, 1, rng);
    CHECK_FALSE(m.ir);
    CHECK(m.attempts == 0);
}
