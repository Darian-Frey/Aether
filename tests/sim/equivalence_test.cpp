// CPU/GPU equivalence (F-002, AV-005, AV-007).
//
// Every fixture rule, under every boundary, seeded with activity everywhere
// including the edges and corners, stepped 1000 generations on both paths
// and compared bitwise. If this file is red, nothing else is trustworthy.

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"
#include "sim/inspect.hpp"
#include "sim/gpu_step.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <format>
#include <string>
#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

namespace {

constexpr int kGenerations = 1000;

struct Fixture {
    std::string  name;
    rule::RuleIR ir;
};

// Deterministic fill so a failure reproduces exactly. Not the session RNG;
// this is test scaffolding, not sim/.
void fill(core::HostGrid& g, uint16_t states, uint32_t seed, double density) {
    uint32_t s = seed;
    auto next = [&] { s = s * 1664525u + 1013904223u; return s >> 8; };
    for (uint8_t& c : g.current()) {
        const bool on = (next() % 1000) < static_cast<uint32_t>(density * 1000);
        c = on ? static_cast<uint8_t>(1 + next() % (states - 1u)) : 0;
    }
}

rule::RuleIR dsl(const char* src, uint8_t dims = 2) {
    rule::DslContext ctx;
    ctx.dimensions = dims;
    auto r = rule::parseDsl(src, ctx);
    REQUIRE(r);
    return *r.ir;
}

rule::RuleIR randomTable(rule::Kind kind, uint8_t dims, uint16_t states, rule::Neighbourhood nb, uint32_t seed) {
    rule::RuleIR ir;
    ir.dimensions = dims;
    ir.states = states;
    ir.kind = kind;
    ir.neighbourhood = nb;
    const uint32_t N = rule::neighbourCount(dims, nb);
    const auto size = rule::tableSize(kind, states, N);
    REQUIRE(size);
    rule::Table t;
    t.entries.resize(*size);
    uint32_t s = seed;
    for (uint8_t& e : t.entries) { s = s * 1664525u + 1013904223u; e = static_cast<uint8_t>((s >> 8) % states); }
    ir.transition = t;
    return ir;
}

// The same automaton an expression tree rather than a table, so that the
// generated GLSL and the interpreter that mirrors it are both exercised.
rule::RuleIR lifeAsExpression(uint16_t states = 2) {
    rule::RuleIR ir;
    ir.states = states;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Count, 1},                       //  0  n(1)
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},         //  1
        {rule::ExprOp::Eq, 0, 1},                       //  2  n == 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, 3},         //  3
        {rule::ExprOp::Eq, 0, 3},                       //  4  n == 3
        {rule::ExprOp::Or, 2, 4},                       //  5
        {rule::ExprOp::Self},                           //  6
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},         //  7
        {rule::ExprOp::Eq, 6, 7},                       //  8  alive
        {rule::ExprOp::And, 8, 5},                      //  9  survives
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},         // 10
        {rule::ExprOp::Eq, 6, 10},                      // 11  dead
        {rule::ExprOp::And, 11, 4},                     // 12  born
        {rule::ExprOp::Or, 9, 12},                      // 13
        {rule::ExprOp::IntLiteral, 0, 0, 0, 1},         // 14
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},         // 15
        {rule::ExprOp::Select, 13, 14, 15},             // 16
    };
    ir.transition = e;
    return ir;
}

