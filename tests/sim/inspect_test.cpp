// The cell inspector (F-030, D-018).
//
// That the inspector's prediction matches the stepper is asserted over every
// equivalence fixture in equivalence_test.cpp, where the fixtures live. This
// file is about what it *reports*: the neighbourhood's geometry, what a
// boundary did to it, the number the kind reduced it to, and which branch of
// a generated rule answered.

#include "sim/inspect.hpp"

#include "rule/dsl.hpp"
#include "rule/growth.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace aether;
using core::GridSpec;
using core::HostGrid;
using sim::Inspection;

namespace {

rule::CompiledRule compile(const char* dsl, rule::Boundary b = rule::Boundary::Wrap, uint8_t dims = 2) {
    rule::DslContext ctx;
    ctx.dimensions = dims;
    ctx.boundary = b;
    auto parsed = rule::parseDsl(dsl, ctx);
    REQUIRE(parsed);
    rule::RuleIR ir = *parsed.ir;
    ir.boundary = b;
    auto c = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(c));
    return std::get<rule::CompiledRule>(std::move(c));
}

Inspection look(const rule::CompiledRule& rule, const HostGrid& g, uint32_t x, uint32_t y) {
    sim::StepScratch scratch(rule);
    return sim::inspect(rule, g.spec(), g.current(), x, y, 0, 0, sim::CellMutation{}, scratch);
}

const sim::NeighbourCell& at(const Inspection& in, int dx, int dy) {
    const auto it = std::find_if(in.neighbours.begin(), in.neighbours.end(),
                                 [&](const sim::NeighbourCell& n) {
                                     return n.offset.dx == dx && n.offset.dy == dy && n.offset.dz == 0;
                                 });
    REQUIRE(it != in.neighbours.end());
    return *it;
}

}  // namespace

TEST_CASE("the inspector reports one entry per neighbour, in the rule's own order", "[inspect]") {
    const rule::CompiledRule rule = compile("B3/S23");
    HostGrid g(GridSpec{2, 8, 8, 1});
    const Inspection in = look(rule, g, 4, 4);

    REQUIRE(in.neighbours.size() == rule.neighbourCount());
    for (size_t i = 0; i < in.neighbours.size(); ++i) {
        CHECK(in.neighbours[i].offset.dx == rule.offsets[i].dx);
        CHECK(in.neighbours[i].offset.dy == rule.offsets[i].dy);
        CHECK(in.neighbours[i].offset.dz == rule.offsets[i].dz);
    }
}

TEST_CASE("the inspector reports the states the rule was actually given", "[inspect]") {
    const rule::CompiledRule rule = compile("B3/S23");
    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(3, 3, 0, 1);
    g.set(5, 5, 0, 1);
    g.set(4, 4, 0, 1);

    const Inspection in = look(rule, g, 4, 4);
    CHECK(in.transition.own == 1);
    CHECK(at(in, -1, -1).state == 1);
    CHECK(at(in, +1, +1).state == 1);
    CHECK(at(in, +1, -1).state == 0);
    // Two live neighbours on a live cell: Life keeps it.
    CHECK(in.transition.next == 1);
}

TEST_CASE("a wrapped neighbour says where it actually came from", "[inspect]") {
    const rule::CompiledRule rule = compile("B3/S23", rule::Boundary::Wrap);
    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(7, 0, 0, 1);   // the far corner on the x axis

    const Inspection in = look(rule, g, 0, 0);
    const sim::NeighbourCell& left = at(in, -1, 0);
    CHECK(left.outside == false);
    CHECK(left.wrapped == true);
    CHECK(left.x == 7);
    CHECK(left.y == 0);
    CHECK(left.state == 1);

    // One that did not leave the grid is not marked as having done so.
    const sim::NeighbourCell& right = at(in, +1, 0);
    CHECK(right.wrapped == false);
    CHECK(right.x == 1);
}

TEST_CASE("a zero boundary marks the neighbours that are off the grid", "[inspect]") {
    const rule::CompiledRule rule = compile("B3/S23", rule::Boundary::Zero);
    HostGrid g(GridSpec{2, 8, 8, 1});
    const Inspection in = look(rule, g, 0, 0);

    CHECK(at(in, -1, 0).outside);
    CHECK(at(in, 0, -1).outside);
    CHECK(at(in, -1, -1).outside);
    CHECK(at(in, -1, -1).state == 0);     // read as empty, which is what the step does
    CHECK_FALSE(at(in, +1, +1).outside);

    // Exactly three of a corner cell's eight neighbours are on the grid.
    const auto on = std::count_if(in.neighbours.begin(), in.neighbours.end(),
                                  [](const sim::NeighbourCell& n) { return !n.outside; });
    CHECK(on == 3);
}

