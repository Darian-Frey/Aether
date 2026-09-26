// Multi-field grids, the oracle half (F-031, D-022).
//
// The IR side is tests/rule/fields_test.cpp; this file is about execution on
// the CPU reference path, which is what the GPU path will be measured against
// in step 3. What it pins: a field is read at this site and at a neighbour
// with the state's own boundary resolution, each written field gets its own
// expression over one gathered neighbourhood, a field the rule does not write
// is carried through rather than lost to the ping-pong, the u8 clamp is the
// width of the storage and the f32 field has no clamp at all, and cell
// mutation moves the state without touching a field.

#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstring>
#include <span>
#include <vector>

using namespace aether;
using aether::rule::ExprOp;
using aether::rule::Expression;
using aether::sim::cpuStep;

namespace {

// A field pair with the spans the stepper wants. Declaration order is the
// rule's, and both halves are allocated up front: the step loop allocates
// nothing (invariant 8) and this is the harness, not the engine.
class FieldPair {
public:
    FieldPair(const rule::CompiledRule& rule, uint64_t cells) {
        for (const rule::CompiledField& f : rule.fields) {
            const size_t bytes = cells * core::cellBytes(f.cell_type);
            buffers_[0].emplace_back(bytes, uint8_t{0});
            buffers_[1].emplace_back(bytes, uint8_t{0});
        }
        rebuild();
    }

    sim::FieldReads  reads()  const { return reads_; }
    sim::FieldWrites writes() const { return writes_; }

    void swap() {
        cur_ ^= 1;
        rebuild();
    }

    // Read and write one field's cell, whichever type it holds.
    void setU8(size_t f, size_t i, uint8_t v) { buffers_[cur_][f][i] = v; }
    uint8_t u8(size_t f, size_t i) const { return buffers_[cur_][f][i]; }

    void setF32(size_t f, size_t i, float v) {
        std::memcpy(buffers_[cur_][f].data() + i * sizeof(float), &v, sizeof(float));
    }
    float f32(size_t f, size_t i) const {
        float v = 0.0f;
        std::memcpy(&v, buffers_[cur_][f].data() + i * sizeof(float), sizeof(float));
        return v;
    }

    void fillU8(size_t f, uint8_t v) {
        for (uint8_t& b : buffers_[cur_][f]) b = v;
    }

private:
    void rebuild() {
        reads_.clear();
        writes_.clear();
        for (auto& b : buffers_[cur_])     reads_.emplace_back(b);
        for (auto& b : buffers_[cur_ ^ 1]) writes_.emplace_back(b);
    }

    std::vector<std::vector<uint8_t>>      buffers_[2];
    std::vector<std::span<const uint8_t>>  reads_;
    std::vector<std::span<uint8_t>>        writes_;
    int cur_ = 0;
};

rule::RuleIR base(uint8_t radius = 1) {
    rule::RuleIR ir;
    ir.states = 2;
    ir.kind = rule::Kind::Expression;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, radius};
    Expression e;
    e.nodes = {{ExprOp::Self}};
    ir.transition = e;
    return ir;
}

rule::CompiledRule compiled(const rule::RuleIR& ir) {
    auto r = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(r));
    return std::get<rule::CompiledRule>(r);
}

core::GridSpec spec2d(uint32_t w, uint32_t h) {
    core::GridSpec s;
    s.dimensions = 2;
    s.width = w;
    s.height = h;
    return s;
}

}  // namespace

