// CPU/GPU equivalence (F-002, AV-005, AV-007).
//
// Every fixture rule, under every boundary, seeded with activity everywhere
// including the edges and corners, stepped 1000 generations on both paths
// and compared bitwise. If this file is red, nothing else is trustworthy.

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "rule/dsl.hpp"
#include "rule/lut.hpp"
#include "sim/cpu_step.hpp"
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
    return out;
}

std::vector<Fixture> fixtures3d() {
    using rule::Kind;
    using rule::NeighbourhoodType;
    std::vector<Fixture> out;
    out.push_back({"3D B5/S45 Moore", dsl("B5/S45", 3)});
    out.push_back({"3D von Neumann 3 states", dsl(
        "states 3; neighbourhood von_neumann 1;"
        "0: n(1) == 2 -> 1; 1: n(1) >= 0 -> 2; 2: n(1) >= 0 -> 0;", 3)});
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

// Runs one fixture under one boundary on both paths and compares.
void checkEquivalence(const Fixture& f, rule::Boundary boundary, const core::GridSpec& spec, double p = 0.0) {
    rule::RuleIR ir = f.ir;
    ir.boundary = boundary;
    auto compiled = rule::compileLut(ir);
    REQUIRE(std::holds_alternative<rule::LutRule>(compiled));
    const rule::LutRule& lut = std::get<rule::LutRule>(compiled);

    core::HostGrid host(spec);
    fill(host, ir.states, 0x5eed + static_cast<uint32_t>(boundary), 0.4);

    auto made = core::GpuGrid::create(spec, core::queryVram());
    REQUIRE(std::holds_alternative<core::GpuGrid>(made));
    core::GpuGrid& gpu = std::get<core::GpuGrid>(made);
    gpu.upload(host.current());

    sim::GpuStepper stepper;
    const auto err = stepper.setRule(lut, spec);
    if (err) FAIL(err->message);
    const sim::CellMutation mutation{sim::mutationThreshold(p), 0xb0b0b0b0ull + static_cast<uint32_t>(boundary)};
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
    INFO(std::format("{} / {} / {}x{}x{} / p={}: first difference at cell {}", f.name,
                     rule::toString(boundary), spec.width, spec.height, spec.depth, p, firstDiff));
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

TEST_CASE("cell mutation changes the trajectory and is reproducible", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 48, 48, 1};
    const rule::LutRule life = std::get<rule::LutRule>(rule::compileLut(dsl("B3/S23")));
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

    const rule::LutRule life = std::get<rule::LutRule>(rule::compileLut(dsl("B3/S23")));
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
    REQUIRE_FALSE(stepper.setRule(std::get<rule::LutRule>(rule::compileLut(dsl("B3/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::LutRule>(rule::compileLut(dsl("B36/S23"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 1);
    REQUIRE_FALSE(stepper.setRule(std::get<rule::LutRule>(rule::compileLut(dsl("B2/S/C3"))), spec).has_value());
    CHECK(stepper.cachedPrograms() == 2);
}

TEST_CASE("a rule whose dimensionality mismatches the grid is refused, leaving the old rule active", "[gpu]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    sim::GpuStepper stepper;
    REQUIRE_FALSE(stepper.setRule(std::get<rule::LutRule>(rule::compileLut(dsl("B3/S23"))), spec).has_value());
    const auto err = stepper.setRule(std::get<rule::LutRule>(rule::compileLut(dsl("B5/S45", 3))), spec);
    REQUIRE(err.has_value());
    CHECK(err->message.find("3D") != std::string::npos);
    CHECK(stepper.hasRule());
}