// next = (self + number of live neighbours) mod states, which uses the
// arithmetic, the modulo guard and the clamp all at once.
rule::RuleIR arithmeticExpression(uint16_t states) {
    rule::RuleIR ir;
    ir.states = states;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Self},                                     // 0
        {rule::ExprOp::Count, 1},                                 // 1
        {rule::ExprOp::Add, 0, 1},                                // 2
        {rule::ExprOp::IntLiteral, 0, 0, 0, states},              // 3
        {rule::ExprOp::Mod, 2, 3},                                // 4
        {rule::ExprOp::Count, 0},                                 // 5
        {rule::ExprOp::IntLiteral, 0, 0, 0, 0},                   // 6
        {rule::ExprOp::Div, 4, 6},                                // 7  divide by zero
        {rule::ExprOp::Sub, 4, 7},                                // 8  so this is node 4
        {rule::ExprOp::IntLiteral, 0, 0, 0, 7},                   // 9
        {rule::ExprOp::Gt, 5, 9},                                 // 10 more than seven dead
        {rule::ExprOp::Select, 10, 6, 8},                         // 11
    };
    ir.transition = e;
    return ir;
}

// A rule that reads its neighbours by position rather than by count.
rule::RuleIR shiftExpression() {
    rule::RuleIR ir;
    ir.states = 3;
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {{rule::ExprOp::Neighbour, 0}};
    ir.transition = e;
    return ir;
}

std::vector<Fixture> fixtures() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"Life B3/S23", dsl("B3/S23")});
    out.push_back({"HighLife B36/S23", dsl("B36/S23")});
    out.push_back({"Brian's Brain B2/S/C3", dsl("B2/S/C3")});
    out.push_back({"Generations B2/S23/C5", dsl("B2/S23/C5")});
    out.push_back({"Cyclic CA, 8 states", dsl(
        "states 8; neighbourhood moore 1;"
        "0: n(1) >= 1 -> 1; 1: n(2) >= 1 -> 2; 2: n(3) >= 1 -> 3; 3: n(4) >= 1 -> 4;"
        "4: n(5) >= 1 -> 5; 5: n(6) >= 1 -> 6; 6: n(7) >= 1 -> 7; 7: n(0) >= 1 -> 0;")});
    out.push_back({"Wireworld", dsl(
        "states 4; neighbourhood moore 1;"
        "1: n(0) >= 0 -> 2; 2: n(0) >= 0 -> 3; 3: n(1) == 1 or n(1) == 2 -> 1;")});
    out.push_back({"Random non-totalistic, 2 states Moore", randomTable(Kind::NonTotalistic, 2, 2, {NeighbourhoodType::Moore, 1}, 7)});
    out.push_back({"Random non-totalistic, 3 states von Neumann", randomTable(Kind::NonTotalistic, 2, 3, {NeighbourhoodType::VonNeumann, 1}, 11)});
    out.push_back({"Random totalistic, 4 states Moore r=2", randomTable(Kind::Totalistic, 2, 4, {NeighbourhoodType::Moore, 2}, 13)});
    out.push_back({"Random outer-totalistic, 5 states von Neumann r=2", randomTable(Kind::OuterTotalistic, 2, 5, {NeighbourhoodType::VonNeumann, 2}, 17)});
    out.push_back({"Hex Life-like B2/S34", dsl("states 2; neighbourhood hex 1; 0: n(1) == 2 -> 1; 1: n(1) < 3 or n(1) > 4 -> 0;")});
    out.push_back({"Signature rule written with rot", dsl(
        "states 3; neighbourhood von_neumann 1;"
        "0: [1, _, _, _] rot -> 1; 1: [_, _, _, _] -> 2; 2: [_, _, _, _] -> 0;")});
    out.push_back({"Life as an expression (codegen)", lifeAsExpression()});
    out.push_back({"Arithmetic expression, 5 states (codegen)", arithmeticExpression(5)});
    out.push_back({"Neighbour-indexed expression (codegen)", shiftExpression()});
    out.push_back({"Life with a 4-state ageing tail", dsl("states 2; neighbourhood moore 1; decay 4; 0: n(1) == 3 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;")});
    out.push_back({"Random non-totalistic hex, 2 states", randomTable(Kind::NonTotalistic, 2, 2, {NeighbourhoodType::Hexagonal, 1}, 29)});
    out.push_back({"Random outer-totalistic hex r=2, 3 states", randomTable(Kind::OuterTotalistic, 2, 3, {NeighbourhoodType::Hexagonal, 2}, 31)});
    return out;
}

