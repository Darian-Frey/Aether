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

#include "core/gpu_grid.hpp"
#include "rule/compile.hpp"
#include "rule/glsl.hpp"
#include "sim/cpu_step.hpp"
#include "rule/dsl.hpp"
#include "sim/gpu_step.hpp"
#include "sim/noise.hpp"
#include "sim/scratch.hpp"
#include "sim/simulation.hpp"
#include "support/fields.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <numeric>
#include <span>
#include <vector>

using namespace aether;
using aether::rule::ExprOp;
using aether::rule::Expression;
using aether::sim::cpuStep;
using aether::test::GlContext;
using FieldPair = aether::test::HostFields;
using aether::test::requireGl;

namespace {

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


// --- The GPU half (F-031 step 3) ---------------------------------------------

namespace {

// A little arena builder. Hand-written node lists are how the earlier cases in
// this file are written and they are legible at four nodes; the rule below has
// twenty across three expressions, where counting indices by eye is how a test
// comes to pass for the wrong reason.
struct Build {
    Expression e;

    uint32_t node(ExprOp op, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
        e.nodes.push_back({op, a, b, c, 0, 0.0f});
        return static_cast<uint32_t>(e.nodes.size()) - 1;
    }
    uint32_t lit(int64_t v) {
        e.nodes.push_back({ExprOp::IntLiteral, 0, 0, 0, v, 0.0f});
        return static_cast<uint32_t>(e.nodes.size()) - 1;
    }
    uint32_t flit(float v) {
        e.nodes.push_back({ExprOp::FloatLiteral, 0, 0, 0, 0, v});
        return static_cast<uint32_t>(e.nodes.size()) - 1;
    }
};

// Four states, two fields, and a transition and two writes that between them
// use every operator the shader had to learn: a field at this site, a field at
// a neighbour, a count beside a field read (so the `cnt` array is emitted in
// more than one generated function), and float arithmetic in a rule whose state
// is an integer.
rule::RuleIR twoFieldRule(rule::Boundary boundary) {
    rule::RuleIR ir;
    ir.states = 4;
    ir.kind = rule::Kind::Expression;
    ir.boundary = boundary;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};

    rule::Field energy;
    energy.name = "energy";
    {
        Build b;
        const uint32_t self = b.node(ExprOp::Self);
        const uint32_t zero = b.lit(0);
        const uint32_t dead = b.node(ExprOp::Eq, self, zero);
        const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
        const uint32_t live = b.node(ExprOp::Count, 1);
        const uint32_t up   = b.node(ExprOp::Add, own, live);
        const uint32_t two  = b.lit(2);
        const uint32_t down = b.node(ExprOp::Sub, own, two);
        b.node(ExprOp::Select, dead, up, down);
        energy.write = b.e;
    }

    rule::Field heat;
    heat.name = "heat";
    heat.cell_type = core::CellType::F32;
    {
        Build b;
        const uint32_t own  = b.node(ExprOp::FieldSelf, 1);
        const uint32_t east = b.node(ExprOp::FieldNeighbour, 1, 0);
        const uint32_t sum  = b.node(ExprOp::Add, own, east);
        const uint32_t half = b.flit(0.5f);
        b.node(ExprOp::Mul, sum, half);
        heat.write = b.e;
    }

    ir.fields = {energy, heat};

    Build b;
    const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
    const uint32_t ten  = b.lit(10);
    const uint32_t rich = b.node(ExprOp::Gt, own, ten);
    const uint32_t one  = b.lit(1);
    const uint32_t self = b.node(ExprOp::Self);
    b.node(ExprOp::Select, rich, one, self);
    ir.transition = b.e;
    return ir;
}

// Deterministic, and not the session RNG: this is scaffolding, the same as the
// fill in equivalence_test.cpp.
uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }

}  // namespace