TEST_CASE("a mirror boundary reports the cell it reflected to", "[inspect]") {
    const rule::CompiledRule rule = compile("B3/S23", rule::Boundary::Mirror);
    HostGrid g(GridSpec{2, 8, 8, 1});
    const Inspection in = look(rule, g, 0, 3);

    const sim::NeighbourCell& left = at(in, -1, 0);
    CHECK_FALSE(left.outside);
    CHECK(left.wrapped);
    CHECK(left.x == 1);      // reflected about the edge cell, SPEC §2
    CHECK(left.y == 3);
}

TEST_CASE("an outer-totalistic rule reports one count per state", "[inspect]") {
    // Counts run over states 1..S-1: state 0 is not counted, because an
    // outer-totalistic table does not index on it (SPEC §5).
    const rule::CompiledRule rule = compile(
        "states 3; neighbourhood moore 1;"
        "0: n(1) == 2 and n(2) == 1 -> 1; 1: n(2) >= 1 -> 2; 2: n(1) >= 5 -> 0;");
    REQUIRE(rule.kind == rule::Kind::OuterTotalistic);

    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(3, 3, 0, 1);
    g.set(4, 3, 0, 1);
    g.set(5, 5, 0, 2);

    const Inspection in = look(rule, g, 4, 4);
    REQUIRE(in.reduction.hasPerState);
    CHECK_FALSE(in.reduction.perStateFromZero);
    REQUIRE(in.reduction.perState.size() == 2);
    CHECK(in.reduction.perState[0] == 2);   // two neighbours in state 1
    CHECK(in.reduction.perState[1] == 1);   // one in state 2
    CHECK(in.transition.hasIndex);
    // Two in state 1 and one in state 2, on a dead cell: the first clause.
    CHECK(in.transition.next == 1);
}

TEST_CASE("a two-state rule keeps the outer-totalistic form", "[inspect]") {
    // D-016 uses counted_totalistic only where it is strictly smaller, which
    // a binary rule is not, so Life is indexed the way it always was.
    const rule::CompiledRule rule = compile("B3/S23");
    CHECK(rule.kind == rule::Kind::OuterTotalistic);

    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(3, 3, 0, 1);
    g.set(4, 3, 0, 1);
    g.set(5, 3, 0, 1);

    const Inspection in = look(rule, g, 4, 4);
    REQUIRE(in.reduction.hasPerState);
    REQUIRE(in.reduction.perState.size() == 1);
    CHECK(in.reduction.perState[0] == 3);
    CHECK(in.transition.next == 1);          // three live neighbours: a birth
}

TEST_CASE("a counted-totalistic rule reports the count it was indexed with", "[inspect]") {
    // A generations rule asks about one state per own state, which is what
    // D-016's smaller form is for.
    const rule::CompiledRule rule = compile("B2/S23/C5");
    REQUIRE(rule.kind == rule::Kind::CountedTotalistic);

    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(3, 3, 0, 1);
    g.set(4, 3, 0, 1);
    g.set(5, 5, 0, 3);      // an ageing cell, which a birth does not count

    const Inspection in = look(rule, g, 4, 4);
    CHECK(in.reduction.hasScalar);
    CHECK(in.reduction.scalar == 2);
    CHECK_FALSE(in.reduction.hasPerState);
    CHECK(in.transition.hasIndex);
    CHECK(in.transition.next == 1);          // B2 on a dead cell
}

TEST_CASE("a totalistic rule reports the sum it was indexed with", "[inspect]") {
    rule::RuleIR ir;
    ir.states = 4;
    ir.kind = rule::Kind::Totalistic;
    ir.neighbourhood = {rule::NeighbourhoodType::VonNeumann, 1};
    rule::Table t;
    t.entries.assign(*rule::tableSize(ir.kind, ir.states, rule::neighbourCount(ir.dimensions, ir.neighbourhood)), 0);
    ir.transition = t;
    auto c = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(c));
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(c));

    HostGrid g(GridSpec{2, 8, 8, 1});
    g.set(4, 4, 0, 1);
    g.set(3, 4, 0, 2);
    g.set(5, 4, 0, 3);

    const Inspection in = look(rule, g, 4, 4);
    CHECK(in.reduction.hasScalar);
    CHECK(in.reduction.scalar == 6);   // own 1 + neighbours 2 and 3
}