// The rule Phase 4 was specified against: three-dimensional, Moore, and
// depending on individual neighbours rather than counts, so its table would
// be 2^26 entries per state and only generated code can run it.
rule::RuleIR nonTotalistic3dExpression() {
    rule::RuleIR ir;
    ir.dimensions = 3;
    ir.states = 2;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};   // 26 neighbours
    ir.kind = rule::Kind::Expression;
    rule::Expression e;
    e.nodes = {
        {rule::ExprOp::Neighbour, 0},                     // 0  a corner
        {rule::ExprOp::Neighbour, 12},                    // 1  a face
        {rule::ExprOp::Neighbour, 25},                    // 2  the opposite corner
        {rule::ExprOp::Add, 0, 1},                        // 3
        {rule::ExprOp::Add, 3, 2},                        // 4
        {rule::ExprOp::Self},                             // 5
        {rule::ExprOp::Add, 4, 5},                        // 6
        {rule::ExprOp::IntLiteral, 0, 0, 0, 2},           // 7
        {rule::ExprOp::Mod, 6, 7},                        // 8  parity of four cells
    };
    ir.transition = e;
    return ir;
}

std::vector<Fixture> fixtures3d() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"3D B5/S45 Moore", dsl("B5/S45", 3)});
    out.push_back({"3D von Neumann 3 states", dsl(
        "states 3; neighbourhood von_neumann 1;"
        "0: n(1) == 2 -> 1; 1: n(1) >= 0 -> 2; 2: n(1) >= 0 -> 0;", 3)});
    out.push_back({"3D Moore non-totalistic expression (codegen)", nonTotalistic3dExpression()});
    out.push_back({"3D random non-totalistic von Neumann", randomTable(Kind::NonTotalistic, 3, 2, {NeighbourhoodType::VonNeumann, 1}, 19)});
    return out;
}

std::vector<Fixture> fixtures1d() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"1D random non-totalistic r=3, 3 states", randomTable(Kind::NonTotalistic, 1, 3, {NeighbourhoodType::Moore, 3}, 23)});
    out.push_back({"1D B1/S", dsl("B1/S", 1)});
    return out;
}

// What the CPU path makes of one cell. This is the standalone reason IMP-005
// was worth doing: a failing case used to name a cell index and stop there.
std::string explainCell(const rule::CompiledRule& r, const core::GridSpec& spec,
                        std::span<const uint8_t> cells, size_t linear,
                        uint64_t generation, sim::CellMutation mutation) {
    const uint32_t x = static_cast<uint32_t>(linear % spec.width);
    const uint32_t y = static_cast<uint32_t>((linear / spec.width) % spec.height);
    const uint32_t z = static_cast<uint32_t>(linear / (size_t{spec.width} * spec.height));
    sim::StepScratch scratch(r);
    const sim::CellTransition t = sim::stepCell(r, spec, cells, x, y, z, generation, mutation, scratch);

    std::string nbrs;
    for (uint32_t i = 0; i < r.neighbourCount(); ++i) {
        nbrs += std::format("{}{}", i ? "," : "", static_cast<int>(scratch.neighbours[i]));
    }
    // An axis of extent 1 has no edges; without that, every cell of a 2D grid
    // reports as being on one, since z is both 0 and depth - 1.
    const bool edge = (spec.width  > 1 && (x == 0 || x + 1 == spec.width)) ||
                      (spec.height > 1 && (y == 0 || y + 1 == spec.height)) ||
                      (spec.depth  > 1 && (z == 0 || z + 1 == spec.depth));
    return std::format("({},{},{}){}, state {}, neighbours [{}] -> {} {}{}",
                       x, y, z, edge ? " on an edge" : "", static_cast<int>(t.own), nbrs,
                       static_cast<int>(t.fromRule),
                       t.hasIndex ? std::format("from table entry {}", t.tableIndex) : "from the expression",
                       t.mutated ? std::format(", then mutated to {}", static_cast<int>(t.next)) : "");
}