TEST_CASE("a multi-field rule steps identically on both paths", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    const bool mutating = GENERATE(false, true);

    const rule::CompiledRule rule = compiled(twoFieldRule(boundary));
    REQUIRE(rule.backend == rule::Backend::Codegen);
    REQUIRE(rule.fields.size() == 2);
    // The generated text really does carry the shape the shader expects.
    CHECK(rule.glsl.find("struct " + rule::glslFieldsStruct()) != std::string::npos);
    CHECK(rule.glsl.find(rule::glslFieldFunction(0)) != std::string::npos);
    CHECK(rule.glsl.find(rule::glslFieldFunction(1)) != std::string::npos);

    const core::GridSpec spec = spec2d(40, 24);
    core::HostGrid host(spec);
    FieldPair fields(rule, spec.cellCount());

    // One seed for both sides: activity at the edges and corners, energy spread
    // across the clamp, heat spread across the unit interval.
    uint32_t seed = 0x51ced00d;
    for (uint8_t& c : host.current()) c = static_cast<uint8_t>(lcg(seed) % rule.states);
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        fields.setU8(0, i, static_cast<uint8_t>(lcg(seed) % 256));
        fields.setF32(1, i, static_cast<float>(lcg(seed) % 1000) / 1000.0f);
    }

    // The GPU side. A field is a grid of one value per site, so it is a GpuGrid
    // of the field's cell type over the same extents — no new storage class.
    auto stateGpu = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(stateGpu));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(stateGpu);
    gpu.upload(host.current());

    auto madeFields = aether::test::GpuFields::create(rule, spec);
    REQUIRE(std::holds_alternative<aether::test::GpuFields>(madeFields));
    aether::test::GpuFields& fieldGpu = std::get<aether::test::GpuFields>(madeFields);
    fieldGpu.upload(fields);

    sim::GpuStepper stepper;
    if (auto e = stepper.setRule(rule, spec)) FAIL(e->message);

    sim::CellMutation mutation;
    if (mutating) {
        mutation.threshold = sim::mutationThreshold(0.01);
        mutation.seedB = 0xa5a5'1234u;
    }
    stepper.setCellMutation(mutation);

    // Compared every generation rather than once at the end. A divergence in a
    // field is a wrong number in one cell, and "these two kilobyte vectors
    // differ" is not a diagnosis; the generation and the cell are.
    constexpr int kGenerations = 200;
    std::vector<uint8_t> got;
    for (int g = 0; g < kGenerations; ++g) {
        cpuStep(rule, spec, host.current(), host.next(), static_cast<uint64_t>(g), mutation,
                fields.reads(), fields.writes());
        host.swap();
        fields.swap();

        stepper.step(gpu.current(), gpu.next(), fieldGpu.textures());
        gpu.swap();
        fieldGpu.swap();

        got.assign(spec.bytesPerBuffer(), 0);
        gpu.download(got);
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] != host.current()[i]) {
                FAIL(std::format("generation {}: state cell {} is {} on the CPU and {} on the GPU",
                                 g, i, host.current()[i], got[i]));
            }
        }
        for (size_t f = 0; f < rule.fields.size(); ++f) {
            got.assign(fields.raw(f).size(), 0);
            fieldGpu.download(f, got);
            // Bitwise, the f32 field included: `precise` and the no-division
            // rule exist so the two are equal and not merely close (AV-015).
            if (got == fields.raw(f)) continue;
            for (size_t i = 0; i < got.size(); ++i) {
                if (got[i] == fields.raw(f)[i]) continue;
                const size_t cell = i / core::cellBytes(rule.fields[f].cell_type);
                if (rule.fields[f].cell_type == core::CellType::F32) {
                    float a = 0.0f, b = 0.0f;
                    std::memcpy(&a, fields.raw(f).data() + cell * 4, 4);
                    std::memcpy(&b, got.data() + cell * 4, 4);
                    FAIL(std::format("generation {}: field {} cell {} is {:a} on the CPU and {:a} on the GPU",
                                     g, f, cell, a, b));
                }
                FAIL(std::format("generation {}: field {} cell {} is {} on the CPU and {} on the GPU",
                                 g, f, cell, fields.raw(f)[i], got[i]));
            }
        }
    }
}

// --- Simulation and the session format (F-031 step 4) ------------------------

namespace {

// A field rule with something to watch: energy climbs where the cell is dead and
// falls where it is alive, and the state turns on once the energy passes ten. It
// therefore reaches a state neither the grid nor the field alone determines,
// which is what makes a session round-trip worth checking.
rule::RuleIR simFieldRule() {
    rule::RuleIR ir = base();
    ir.states = 4;

    rule::Field energy;
    energy.name = "energy";
    {
        Build b;
        const uint32_t self = b.node(ExprOp::Self);
        const uint32_t zero = b.lit(0);
        const uint32_t dead = b.node(ExprOp::Eq, self, zero);
        const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
        const uint32_t one  = b.lit(1);
        const uint32_t up   = b.node(ExprOp::Add, own, one);
        const uint32_t two  = b.lit(2);
        const uint32_t down = b.node(ExprOp::Sub, own, two);
        b.node(ExprOp::Select, dead, up, down);
        energy.write = b.e;
    }
    ir.fields.push_back(energy);

    Build b;
    const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
    const uint32_t ten  = b.lit(10);
    const uint32_t rich = b.node(ExprOp::Gt, own, ten);
    const uint32_t one  = b.lit(1);
    const uint32_t self = b.node(ExprOp::Self);
    b.node(ExprOp::Select, rich, one, self);
    ir.transition = b.e;
    return ir;
}

std::vector<uint8_t> bytesOf(std::span<const uint8_t> b) { return {b.begin(), b.end()}; }

}  // namespace

