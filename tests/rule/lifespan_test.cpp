// Hard cell lifespan (F-034, SPEC §7).
//
// The mirror of `decay`: where that appends states after a death so a cell has
// somewhere to fade to, this inserts states before one so a cell has a deadline.
// A front-end transform, IR in and IR out, so what this file checks is the
// *table* it produces — nothing downstream learns a new concept, which is the
// whole reason for the route.
//
// The claim that matters is in the name: a cell dies at its last age *regardless
// of its neighbours*. A test that only checked cells die eventually would pass
// for a rule that was merely slower to die, which is decay and not this.

#include "rule/dsl.hpp"
#include "rule/ir.hpp"
#include "rule/lua.hpp"
#include "rule/decay.hpp"
#include "rule/lifespan.hpp"
#include "rule/table_layout.hpp"
#include "support/table.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace aether;
using namespace aether::rule;

namespace {

RuleIR life() {
    auto r = parseDsl("B3/S23");
    REQUIRE(r);
    return *r.ir;
}

// What the rule says a cell of state `own` with `k` live neighbours becomes.
uint8_t next(const RuleIR& ir, uint8_t own, uint32_t k) {
    const uint32_t N = neighbourCount(ir.dimensions, ir.neighbourhood);
    const TableLayout layout(ir.kind, ir.states, N);
    return std::get<Table>(ir.transition).entries[layout.indexCounted(own, k)];
}

}  // namespace

TEST_CASE("a lifespan gives a cell ages and a deadline", "[lifespan]") {
    const auto spanned = applyLifespan(life(), 4);
    REQUIRE(std::holds_alternative<RuleIR>(spanned));
    const RuleIR& ir = std::get<RuleIR>(spanned);

    // State 0 plus four ages, counted rather than outer-totalistic: every age has
    // to count as a live neighbour, and counting a set of states as one thing is
    // what the counted kind is for.
    CHECK(ir.states == 5);
    CHECK(ir.kind == Kind::CountedTotalistic);
    REQUIRE(ir.counted.size() == 5);
    for (uint16_t own = 0; own < 5; ++own) {
        CHECK_FALSE(ir.counted[own].test(0));
        for (uint16_t a = 1; a < 5; ++a) CHECK(ir.counted[own].test(a));
    }

    // Born into age 1 on three neighbours, as B3 says, and not otherwise.
    CHECK(next(ir, 0, 3) == 1);
    CHECK(next(ir, 0, 2) == 0);
    CHECK(next(ir, 0, 4) == 0);

    // Supported at any age, advance. S23, so two or three.
    CHECK(next(ir, 1, 2) == 2);
    CHECK(next(ir, 2, 3) == 3);
    CHECK(next(ir, 3, 2) == 4);

    // The deadline, and the point of the feature: the last age dies on the counts
    // that would otherwise have kept it alive.
    CHECK(next(ir, 4, 2) == 0);
    CHECK(next(ir, 4, 3) == 0);

    // And an unsupported cell still dies at once, at any age — a lifespan is a
    // ceiling on life, not a floor.
    CHECK(next(ir, 1, 1) == 0);
    CHECK(next(ir, 3, 8) == 0);
}

TEST_CASE("a lifespan of one or zero changes nothing", "[lifespan]") {
    // One age is what a Life-like rule already has: a cell either survives into
    // the same state or dies.
    const RuleIR base = life();
    for (uint16_t ages : {uint16_t{0}, uint16_t{1}}) {
        const auto same = applyLifespan(base, ages);
        REQUIRE(std::holds_alternative<RuleIR>(same));
        CHECK(irHash(std::get<RuleIR>(same)) == irHash(base));
    }
}

TEST_CASE("a lifespan is refused where an age would be a guess", "[lifespan]") {
    // A rule with three or more states already means something by each of them,
    // and an age inserted among those would be the transform deciding which of
    // the author's states was the living one.
    auto wireworld = parseDsl("states 4; neighbourhood moore 1;"
                              "1: n(0) >= 0 -> 2; 2: n(0) >= 0 -> 3; 3: n(1) == 1 or n(1) == 2 -> 1;");
    REQUIRE(wireworld);
    const auto refused = applyLifespan(*wireworld.ir, 3);
    REQUIRE(std::holds_alternative<std::string>(refused));
    CHECK(std::get<std::string>(refused).find("two-state") != std::string::npos);

    // And the limits are named rather than hit.
    const auto tooLong = applyLifespan(life(), 300);
    REQUIRE(std::holds_alternative<std::string>(tooLong));
    CHECK(std::get<std::string>(tooLong).find("256") != std::string::npos);
    CHECK(maxLifespan(life()) > 8);
    CHECK(maxLifespan(*wireworld.ir) == 0);
}