TEST_CASE("a generated rule names the conditional that answered", "[inspect]") {
    // Two conditionals over a three-state rule, written as an expression so
    // that it is certain to go through codegen: a cell in state 1 becomes 2,
    // a cell with two live neighbours becomes 1, anything else stays put.
    rule::RuleIR ir;
    ir.states = 3;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Self},                        // 0
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},      // 1
        {rule::ExprOp::Eq, 0, 1},                    // 2  own == 1
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},      // 3
        {rule::ExprOp::Count, 1},                    // 4  neighbours in state 1
        {rule::ExprOp::Eq, 4, 3},                    // 5  count == 2
        {rule::ExprOp::Select, 5, 1, 0},             // 6  inner: count==2 ? 1 : own
        {rule::ExprOp::Select, 2, 3, 6},             // 7  outer: own==1  ? 2 : inner
    };
    ir.transition = e;
    auto c = rule::compileRule(ir);
    if (const auto* err = std::get_if<rule::CompileError>(&c)) FAIL(err->message);
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(c));
    REQUIRE(rule.backend == rule::Backend::Codegen);

    HostGrid g(GridSpec{2, 12, 12, 1});

    // The outer conditional answers.
    g.set(5, 5, 0, 1);
    const Inspection outer = look(rule, g, 5, 5);
    CHECK(outer.transition.next == 2);
    REQUIRE(outer.clause.has_value());
    CHECK(*outer.clause == 7);

    // The outer test fails and the inner one answers.
    g.set(7, 8, 0, 1);
    g.set(9, 8, 0, 1);
    const Inspection inner = look(rule, g, 8, 8);
    CHECK(inner.transition.next == 1);
    REQUIRE(inner.clause.has_value());
    CHECK(*inner.clause == 6);

    // Both tests fail, so the last alternative answered and there is no
    // clause to name. The inspector says that rather than naming one.
    const Inspection none = look(rule, g, 2, 2);
    CHECK(none.transition.next == 0);
    CHECK_FALSE(none.clause.has_value());
    CHECK(none.clauseIsDefault);

    // Every count the expression could have asked about is reported, state 0
    // included, which an outer-totalistic table would not have.
    REQUIRE(none.reduction.hasPerState);
    CHECK(none.reduction.perStateFromZero);
    CHECK(none.reduction.perState.size() == 3);
}

TEST_CASE("the inspector reports a continuous cell's convolution and increment", "[inspect]") {
    // A continuous rule reduces its neighbourhood to one number rather than
    // counting states, so what is worth showing is different.
    rule::RuleIR ir;
    ir.states = 2;
    ir.cell_type = core::CellType::F32;
    ir.kind = rule::Kind::Continuous;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 2};
    rule::Kernel k;
    k.shape = rule::Kernel::Shape::Radial;
    k.profile = {1.0f, 0.5f, 0.1f};
    k.growth = rule::growthExpression({rule::GrowthForm::Rectangular, 0.3f, 0.05f, 0.1f});
    ir.transition = k;
    auto c = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&c)) FAIL(e->message);
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(c));

    HostGrid g(GridSpec{2, 16, 16, 1, core::CellType::F32});
    for (uint32_t y = 3; y < 8; ++y) {
        for (uint32_t x = 3; x < 8; ++x) g.setFloat(x, y, 0, 0.5f);
    }

    const Inspection in = look(rule, g, 5, 5);
    CHECK(in.transition.ownValue == 0.5f);
    CHECK(in.transition.convolution > 0.0f);
    CHECK(in.transition.nextValue >= 0.0f);
    CHECK(in.transition.nextValue <= 1.0f);
    // Neighbour values are reported, not neighbour states.
    CHECK(in.neighbours.size() == rule.neighbourCount());
    CHECK(at(in, 1, 0).value == 0.5f);
    CHECK_FALSE(in.reduction.hasPerState);
}