TEST_CASE("a Simulation carries a field rule's storage", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(16, 16);
    auto made = sim::Simulation::create(spec, simFieldRule(), sim::Path::Gpu, 1u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    REQUIRE(s.fieldCount() == 1);
    CHECK(s.fieldHost(0).spec().cell_type == core::CellType::U8);
    CHECK(s.fieldHost(0).spec().width == spec.width);

    // A field starts at zero, and the rule builds it from the state. Ten
    // generations of a dead grid is ten units of energy everywhere.
    for (int i = 0; i < 10; ++i) s.step();
    s.syncToHost();
    for (uint8_t v : s.fieldHost(0).current()) CHECK(v == 10);
    // Which is not yet past ten, so nothing is alive.
    for (uint8_t v : s.host().current()) CHECK(v == 0);

    // The eleventh generation reads an energy of ten, which is not greater than
    // ten either, so the twelfth is where the grid lights.
    s.step();
    s.step();
    s.syncToHost();
    for (uint8_t v : s.host().current()) CHECK(v == 1);
}

TEST_CASE("switching paths mid-run carries the fields with the state", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    // setPath is the one place the two authority rules meet, and a sync that
    // moved the state without the fields would not show up until the next
    // commit wrote stale field bytes back over live ones.
    const core::GridSpec spec = spec2d(24, 16);
    const rule::RuleIR ir = simFieldRule();

    auto pure = sim::Simulation::create(spec, ir, sim::Path::Cpu, 7u);
    REQUIRE(std::holds_alternative<sim::Simulation>(pure));
    sim::Simulation& a = std::get<sim::Simulation>(pure);
    a.fillRandom(std::vector<double>{0.5, 0.5, 0.0, 0.0});

    auto mixed = sim::Simulation::create(spec, ir, sim::Path::Cpu, 7u);
    REQUIRE(std::holds_alternative<sim::Simulation>(mixed));
    sim::Simulation& b = std::get<sim::Simulation>(mixed);
    b.fillRandom(std::vector<double>{0.5, 0.5, 0.0, 0.0});

    for (int i = 0; i < 20; ++i) a.step();
    for (int i = 0; i < 7; ++i)  b.step();
    REQUIRE_FALSE(b.setPath(sim::Path::Gpu).has_value());
    for (int i = 0; i < 6; ++i)  b.step();
    REQUIRE_FALSE(b.setPath(sim::Path::Cpu).has_value());
    for (int i = 0; i < 7; ++i)  b.step();

    a.syncToHost();
    b.syncToHost();
    CHECK(bytesOf(a.host().current()) == bytesOf(b.host().current()));
    CHECK(bytesOf(a.fieldHost(0).current()) == bytesOf(b.fieldHost(0).current()));
}

TEST_CASE("a session round-trips a field rule's contents", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(20, 12);
    auto made = sim::Simulation::create(spec, simFieldRule(), sim::Path::Gpu, 3u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    s.fillRandom(std::vector<double>{0.6, 0.4, 0.0, 0.0});
    for (int i = 0; i < 30; ++i) s.step();

    const sim::Session saved = s.session();
    REQUIRE(saved.fields.size() == 1);
    CHECK(saved.fields[0].name == "energy");
    CHECK(saved.fields[0].cell_type == core::CellType::U8);
    CHECK(saved.fields[0].current.size() == spec.cellCount());

    // Through the text, because that is what a file is.
    const std::string text = sim::sessionToJson(saved);
    CHECK(text.find("\"fields\"") != std::string::npos);
    auto parsed = sim::sessionFromJson(text);
    REQUIRE(std::holds_alternative<sim::Session>(parsed));
    const sim::Session& back = std::get<sim::Session>(parsed);
    REQUIRE(back.fields.size() == 1);
    CHECK(back.fields[0].name == "energy");
    CHECK(back.fields[0].current == saved.fields[0].current);

    // And resumed, which is the thing the file is for.
    auto resumed = sim::Simulation::resume(back, sim::Path::Gpu);
    REQUIRE(std::holds_alternative<sim::Simulation>(resumed));
    sim::Simulation& r = std::get<sim::Simulation>(resumed);
    REQUIRE(r.fieldCount() == 1);
    r.syncToHost();
    s.syncToHost();
    CHECK(bytesOf(r.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
    CHECK(bytesOf(r.host().current()) == bytesOf(s.host().current()));

    // Stepping on from there stays in step with the run it came from.
    for (int i = 0; i < 10; ++i) { s.step(); r.step(); }
    s.syncToHost();
    r.syncToHost();
    CHECK(bytesOf(r.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
    CHECK(bytesOf(r.host().current()) == bytesOf(s.host().current()));
}

TEST_CASE("replay rebuilds a field from zero", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    // A field is not in the journal and has no initial buffer in the file: it
    // starts at zero and the rule writes it from the state, so replay has to
    // arrive at the same field the run did by doing the same arithmetic.
    const core::GridSpec spec = spec2d(20, 12);
    auto made = sim::Simulation::create(spec, simFieldRule(), sim::Path::Gpu, 11u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    s.fillRandom(std::vector<double>{0.5, 0.5, 0.0, 0.0});
    for (int i = 0; i < 25; ++i) s.step();

    sim::Session saved = s.session();
    saved.current.clear();          // force the replay path rather than resume
    saved.fields.clear();
    saved.streamA.reset();

    auto rebuilt = sim::Simulation::replay(saved, sim::Simulation::ReplayTarget{25}, sim::Path::Gpu);
    REQUIRE(std::holds_alternative<sim::Simulation>(rebuilt));
    sim::Simulation& r = std::get<sim::Simulation>(rebuilt);
    r.syncToHost();
    s.syncToHost();
    CHECK(r.generation() == s.generation());
    CHECK(bytesOf(r.host().current()) == bytesOf(s.host().current()));
    CHECK(bytesOf(r.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
}

TEST_CASE("a session whose fields disagree with its rule is refused", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(8, 8);
    auto made = sim::Simulation::create(spec, simFieldRule(), sim::Path::Cpu, 5u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    std::get<sim::Simulation>(made).step();
    const sim::Session good = std::get<sim::Simulation>(made).session();

    SECTION("a renamed field") {
        sim::Session s = good;
        s.fields[0].name = "not-energy";
        const auto r = sim::Simulation::resume(s, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<core::Error>(r));
        CHECK(std::get<core::Error>(r).message.find("not-energy") != std::string::npos);
    }
    SECTION("a missing field") {
        sim::Session s = good;
        s.fields.clear();
        const auto r = sim::Simulation::resume(s, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<core::Error>(r));
        CHECK(std::get<core::Error>(r).message.find("declares 1") != std::string::npos);
    }
    SECTION("a field of the wrong length") {
        sim::Session s = good;
        s.fields[0].current.pop_back();
        const auto r = sim::Simulation::resume(s, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<core::Error>(r));
        CHECK(std::get<core::Error>(r).message.find("wants") != std::string::npos);
    }
}

TEST_CASE("a rule without fields writes the session it always wrote", "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    // The additive claim on this side of it: no `fields` key at all, so a file
    // written before F-031 and one written after are the same bytes.
    const core::GridSpec spec = spec2d(12, 12);
    auto made = sim::Simulation::create(spec, *rule::parseDsl("B3/S23").ir, sim::Path::Cpu, 2u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    s.fillRandom(std::vector<double>{0.5, 0.5});
    s.step();

    const sim::Session saved = s.session();
    CHECK(saved.fields.empty());
    CHECK(s.fieldCount() == 0);
    const std::string text = sim::sessionToJson(saved);
    CHECK(text.find("\"fields\"") == std::string::npos);

    auto parsed = sim::sessionFromJson(text);
    REQUIRE(std::holds_alternative<sim::Session>(parsed));
    CHECK(std::get<sim::Session>(parsed).fields.empty());
}

TEST_CASE("a rule change keeps a field it redeclares and zeroes one it does not",
          "[sim][fields][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(8, 8);
    auto made = sim::Simulation::create(spec, simFieldRule(), sim::Path::Cpu, 9u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    for (int i = 0; i < 5; ++i) s.step();
    REQUIRE(s.fieldHost(0).current()[0] == 5);

    SECTION("the same list keeps what the fields hold") {
        // The rule's transition changes and its field list does not, which is
        // the shape every rule mutation has: the bookkeeping survives.
        rule::RuleIR ir = simFieldRule();
        Build b;
        const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
        const uint32_t two  = b.lit(2);
        const uint32_t rich = b.node(ExprOp::Gt, own, two);
        const uint32_t one  = b.lit(1);
        const uint32_t self = b.node(ExprOp::Self);
        b.node(ExprOp::Select, rich, one, self);
        ir.transition = b.e;
        REQUIRE_FALSE(s.setRule(ir).has_value());
        CHECK(s.fieldCount() == 1);
        CHECK(s.fieldHost(0).current()[0] == 5);
    }
    SECTION("a different list starts again from zero") {
        // Carrying the old bytes across a field that was retyped would be a
        // guess about what they mean.
        rule::RuleIR ir = simFieldRule();
        ir.fields[0].cell_type = core::CellType::F32;
        Build b;
        const uint32_t own  = b.node(ExprOp::FieldSelf, 0);
        const uint32_t half = b.flit(0.5f);
        b.node(ExprOp::Mul, own, half);
        ir.fields[0].write = b.e;
        // The transition read that field as an integer, which it no longer is,
        // so it goes back to reading the state.
        Expression plain;
        plain.nodes = {{ExprOp::Self}};
        ir.transition = plain;
        const auto err = s.setRule(ir);
        if (err) FAIL(err->message);
        REQUIRE(s.fieldCount() == 1);
        CHECK(s.fieldHost(0).spec().cell_type == core::CellType::F32);
        for (uint8_t v : s.fieldHost(0).current()) CHECK(v == 0);
    }
}

TEST_CASE("the pattern editor's pad refuses a field rule", "[sim][fields][scratch]") {
    // BUG-022. The pad is one grid and the editor opens it with whatever rule is
    // running, which since F-031 step 4 can declare fields. `cpuStep` asserts it
    // has a buffer pair apiece and a Release build does not check, so without
    // this the pad read past the end of nothing. Refused where it can be said.
    rule::RuleIR ir = base();
    rule::Field f;
    f.name = "energy";
    ir.fields.push_back(f);

    const core::GridSpec spec = spec2d(16, 16);
    auto made = sim::Scratch::make(spec, ir);
    REQUIRE(std::holds_alternative<sim::PatternError>(made));
    CHECK(std::get<sim::PatternError>(made).message.find("states only") != std::string::npos);

    // And on a pad that already exists, which is the path the editor takes when
    // the running rule changes under it.
    auto plain = sim::Scratch::make(spec, *rule::parseDsl("B3/S23").ir);
    REQUIRE(std::holds_alternative<sim::Scratch>(plain));
    sim::Scratch& pad = std::get<sim::Scratch>(plain);
    const auto err = pad.setRule(ir);
    REQUIRE(err.has_value());
    CHECK(err->message.find("states only") != std::string::npos);
    CHECK(pad.rule().fields.empty());   // refused whole
}

// --- Seeding the resource (F-032 step 2) -------------------------------------

namespace {

// A resource rule: the capacity is the world's shape and the store is drawn down
// where a cell is alive. Nothing regenerates it yet — that is step 3 — so what is
// under test here is the seed, its journal event and its replay.
rule::RuleIR resourceRule() {
    rule::RuleIR ir = base();
    ir.states = 2;

    rule::Field store;
    store.name = "food";
    store.cell_type = core::CellType::F32;
    {
        Build b;
        const uint32_t self  = b.node(ExprOp::Self);
        const uint32_t one   = b.lit(1);
        const uint32_t alive = b.node(ExprOp::Eq, self, one);
        const uint32_t here  = b.node(ExprOp::FieldSelf, 0);
        const uint32_t rate  = b.flit(0.75f);
        const uint32_t eaten = b.node(ExprOp::Mul, here, rate);
        b.node(ExprOp::Select, alive, eaten, here);
        store.write = b.e;
    }

    rule::Field capacity;
    capacity.name = "fertility";
    capacity.cell_type = core::CellType::F32;   // no write: the world's shape

    ir.fields = {store, capacity};
    ir.resource = rule::Resource{0, 1};

    Build t;
    const uint32_t here = t.node(ExprOp::FieldSelf, 0);
    const uint32_t half = t.flit(0.5f);
    const uint32_t rich = t.node(ExprOp::Gt, here, half);
    const uint32_t one  = t.lit(1);
    const uint32_t self = t.node(ExprOp::Self);
    t.node(ExprOp::Select, rich, one, self);
    ir.transition = t.e;
    return ir;
}

std::vector<float> floatsOf(std::span<const uint8_t> bytes) {
    std::vector<float> out(bytes.size() / sizeof(float));
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

}  // namespace

TEST_CASE("seeding a resource fills the capacity and starts the store at it", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(48, 32);
    auto made = sim::Simulation::create(spec, resourceRule(), sim::Path::Cpu, 21u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    REQUIRE(s.fieldCount() == 2);

    // Before the seed both are zero: a field starts at zero and nothing else
    // has touched them.
    for (uint8_t b : s.fieldHost(1).current()) CHECK(b == 0);

    sim::NoiseParams params;
    params.frequency = 5;
    params.octaves = 3;
    params.low = 0.2f;
    params.high = 1.0f;
    REQUIRE_FALSE(s.seedResource(params).has_value());

    const auto capacity = floatsOf(s.fieldHost(1).current());
    const auto store    = floatsOf(s.fieldHost(0).current());
    REQUIRE(capacity.size() == spec.cellCount());
    // The world begins full, which is the only starting point that needs no
    // second number to justify it.
    CHECK(store == capacity);
    CHECK(*std::min_element(capacity.begin(), capacity.end()) >= params.low);
    CHECK(*std::max_element(capacity.begin(), capacity.end()) <= params.high);
    // Patchy, not uniform — the reason the feature exists.
    CHECK(*std::max_element(capacity.begin(), capacity.end())
          - *std::min_element(capacity.begin(), capacity.end()) > 0.2f);

    // Journalled, so a session replays it.
    REQUIRE(s.journal().size() == 1);
    const auto* ev = std::get_if<sim::EvSeedResource>(&s.journal().front().body);
    REQUIRE(ev != nullptr);
    CHECK(ev->params == params);
}

TEST_CASE("a rule with no resource has nothing to seed", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    auto made = sim::Simulation::create(spec2d(16, 16), *rule::parseDsl("B3/S23").ir, sim::Path::Cpu, 1u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);
    const auto err = s.seedResource(sim::NoiseParams{});
    REQUIRE(err.has_value());
    CHECK(err->message.find("no resource") != std::string::npos);
    CHECK(s.journal().empty());   // a refusal journals nothing
}

TEST_CASE("the seed survives a session and a replay", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    const core::GridSpec spec = spec2d(40, 24);
    auto made = sim::Simulation::create(spec, resourceRule(), sim::Path::Cpu, 17u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);

    sim::NoiseParams params;
    params.frequency = 6;
    params.octaves = 2;
    params.low = 0.1f;
    params.high = 0.9f;
    REQUIRE_FALSE(s.seedResource(params).has_value());
    // A fill after the seed, so that a replay drawing the wrong number of values
    // for the noise would put the *grid* wrong too and not only the field.
    s.fillRandom(std::vector<double>{0.4});
    for (int i = 0; i < 5; ++i) s.step();

    const sim::Session saved = s.session();
    // The event is in the journal and the buffer is not the reason it replays:
    // the parameters travel and stream A re-draws them.
    const std::string text = sim::sessionToJson(saved);
    CHECK(text.find("seed_resource") != std::string::npos);
    auto parsed = sim::sessionFromJson(text);
    REQUIRE(std::holds_alternative<sim::Session>(parsed));
    sim::Session back = std::get<sim::Session>(parsed);

    SECTION("resumed from the stored state") {
        auto resumed = sim::Simulation::resume(back, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<sim::Simulation>(resumed));
        sim::Simulation& r = std::get<sim::Simulation>(resumed);
        CHECK(bytesOf(r.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
        CHECK(bytesOf(r.fieldHost(1).current()) == bytesOf(s.fieldHost(1).current()));
    }
    SECTION("rebuilt by replay, which re-draws the noise") {
        back.current.clear();
        back.fields.clear();
        back.streamA.reset();
        auto rebuilt = sim::Simulation::replay(back, sim::Simulation::ReplayTarget{5}, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<sim::Simulation>(rebuilt));
        sim::Simulation& r = std::get<sim::Simulation>(rebuilt);
        CHECK(r.generation() == s.generation());
        // The capacity is not in the file at all; it is here because the same
        // draws were made from stream A in the same order.
        CHECK(bytesOf(r.fieldHost(1).current()) == bytesOf(s.fieldHost(1).current()));
        CHECK(bytesOf(r.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
        CHECK(bytesOf(r.host().current()) == bytesOf(s.host().current()));
    }
    SECTION("a seed event whose rule cannot be seeded is an error, not a shrug") {
        // A seed that silently did nothing would consume no draws and leave every
        // later draw one place out, so the run would diverge with nothing saying
        // so (AV-006). Simulated by replacing the rule with one that has no
        // resource, which a hand-edited session could do.
        back.lineage.front().ir = *rule::parseDsl("B3/S23").ir;
        back.rule = back.lineage.front().ir;
        back.current.clear();
        back.fields.clear();
        back.streamA.reset();
        auto rebuilt = sim::Simulation::replay(back, sim::Simulation::ReplayTarget{5}, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<core::Error>(rebuilt));
        CHECK(std::get<core::Error>(rebuilt).message.find("no resource") != std::string::npos);
    }
}

TEST_CASE("the resource regenerates identically on both paths", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    // The engine's half of the resource is a second writer of one field, so this
    // is the case that says the oracle and the generated GLSL are twins. Run
    // through a Simulation rather than the two steppers directly, because the
    // ordering under test — draw-down, regeneration, diffusion, clamp — is the
    // engine's and this is where it is assembled.
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero);
    const bool diffusing = GENERATE(false, true);

    rule::RuleIR ir = resourceRule();
    ir.boundary = boundary;

    const core::GridSpec spec = spec2d(37, 23);   // not a multiple of the workgroup
    sim::ResourceParams params;
    params.regen = 0.05f;
    params.minSeed = 0.002f;
    params.diffusion = diffusing ? 0.25f : 0.0f;

    auto build = [&](sim::Path path) {
        auto made = sim::Simulation::create(spec, ir, path, 31u);
        REQUIRE(std::holds_alternative<sim::Simulation>(made));
        return std::move(std::get<sim::Simulation>(made));
    };
    sim::Simulation cpu = build(sim::Path::Cpu);
    sim::Simulation gpu = build(sim::Path::Gpu);

    for (sim::Simulation* s : {&cpu, &gpu}) {
        REQUIRE_FALSE(s->setResource(params).has_value());
        sim::NoiseParams noise;
        noise.frequency = 5;
        noise.low = 0.1f;
        REQUIRE_FALSE(s->seedResource(noise).has_value());
        s->fillRandom(std::vector<double>{0.45});
    }

    for (int i = 0; i < 200; ++i) { cpu.step(); gpu.step(); }
    cpu.syncToHost();
    gpu.syncToHost();

    CHECK(bytesOf(cpu.host().current()) == bytesOf(gpu.host().current()));
    // Bitwise on the resource too, which is the point: `precise`, no divide and
    // the subnormal flush exist so these are equal and not merely close (AV-015).
    CHECK(bytesOf(cpu.fieldHost(0).current()) == bytesOf(gpu.fieldHost(0).current()));
    CHECK(bytesOf(cpu.fieldHost(1).current()) == bytesOf(gpu.fieldHost(1).current()));

    // And it is a live world rather than an empty one, or the comparison above is
    // of two identical nothings — the mistake AV-015's own history records.
    const auto store = floatsOf(cpu.fieldHost(0).current());
    const double total = std::accumulate(store.begin(), store.end(), 0.0);
    CHECK(total > 0.0);
    CHECK(*std::max_element(store.begin(), store.end()) > 0.0f);
}

TEST_CASE("diffusion is refused against a mirror boundary", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    // Measured, not argued: in 1D with five cells and D = 0.5, a single unit at
    // cell 1 diffuses to a total of 1.25 under a mirror boundary. An edge cell
    // counts its inward neighbour twice while that neighbour counts it once, so
    // the exchange creates material rather than moving it — and a source that is
    // not regeneration is the one thing AV-018 forbids.
    rule::RuleIR ir = resourceRule();
    ir.boundary = rule::Boundary::Mirror;
    auto made = sim::Simulation::create(spec2d(16, 16), ir, sim::Path::Cpu, 3u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);

    sim::ResourceParams params;
    params.diffusion = 0.25f;
    const auto err = s.setResource(params);
    REQUIRE(err.has_value());
    CHECK(err->message.find("mirror") != std::string::npos);
    CHECK(s.resource().diffusion == 0.0f);   // refused whole

    // Without diffusion a mirror boundary is fine: regeneration is per-site.
    params.diffusion = 0.0f;
    params.regen = 0.1f;
    CHECK_FALSE(s.setResource(params).has_value());
    CHECK(s.resource().regen == 0.1f);
}

TEST_CASE("the resource's controls travel in the session", "[sim][resource][gpu]") {
    GlContext gl;
    requireGl(gl);

    // They are not rule text, so nothing about them is in the IR or its hash
    // (D-024). That makes the session the only place they can be recorded, and a
    // replay that ran with the defaults would be a different world.
    auto made = sim::Simulation::create(spec2d(24, 16), resourceRule(), sim::Path::Cpu, 13u);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    sim::Simulation& s = std::get<sim::Simulation>(made);

    sim::ResourceParams params;
    params.regen = 0.07f;
    params.minSeed = 0.003f;
    params.diffusion = 0.4f;
    REQUIRE_FALSE(s.setResource(params).has_value());
    REQUIRE_FALSE(s.seedResource(sim::NoiseParams{}).has_value());
    for (int i = 0; i < 12; ++i) s.step();

    const sim::Session saved = s.session();
    CHECK(saved.resource == params);
    const std::string text = sim::sessionToJson(saved);
    CHECK(text.find("\"resource\"") != std::string::npos);

    auto parsed = sim::sessionFromJson(text);
    REQUIRE(std::holds_alternative<sim::Session>(parsed));
    sim::Session back = std::get<sim::Session>(parsed);
    CHECK(back.resource == params);

    SECTION("resumed") {
        auto r = sim::Simulation::resume(back, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<sim::Simulation>(r));
        sim::Simulation& resumed = std::get<sim::Simulation>(r);
        CHECK(resumed.resource() == params);
        CHECK(bytesOf(resumed.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
    }
    SECTION("replayed, which re-applies the control from the journal") {
        back.current.clear();
        back.fields.clear();
        back.streamA.reset();
        back.resource = sim::ResourceParams{};   // the defaults, so only the journal can restore it
        auto r = sim::Simulation::replay(back, sim::Simulation::ReplayTarget{12}, sim::Path::Cpu);
        REQUIRE(std::holds_alternative<sim::Simulation>(r));
        sim::Simulation& rebuilt = std::get<sim::Simulation>(r);
        CHECK(rebuilt.resource() == params);
        CHECK(bytesOf(rebuilt.fieldHost(0).current()) == bytesOf(s.fieldHost(0).current()));
    }
}

TEST_CASE("the resource's books balance", "[sim][resource]") {
    // AV-018's detection, on the oracle. The identity is exact by construction —
    // after minus before equals regenerated plus diffused plus clamped minus
    // consumed — so what this actually tests is that every term is counted and
    // that nothing else moves the quantity.
    rule::RuleIR ir = resourceRule();
    ir.boundary = rule::Boundary::Wrap;
    const rule::CompiledRule rule = compiled(ir);

    const core::GridSpec spec = spec2d(32, 24);
    core::HostGrid host(spec);
    aether::test::HostFields fields(rule, spec.cellCount());

    sim::Pcg32 stream(77);
    std::vector<float> noise(spec.cellCount());
    sim::NoiseParams np;
    np.low = 0.2f;
    sim::fillNoise(spec, np, stream, noise);
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        fields.setF32(0, i, noise[i]);
        fields.setF32(1, i, noise[i]);
        host.current()[i] = static_cast<uint8_t>((i * 7u) % 3u == 0u ? 1 : 0);
    }

    sim::ResourceParams params;
    params.regen = 0.03f;
    params.minSeed = 0.001f;
    params.diffusion = 0.2f;

    auto totalResource = [&] {
        const auto v = floatsOf(fields.raw(0));
        return std::accumulate(v.begin(), v.end(), 0.0);
    };

    sim::GridLedger books;
    const double opening = totalResource();
    for (int g = 0; g < 1000; ++g) {
        cpuStep(rule, spec, host.current(), host.next(), static_cast<uint64_t>(g), {},
                fields.reads(), fields.writes(), params, &books);
        host.swap();
        fields.swap();
    }
    const double closing = totalResource();

    const double accounted = books.regenerated + books.diffused + books.clamped - books.consumed;
    const double measured = closing - opening;
    // A thousand generations of float addition over 768 cells, summed in double:
    // the tolerance is for the summation order, not for a leak. A leak shows up as
    // a proportion of the throughput rather than as a rounding error.
    const double throughput = books.regenerated + books.consumed;
    REQUIRE(throughput > 1.0);            // the world actually moved
    CHECK(std::fabs(accounted - measured) < 1e-3 * throughput);

    // And the sources really are only the ones named. A rule that raised the
    // resource would show as a negative consumption; this one only ever takes.
    CHECK(books.consumed > 0.0);
    CHECK(books.regenerated > 0.0);
    // Under wrap, diffusion moves material without creating or destroying it.
    CHECK(std::fabs(books.diffused) < 1e-3 * throughput);
}

TEST_CASE("diffusion leaks at a zero boundary and the ledger says so", "[sim][resource]") {
    // A world with an edge loses material at the edge: a cell there diffuses into
    // nothing. That is the honest behaviour rather than a defect, so it is counted
    // as a sink and visible, not suppressed.
    // A rule that neither draws down nor turns anything on, so diffusion is
    // provably the only thing moving the quantity. The first version of this test
    // reused the rule above and proved nothing of the sort: its transition lights
    // a cell wherever the resource is high, so seeding the grid full made
    // everything alive and the cells ate 535 units while the test claimed none.
    rule::RuleIR ir = base();
    ir.boundary = rule::Boundary::Zero;
    rule::Field inert;
    inert.name = "food";
    inert.cell_type = core::CellType::F32;      // no write: no draw-down
    rule::Field ceiling;
    ceiling.name = "fertility";
    ceiling.cell_type = core::CellType::F32;
    ir.fields = {inert, ceiling};
    ir.resource = rule::Resource{0, 1};
    Expression still;
    still.nodes = {{ExprOp::Self}};
    ir.transition = still;
    const rule::CompiledRule rule = compiled(ir);

    const core::GridSpec spec = spec2d(24, 24);
    core::HostGrid host(spec);
    aether::test::HostFields fields(rule, spec.cellCount());
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        fields.setF32(0, i, 1.0f);
        fields.setF32(1, i, 1.0f);
    }
    sim::ResourceParams params;
    params.regen = 0.0f;
    params.minSeed = 0.0f;
    params.diffusion = 0.3f;

    sim::GridLedger books;
    for (int g = 0; g < 20; ++g) {
        cpuStep(rule, spec, host.current(), host.next(), static_cast<uint64_t>(g), {},
                fields.reads(), fields.writes(), params, &books);
        host.swap();
        fields.swap();
    }
    // Negative: the grid gave material to its edge and got none back.
    CHECK(books.diffused < 0.0);
    CHECK(books.consumed == 0.0);       // nothing alive drew any down
    CHECK(books.regenerated == 0.0);    // and nothing put any back
    const auto store = floatsOf(fields.raw(0));
    CHECK(std::accumulate(store.begin(), store.end(), 0.0) < static_cast<double>(spec.cellCount()));
}

TEST_CASE("the bitwise operators step on the CPU", "[sim][genome]") {
    // Isolates the oracle from the shader: if this passes and the equivalence
    // sweep crashes, the fault is on the GPU side and vice versa.
    rule::RuleIR ir = base();
    rule::Field bits;
    bits.name = "bits";
    bits.cell_type = core::CellType::U32;
    ir.fields = {bits};
    ir.genome = rule::Genome{0};
    Expression e;
    e.nodes = {{ExprOp::FieldSelf, 0},
               {ExprOp::Count, 1},
               {ExprOp::Shr, 0, 1},
               {ExprOp::IntLiteral, 0, 0, 0, 1},
               {ExprOp::BitAnd, 2, 3}};
    ir.transition = e;

    const rule::CompiledRule rule = compiled(ir);
    REQUIRE(rule.fields.size() == 1);
    CHECK(rule.fields[0].cell_type == core::CellType::U32);

    const core::GridSpec spec = spec2d(16, 16);
    core::HostGrid host(spec);
    aether::test::HostFields fields(rule, spec.cellCount());
    CHECK(fields.raw(0).size() == spec.cellCount() * 4);

    // Conway's mask: B3 is bit 3, S2 and S3 are bits 11 and 12.
    const uint32_t life = (1u << 3) | (1u << (9 + 2)) | (1u << (9 + 3));
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        std::memcpy(const_cast<uint8_t*>(fields.raw(0).data()) + i * 4, &life, 4);
        host.current()[i] = static_cast<uint8_t>((i * 5u) % 3u == 0u ? 1 : 0);
    }
    for (int g = 0; g < 10; ++g) {
        cpuStep(rule, spec, host.current(), host.next(), static_cast<uint64_t>(g), {},
                fields.reads(), fields.writes());
        host.swap();
        fields.swap();
    }
    // It ran, and the genome was carried rather than lost.
    uint32_t back = 0;
    std::memcpy(&back, fields.raw(0).data(), 4);
    CHECK(back == life);
}