TEST_CASE("a lifespan composes with an ageing tail", "[lifespan]") {
    // F-034's acceptance says alongside the soft decay of F-025 rather than
    // instead of it, and the order is what makes that mean anything: ages first,
    // then a tail to fade into. A cell then lives three generations and spends
    // two more fading.
    const auto spanned = applyLifespan(life(), 3);
    REQUIRE(std::holds_alternative<RuleIR>(spanned));
    const auto both = applyDecay(std::get<RuleIR>(spanned), 2);
    REQUIRE(std::holds_alternative<RuleIR>(both));
    const RuleIR& ir = std::get<RuleIR>(both);

    CHECK(ir.states == 6);                 // 0, ages 1..3, tail 4..5
    CHECK(ir.metadata.decay_from == 4);    // the palette hint points past the ages

    // The ages still count as alive and the tail still counts as nothing, so a
    // fading cell neither feeds a birth nor holds a neighbour up. Only the
    // states that *ask* a question have a set: a tail state advances
    // unconditionally, so what it would count is left empty by `decay` and
    // asserting otherwise would be asserting about an answer nobody needs.
    for (uint16_t own = 0; own <= 3; ++own) {
        for (uint16_t a = 1; a <= 3; ++a) CHECK(ir.counted[own].test(a));
        for (uint16_t t = 4; t <= 5; ++t) CHECK_FALSE(ir.counted[own].test(t));
    }

    // The deadline now routes into the tail rather than straight to nothing.
    CHECK(next(ir, 3, 2) == 4);
    CHECK(next(ir, 4, 0) == 5);
    CHECK(next(ir, 5, 0) == 0);
    // And a supported young cell still advances.
    CHECK(next(ir, 1, 3) == 2);
}

TEST_CASE("both front ends can ask for a lifespan", "[lifespan]") {
    // The DSL keyword sits before `decay` in the grammar because that is the
    // order they compose in; writing them the other way round would read as
    // though the tail came first.
    auto dsl = parseDsl("states 2; neighbourhood moore 1; lifespan 6; decay 2;"
                        "0: n(1) == 3 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(dsl);
    CHECK(dsl.ir->states == 9);                 // 0, ages 1..6, tail 7..8
    CHECK(dsl.ir->metadata.decay_from == 7);
    CHECK(next(*dsl.ir, 6, 2) == 7);            // the deadline routes into the tail

    // And a rule already too large for a table says so rather than trying.
    auto refused = parseDsl("states 2; neighbourhood moore 1; lifespan 300;"
                            "0: n(1) == 3 -> 1;");
    CHECK_FALSE(refused);

    // Lua asks with a field, and the result is the same rule: one desugaring
    // rather than two, which is what IMP-003 was about for `decay`.
    auto lua = compileLua(R"(
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            lifespan = 6,
            -- counts[1] is the number of neighbours in state 1. The first
            -- attempt here read counts[3], which does not exist in a two-state
            -- rule, so the function returned 0 everywhere and the deadline
            -- assertion below passed by comparing nothing to nothing.
            transition = function(own, counts)
                if own == 0 then return counts[1] == 3 and 1 or 0 end
                return (counts[1] == 2 or counts[1] == 3) and 1 or 0
            end,
        }
    )", LuaContext{});
    if (const auto* e = std::get_if<LuaError>(&lua)) FAIL(e->message);
    const RuleIR& spanned = std::get<RuleIR>(lua);
    CHECK(spanned.states == 7);
    CHECK(spanned.kind == Kind::CountedTotalistic);
    CHECK(next(spanned, 0, 3) == 1);     // born into age 1, so the rule is real
    CHECK(next(spanned, 1, 2) == 2);     // an age advances
    CHECK(next(spanned, 6, 2) == 0);     // and the deadline bites

    // B/S notation has no statement list, so `lifespan` has nowhere to go there
    // and `C` is a tail rather than a deadline. A rule wanting ages writes the
    // statement form.
    auto bs = parseDsl("B3/S23/C5");
    REQUIRE(bs);
    CHECK(bs.ir->metadata.decay_from == 2);
}

TEST_CASE("a genome-tunable lifespan needs no feature at all", "[lifespan][genome]") {
    // F-034's third acceptance point. The desugaring above gives a *fixed*
    // deadline, because it bakes the ages into a table. A per-cell deadline is a
    // different thing and wants no engine support: with the ages as states, a
    // cell's age is its state index, so the limit is an ordinary comparison
    // between that index and some bits of the genome — which F-033's operators
    // already express. The engine learns nothing; the rule does the work.
    auto compiled = compileLua(R"(
        local e = expr
        -- Ages 1..7 live, and the genome's low three bits say how long. A cell
        -- dies when its age reaches its own limit, whatever its neighbours.
        local age = e.self()
        local limit = e.band(e.field("genome"), e.int(7))
        local supported = e.or_(e.eq(e.count(1), e.int(2)), e.eq(e.count(1), e.int(3)))
        local born = e.eq(e.count(1), e.int(2))
        return {
            states = 8,
            neighbourhood = { type = "moore", radius = 1 },
            fields = { { name = "genome", cell_type = "u32" } },
            genome = { field = "genome", bits = 3 },
            transition = e.select(
                e.eq(age, e.int(0)),
                e.select(born, e.int(1), e.int(0)),
                e.select(e.or_(e.ge(age, limit), e.not_(supported)),
                         e.int(0), e.add(age, e.int(1)))),
        }
    )", LuaContext{});
    if (const auto* e = std::get_if<LuaError>(&compiled)) FAIL(e->message);
    const RuleIR& ir = std::get<RuleIR>(compiled);
    CHECK(ir.kind == Kind::Expression);
    REQUIRE(ir.genome.has_value());
    CHECK(ir.genome->bits == 3);
    CHECK(isValid(ir));
    // Nothing about `lifespan` is in this rule: the deadline is arithmetic the
    // author wrote, so it varies per cell and is inherited with everything else.
}