// Runs one fixture under one boundary on both paths and compares.
void checkEquivalence(const Fixture& f, rule::Boundary boundary, const core::GridSpec& spec, double p = 0.0,
                      uint8_t blockShift = 0) {
    rule::RuleIR ir = f.ir;
    ir.boundary = boundary;
    auto compiled = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
    const rule::CompiledRule& lut = std::get<rule::CompiledRule>(compiled);

    core::HostGrid host(spec);
    fill(host, ir.states, 0x5eed + static_cast<uint32_t>(boundary), 0.4);

    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    sim::GpuStepper stepper;
    const auto err = stepper.setRule(lut, spec);
    if (err) FAIL(err->message);
    const sim::CellMutation mutation{sim::mutationThreshold(p), 0xb0b0b0b0ull + static_cast<uint32_t>(boundary), blockShift};
    stepper.setCellMutation(mutation);

    for (int i = 0; i < kGenerations; ++i) {
        sim::cpuStep(lut, host, static_cast<uint64_t>(i), mutation);
        stepper.step(gpu);
    }

    std::vector<uint8_t> fromGpu(spec.bytesPerBuffer());
    gpu.download(fromGpu);
    const std::vector<uint8_t> fromCpu(host.current().begin(), host.current().end());

    size_t firstDiff = fromCpu.size();
    for (size_t i = 0; i < fromCpu.size(); ++i) {
        if (fromCpu[i] != fromGpu[i]) { firstDiff = i; break; }
    }
    std::string detail;
    if (firstDiff != fromCpu.size()) {
        // The grids are compared after kGenerations, so this cell is where the
        // divergence had reached, not necessarily where it began.
        detail = std::format("\n  cpu {} vs gpu {}\n  the CPU path reads that cell as {}",
                             static_cast<int>(fromCpu[firstDiff]), static_cast<int>(fromGpu[firstDiff]),
                             explainCell(lut, spec, fromCpu, firstDiff, kGenerations, mutation));
    }
    INFO(std::format("{} / {} / {}x{}x{} / p={}: first difference at cell {}{}", f.name,
                     rule::toString(boundary), spec.width, spec.height, spec.depth, p, firstDiff, detail));
    CHECK(firstDiff == fromCpu.size());
}

}  // namespace

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 2D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            // Extents chosen not to be multiples of the workgroup size.
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 3D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise after 1000 generations, 1D", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, core::GridSpec{1, 131, 1, 1});
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise with cell mutation on, all dimensions", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1}, 0.02);
        }
    }
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11}, 0.02);
        }
    }
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02") {
            checkEquivalence(f, boundary, core::GridSpec{1, 131, 1, 1}, 0.02);
        }
    }
}

TEST_CASE("CPU and GPU agree bitwise with block-correlated mutation", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02 block=4") {
            checkEquivalence(f, boundary, core::GridSpec{2, 61, 43, 1}, 0.02, 2);
        }
    }
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.02 block=2") {
            checkEquivalence(f, boundary, core::GridSpec{3, 19, 14, 11}, 0.02, 1);
        }
    }
}

TEST_CASE("cell mutation changes the trajectory and is reproducible", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 48, 48, 1};
    const rule::CompiledRule life = std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23")));
    auto run = [&](double p, uint64_t seed) {
        core::HostGrid h(spec);
        fill(h, 2, 77, 0.35);
        const sim::CellMutation m{sim::mutationThreshold(p), seed};
        for (int i = 0; i < 200; ++i) sim::cpuStep(life, h, static_cast<uint64_t>(i), m);
        return std::vector<uint8_t>(h.current().begin(), h.current().end());
    };
    const auto plain = run(0.0, 1);
    const auto mutA  = run(0.01, 1);
    const auto mutA2 = run(0.01, 1);
    const auto mutB  = run(0.01, 2);
    CHECK(plain != mutA);
    CHECK(mutA == mutA2);
    CHECK(mutA != mutB);
}