TEST_CASE("a field the rule writes follows its own expression", "[sim][fields]") {
    // energy' = energy + 1, and the state turns on once energy passes two. The
    // two are decided from one reading of the site, so the state at generation
    // g answers about the energy at generation g rather than g+1.
    rule::RuleIR ir = base();
    rule::Field energy;
    energy.name = "energy";
    Expression inc;
    inc.nodes = {{ExprOp::FieldSelf, 0}, {ExprOp::IntLiteral, 0, 0, 0, 1}, {ExprOp::Add, 0, 1}};
    energy.write = inc;
    ir.fields.push_back(energy);

    Expression state;
    state.nodes = {{ExprOp::FieldSelf, 0},
                   {ExprOp::IntLiteral, 0, 0, 0, 2},
                   {ExprOp::Gt, 0, 1},
                   {ExprOp::IntLiteral, 0, 0, 0, 1},
                   {ExprOp::IntLiteral, 0, 0, 0, 0},
                   {ExprOp::Select, 2, 3, 4}};
    ir.transition = state;

    const rule::CompiledRule rule = compiled(ir);
    CHECK(rule.backend == rule::Backend::Codegen);   // D-022: fields force it
    REQUIRE(rule.fields.size() == 1);

    const core::GridSpec spec = spec2d(4, 4);
    core::HostGrid grid(spec);
    FieldPair fields(rule, spec.cellCount());

    // Energy climbs by one a generation; the state lights when the energy it
    // read was three or more.
    const std::vector<uint8_t> expectState = {0, 0, 0, 1, 1};
    for (uint64_t g = 0; g < 5; ++g) {
        cpuStep(rule, spec, grid.current(), grid.next(), g, {}, fields.reads(), fields.writes());
        grid.swap();
        fields.swap();
        CHECK(fields.u8(0, 5) == g + 1);
        CHECK(grid.current()[5] == expectState[g]);
    }
}

TEST_CASE("a field the rule does not write keeps its value", "[sim][fields]") {
    // The ping-pong is why this is a test and not a tautology: the buffer the
    // step writes is two generations old, so carrying a value means copying it.
    rule::RuleIR ir = base();
    rule::Field store;
    store.name = "store";
    ir.fields.push_back(store);           // no write expression

    const rule::CompiledRule rule = compiled(ir);
    const core::GridSpec spec = spec2d(3, 3);
    core::HostGrid grid(spec);
    FieldPair fields(rule, spec.cellCount());
    for (size_t i = 0; i < 9; ++i) fields.setU8(0, i, static_cast<uint8_t>(i * 7));

    for (uint64_t g = 0; g < 4; ++g) {
        cpuStep(rule, spec, grid.current(), grid.next(), g, {}, fields.reads(), fields.writes());
        grid.swap();
        fields.swap();
    }
    for (size_t i = 0; i < 9; ++i) CHECK(fields.u8(0, i) == i * 7);
}

TEST_CASE("a neighbour field read resolves the boundary as the state does", "[sim][fields]") {
    // v' = the sum of v over the eight neighbours. Under a zero boundary a
    // corner sees three of them and the rest read zero; under wrap every cell
    // sees eight.
    for (const rule::Boundary boundary : {rule::Boundary::Zero, rule::Boundary::Wrap}) {
        rule::RuleIR ir = base();
        ir.boundary = boundary;
        rule::Field v;
        v.name = "v";
        // A left-leaning chain of adds: each one takes the running total and
        // the read just pushed. The arena wants children before parents, which
        // pushing in this order gives for free.
        Expression sum;
        sum.nodes.push_back({ExprOp::FieldNeighbour, 0, 0});
        uint32_t acc = 0;
        for (uint32_t i = 1; i < 8; ++i) {
            sum.nodes.push_back({ExprOp::FieldNeighbour, 0, i});
            const uint32_t read = static_cast<uint32_t>(sum.nodes.size()) - 1;
            sum.nodes.push_back({ExprOp::Add, acc, read});
            acc = static_cast<uint32_t>(sum.nodes.size()) - 1;
        }
        v.write = sum;
        ir.fields.push_back(v);

        const rule::CompiledRule rule = compiled(ir);
        const core::GridSpec spec = spec2d(3, 3);
        core::HostGrid grid(spec);
        FieldPair fields(rule, spec.cellCount());
        fields.fillU8(0, 1);

        cpuStep(rule, spec, grid.current(), grid.next(), 0, {}, fields.reads(), fields.writes());
        fields.swap();

        const uint8_t corner = boundary == rule::Boundary::Zero ? 3 : 8;
        const uint8_t edge   = boundary == rule::Boundary::Zero ? 5 : 8;
        CHECK(fields.u8(0, 0) == corner);   // (0,0)
        CHECK(fields.u8(0, 1) == edge);     // (1,0)
        CHECK(fields.u8(0, 4) == 8);        // (1,1), interior either way
    }
}