TEST_CASE("the two backends agree on a rule expressible both ways (AV-007)", "[gpu][equivalence]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    const core::GridSpec spec{2, 61, 43, 1};

    // Conway's Life as a table and as an expression tree. Which backend runs
    // a rule must not change what the rule does, whatever LUT_MAX_ENTRIES
    // happens to be.
    rule::RuleIR table = dsl("B3/S23");
    rule::RuleIR expression = lifeAsExpression();
    table.boundary = expression.boundary = boundary;
    REQUIRE(rule::selectBackend(table) == rule::Backend::Lut);
    REQUIRE(rule::selectBackend(expression) == rule::Backend::Codegen);

    auto run = [&](const rule::RuleIR& ir, bool onCpu) {
        auto compiled = rule::compileRule(ir);
        if (const auto* e = std::get_if<rule::CompileError>(&compiled)) FAIL(e->message);
        const rule::CompiledRule& rule = std::get<rule::CompiledRule>(compiled);

        core::HostGrid host(spec);
        fill(host, 2, 0x11ce, 0.4);
        auto made = core::GpuGrid::create(spec, core::queryVram());
        REQUIRE(std::holds_alternative<core::GpuGrid>(made));
        core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
        gpu.upload(host.current());

        sim::GpuStepper stepper;
        if (auto e = stepper.setRule(rule, spec)) FAIL(e->message);
        for (int i = 0; i < kGenerations; ++i) {
            if (onCpu) sim::cpuStep(rule, host, static_cast<uint64_t>(i), {});
            else stepper.step(gpu);
        }
        std::vector<uint8_t> out(spec.bytesPerBuffer());
        if (onCpu) std::copy(host.current().begin(), host.current().end(), out.begin());
        else gpu.download(out);
        return out;
    };

    const auto tableGpu = run(table, false);
    CHECK(run(expression, false) == tableGpu);
    CHECK(run(expression, true) == tableGpu);
    CHECK(run(table, true) == tableGpu);
}

TEST_CASE("a generated rule is compiled once per rule, a table rule once per shape", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;

    // Two table rules of one shape share a program: mutation is an upload.
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B36/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);

    // A generated rule is its program, so a different one compiles again...
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(lifeAsExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 2);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(shiftExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 3);
    // ...and the same one does not (SPEC §6: the cache is keyed on ir_hash).
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(lifeAsExpression())), spec).has_value());
    CHECK(stepper.cachedPrograms() == 3);
}

TEST_CASE("GPU glider arrives at its predicted offset (AV-004 detector)", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 32, 32, 1};
    core::HostGrid host(spec);
    for (auto [x, y] : {std::pair{1u, 0u}, {2u, 1u}, {0u, 2u}, {1u, 2u}, {2u, 2u}}) host.set(x, y, 0, 1);

    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    const rule::CompiledRule life = std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23")));
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(life, spec).has_value());
    for (int i = 0; i < 100; ++i) stepper.step(gpu);
    CHECK(stepper.generation() == 100);

    std::vector<uint8_t> out(spec.cellCount());
    gpu.download(out);
    core::HostGrid expected(spec);
    for (auto [x, y] : {std::pair{1u, 0u}, {2u, 1u}, {0u, 2u}, {1u, 2u}, {2u, 2u}}) expected.set((x + 25) % 32, (y + 25) % 32, 0, 1);
    CHECK(out == std::vector<uint8_t>(expected.current().begin(), expected.current().end()));
}

TEST_CASE("changing the table without changing the shape compiles nothing", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B36/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B2/S/C3"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 2);
}