TEST_CASE("a u8 field clamps to its storage and an f32 field does not clamp", "[sim][fields]") {
    // SPEC §1 clamps a continuous *state* to [0,1]. A field is not that: a
    // resource has no natural ceiling and neither has its texture, so the only
    // clamp a field gets is the one its storage forces.
    rule::RuleIR ir = base();

    rule::Field up;
    up.name = "up";
    Expression add10;
    add10.nodes = {{ExprOp::FieldSelf, 0}, {ExprOp::IntLiteral, 0, 0, 0, 10}, {ExprOp::Add, 0, 1}};
    up.write = add10;

    rule::Field down;
    down.name = "down";
    Expression sub10;
    sub10.nodes = {{ExprOp::FieldSelf, 1}, {ExprOp::IntLiteral, 0, 0, 0, 10}, {ExprOp::Sub, 0, 1}};
    down.write = sub10;

    rule::Field heat;
    heat.name = "heat";
    heat.cell_type = core::CellType::F32;
    Expression dbl;
    dbl.nodes = {{ExprOp::FieldSelf, 2}, {ExprOp::FloatLiteral, 0, 0, 0, 0, 2.0f}, {ExprOp::Mul, 0, 1}};
    heat.write = dbl;

    ir.fields = {up, down, heat};

    const rule::CompiledRule rule = compiled(ir);
    REQUIRE(rule.fields.size() == 3);
    CHECK(rule.fields[2].cell_type == core::CellType::F32);

    const core::GridSpec spec = spec2d(2, 2);
    core::HostGrid grid(spec);
    FieldPair fields(rule, spec.cellCount());
    fields.fillU8(0, 250);
    fields.fillU8(1, 5);
    for (size_t i = 0; i < 4; ++i) fields.setF32(2, i, 1.0f);

    for (uint64_t g = 0; g < 3; ++g) {
        cpuStep(rule, spec, grid.current(), grid.next(), g, {}, fields.reads(), fields.writes());
        grid.swap();
        fields.swap();
    }
    CHECK(fields.u8(0, 0) == 255);    // saturated upward, not wrapped to 4
    CHECK(fields.u8(1, 0) == 0);      // saturated downward, not wrapped to 231
    CHECK_THAT(fields.f32(2, 0),
               Catch::Matchers::WithinULP(8.0f, 0));   // well past the state's ceiling
}

TEST_CASE("cell mutation moves the state and leaves the fields alone", "[sim][fields]") {
    // SPEC §9.2 mutation is the state's. A field that drifted with it could
    // not be conserved, which is what AV-018 is about.
    rule::RuleIR ir = base();
    rule::Field energy;
    energy.name = "energy";
    Expression inc;
    inc.nodes = {{ExprOp::FieldSelf, 0}, {ExprOp::IntLiteral, 0, 0, 0, 1}, {ExprOp::Add, 0, 1}};
    energy.write = inc;
    ir.fields.push_back(energy);
    ir.states = 4;

    const rule::CompiledRule rule = compiled(ir);
    const core::GridSpec spec = spec2d(8, 8);
    core::HostGrid grid(spec);
    FieldPair fields(rule, spec.cellCount());

    sim::CellMutation mutation;
    mutation.threshold = sim::mutationThreshold(1.0);   // every cell, every generation
    mutation.seedB = 0xfeedu;

    for (uint64_t g = 0; g < 5; ++g) {
        cpuStep(rule, spec, grid.current(), grid.next(), g, mutation, fields.reads(), fields.writes());
        grid.swap();
        fields.swap();
    }
    for (size_t i = 0; i < spec.cellCount(); ++i) CHECK(fields.u8(0, i) == 5);

    bool anyNonZero = false;
    for (uint8_t s : grid.current()) anyNonZero = anyNonZero || s != 0;
    CHECK(anyNonZero);   // the state did drift, so the check above means something
}

TEST_CASE("fields are refused on a transition that cannot carry them", "[sim][fields]") {
    // A table cannot express a field write and nothing yet says what a
    // neighbour read means to a rule whose state is a float (D-021, D-022).
    rule::RuleIR ir = base();
    ir.kind = rule::Kind::OuterTotalistic;
    ir.transition = rule::Table{std::vector<uint8_t>(2 * 9, 0)};
    rule::Field f;
    f.name = "f";
    ir.fields.push_back(f);
    CHECK(std::holds_alternative<rule::CompileError>(rule::compileRule(ir)));
}