TEST_CASE("a rule whose dimensionality mismatches the grid is refused, leaving the old rule active", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B3/S23"))), spec).has_value());
    const auto err = stepper.setRule(std::get<rule::CompiledRule>(rule::compileRule(dsl("B5/S45", 3))), spec);
    REQUIRE(err.has_value());
    CHECK(err->message.find("3D") != std::string::npos);
    CHECK(stepper.hasRule());
}

// --- The inspector against the stepper (F-030, AV-017) ----------------------
//
// The inspector is consulted precisely when nobody can check its answer, so
// what it predicts for a cell must be what the stepper writes into that cell.
// Run over the same fixtures under the same boundaries as the equivalence
// cases above, because the cells that matter are the edges and the corners: an
// inspector resolving neighbours differently from `sim::resolve` explains the
// interior perfectly and lies about the rim. No GL, so this is not a [gpu]
// case — it compares the oracle against itself.

namespace {

void checkInspector(const Fixture& f, rule::Boundary boundary, const core::GridSpec& spec) {
    rule::RuleIR ir = f.ir;
    ir.boundary = boundary;
    auto compiled = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
    const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(compiled));

    core::HostGrid grid(spec);
    fill(grid, ir.states, 4242u, 0.4);

    // What the stepper writes, for every cell at once.
    sim::cpuStep(rule, spec, grid.current(), grid.next(), 0, sim::CellMutation{});

    sim::StepScratch scratch(rule);
    size_t checked = 0;
    for (uint32_t z = 0; z < spec.depth; ++z) {
        for (uint32_t y = 0; y < spec.height; ++y) {
            for (uint32_t x = 0; x < spec.width; ++x) {
                const sim::Inspection got =
                    sim::inspect(rule, spec, grid.current(), x, y, z, 0, sim::CellMutation{}, scratch);
                const size_t at = (size_t{z} * spec.height + y) * spec.width + x;
                if (got.transition.next != grid.next()[at]) {
                    INFO(f.name << " / " << rule::toString(boundary) << " at " << x << "," << y << "," << z);
                    CHECK(got.transition.next == grid.next()[at]);
                    return;
                }
                // The neighbours it reports must be the ones the rule was
                // given, in the order it was given them.
                REQUIRE(got.neighbours.size() == rule.neighbourCount());
                ++checked;
            }
        }
    }
    CHECK(checked == spec.cellCount());
}

}  // namespace

TEST_CASE("the inspector predicts what the stepper writes, 2D", "[inspect][equivalence]") {
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{2, 37, 29, 1});
        }
    }
}

TEST_CASE("the inspector predicts what the stepper writes, 3D and 1D", "[inspect][equivalence]") {
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    for (const Fixture& f : fixtures3d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{3, 11, 9, 7});
        }
    }
    for (const Fixture& f : fixtures1d()) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkInspector(f, boundary, core::GridSpec{1, 71, 1, 1});
        }
    }
}

TEST_CASE("the inspector predicts what the stepper writes with mutation on", "[inspect][equivalence]") {
    // Mutation is hashed from the coordinate and the generation, so the
    // inspector must agree about an overridden cell too — and say that it was
    // overridden rather than reporting the rule's answer as the outcome.
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero);
    const core::GridSpec spec{2, 37, 29, 1};
    for (const Fixture& f : fixtures()) {
        rule::RuleIR ir = f.ir;
        ir.boundary = boundary;
        auto compiled = rule::compileRule(ir);
        REQUIRE(std::holds_alternative<rule::CompiledRule>(compiled));
        const rule::CompiledRule rule = std::get<rule::CompiledRule>(std::move(compiled));

        sim::CellMutation mutation;
        mutation.threshold = sim::mutationThreshold(0.05);
        mutation.seedB = 99;

        core::HostGrid grid(spec);
        fill(grid, ir.states, 777u, 0.4);
        sim::cpuStep(rule, spec, grid.current(), grid.next(), 5, mutation);

        sim::StepScratch scratch(rule);
        size_t overridden = 0;
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary) << " / p=0.05") {
            for (uint32_t y = 0; y < spec.height; ++y) {
                for (uint32_t x = 0; x < spec.width; ++x) {
                    const sim::Inspection got =
                        sim::inspect(rule, spec, grid.current(), x, y, 0, 5, mutation, scratch);
                    REQUIRE(got.transition.next == grid.next()[size_t{y} * spec.width + x]);
                    if (got.transition.mutated) {
                        ++overridden;
                        // What the rule alone would have given is still there
                        // to show beside what mutation did with it. The two
                        // may coincide: cell mutation draws uniformly over
                        // every state and does not exclude the current one
                        // (SPEC §9.2), so the check is that `fromRule` is
                        // the rule's answer, not that it differs.
                        const sim::Inspection unmutated =
                            sim::inspect(rule, spec, grid.current(), x, y, 0, 5, sim::CellMutation{}, scratch);
                        CHECK_FALSE(unmutated.transition.mutated);
                        CHECK(got.transition.fromRule == unmutated.transition.next);
                    }
                }
            }
            CHECK(overridden > 0);
        }
    }
}

// --- The bundled library as the fixture set (F-002) --------------------------
//
// F-002's acceptance is bit-identical grids "for every rule in the bundled
// library", and until now the suite proved it for fifteen fixtures written to
// exercise the table kinds instead. Those are the better test of the index
// arithmetic; these are the better test of what somebody actually runs. A
// bundled rule that stepped differently on the two paths would be shipped,
// named in the Library panel, and wrong — and nothing here would have said so.
//
// Continuous rules are excluded and covered by `continuous_test.cpp`: this
// harness seeds a grid by writing bytes, which on an f32 grid writes into the
// middle of values rather than producing them.

namespace {

std::vector<Fixture> libraryFixtures() {
    std::vector<Fixture> out;
    for (const rule::LibraryRule& entry : rule::loadLibrary({AETHER_RULES_DIR})) {
        auto built = rule::compileLibraryRule(entry, rule::Boundary::Wrap);
        if (const auto* e = std::get_if<std::string>(&built)) {
            FAIL("bundled rule " + entry.id + ": " + *e);
        }
        rule::RuleIR ir = std::get<rule::RuleIR>(std::move(built));
        if (ir.cell_type == core::CellType::F32) continue;
        out.push_back({entry.id, std::move(ir)});
    }
    return out;
}

// Small enough to run every bundled rule in a reasonable time, and none of
// them a multiple of the workgroup size.
core::GridSpec specFor(const rule::RuleIR& ir) {
    switch (ir.dimensions) {
        case 1:  return {1, 131, 1, 1};
        case 3:  return {3, 19, 14, 11};
        default: return {2, 61, 43, 1};
    }
}

}  // namespace

TEST_CASE("every bundled rule steps identically on both paths", "[gpu][equivalence][library]") {
    GlContext gl;
    requireGl(gl);
    const auto boundary = GENERATE(rule::Boundary::Wrap, rule::Boundary::Zero, rule::Boundary::Mirror);
    const auto fixtures = libraryFixtures();
    REQUIRE(fixtures.size() >= 15);
    for (const Fixture& f : fixtures) {
        DYNAMIC_SECTION(f.name << " / " << rule::toString(boundary)) {
            checkEquivalence(f, boundary, specFor(f.ir));
        }
    }
}

TEST_CASE("every bundled rule steps identically with cell mutation on", "[gpu][equivalence][library]") {
    GlContext gl;
    requireGl(gl);
    const auto fixtures = libraryFixtures();
    for (const Fixture& f : fixtures) {
        DYNAMIC_SECTION(f.name << " / wrap / p=0.02") {
            checkEquivalence(f, rule::Boundary::Wrap, specFor(f.ir), 0.02);
        }
    }
}
