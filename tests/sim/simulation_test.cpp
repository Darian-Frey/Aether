#include "rule/dsl.hpp"
#include "sim/cpu_step.hpp"
#include "sim/simulation.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;
using sim::Path;
using sim::Simulation;

namespace {

rule::RuleIR life() { return *rule::parseDsl("B3/S23").ir; }

std::vector<uint8_t> snapshot(Simulation& s) {
    s.syncToHost();
    return {s.host().current().begin(), s.host().current().end()};
}

Simulation make(const core::GridSpec& spec, const rule::RuleIR& ir, Path p) {
    auto made = Simulation::create(spec, ir, p, 1234);
    if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
    return std::get<Simulation>(std::move(made));
}

}  // namespace

TEST_CASE("switching paths mid-run does not change the trajectory", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 48, 40, 1};
    const double density[1] = {0.4};

    Simulation ref = make(spec, life(), Path::Cpu);
    ref.fillRandom(density);
    for (int i = 0; i < 40; ++i) ref.step();

    Simulation mixed = make(spec, life(), Path::Gpu);
    mixed.fillRandom(density);           // same seed, same fill
    for (int i = 0; i < 10; ++i) mixed.step();
    REQUIRE_FALSE(mixed.setPath(Path::Cpu).has_value());
    for (int i = 0; i < 15; ++i) mixed.step();
    REQUIRE_FALSE(mixed.setPath(Path::Gpu).has_value());
    for (int i = 0; i < 15; ++i) mixed.step();

    CHECK(mixed.generation() == 40);
    CHECK(snapshot(mixed) == snapshot(ref));
}

TEST_CASE("the renderer's texture tracks the CPU path", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    Simulation s = make(spec, life(), Path::Cpu);
    s.host().set(2, 3, 0, 1); s.host().set(3, 3, 0, 1); s.host().set(4, 3, 0, 1);
    s.commitHost();
    s.step();
    s.syncToHost();   // no-op on CPU path; host is the truth
    CHECK(s.host().get(3, 2) == 1);
    CHECK(s.host().get(3, 4) == 1);
    CHECK(s.host().get(2, 3) == 0);
    // The GPU copy the renderer samples must agree. Switching to the GPU
    // path and back reads it: the switch to GPU uploads nothing new (the CPU
    // step already mirrored), the switch back downloads what the GPU holds.
    REQUIRE_FALSE(s.setPath(Path::Gpu).has_value());
    REQUIRE_FALSE(s.setPath(Path::Cpu).has_value());
    CHECK(s.host().get(3, 2) == 1);
    CHECK(s.host().get(2, 3) == 0);
}

TEST_CASE("a failed rule change leaves the running rule and grid untouched", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 32, 32, 1};
    Simulation s = make(spec, life(), Path::Gpu);
    const double density[1] = {0.35};
    s.fillRandom(density);
    s.step();
    const auto before = snapshot(s);
    const uint64_t hash = rule::irHash(s.rule());

    rule::DslContext ctx;
    ctx.dimensions = 3;
    const auto err = s.setRule(*rule::parseDsl("B5/S45", ctx).ir);
    REQUIRE(err.has_value());
    CHECK(rule::irHash(s.rule()) == hash);
    CHECK(snapshot(s) == before);

    // A continuous rule has no backend at all, and is refused as harmlessly.
    const auto err2 = s.setRule([] {
        rule::RuleIR k;
        k.cell_type = core::CellType::F32;
        k.kind = rule::Kind::Continuous;
        rule::Kernel kern;
        kern.profile = {1.0f};
        kern.growth.nodes = {{rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
        k.transition = kern;
        return k;
    }());
    REQUIRE(err2.has_value());
    CHECK(err2->message.find("Phase 5") != std::string::npos);
    CHECK(rule::irHash(s.rule()) == hash);
}

TEST_CASE("shrinking the state count resets cells that no longer exist", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 16, 16, 1};
    Simulation s = make(spec, *rule::parseDsl("B2/S/C4").ir, Path::Gpu);
    s.host().set(1, 1, 0, 3);
    s.host().set(2, 2, 0, 1);
    s.commitHost();
    REQUIRE_FALSE(s.setRule(life()).has_value());
    const auto cells = snapshot(s);
    CHECK(s.host().get(1, 1) == 0);
    CHECK(s.host().get(2, 2) == 1);
    for (uint8_t c : cells) CHECK(c < 2);
}

TEST_CASE("frame() drives generations through the scheduler", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    Simulation s = make(core::GridSpec{2, 16, 16, 1}, life(), Path::Gpu);
    s.scheduler().setTargetRate(120.0);
    s.scheduler().setMaxStepsPerFrame(64);
    s.scheduler().setFrameBudget(0.0);   // unlimited
    for (int i = 0; i < 30; ++i) s.frame(1.0 / 60.0);
    CHECK(s.generation() == 60);
    s.scheduler().setPaused(true);
    s.frame(1.0);
    CHECK(s.generation() == 60);
    s.scheduler().requestBurst(7);
    while (s.scheduler().burstRemaining() > 0) s.frame(1.0 / 60.0);
    CHECK(s.generation() == 67);
}

TEST_CASE("Simulation refuses a grid the VRAM guard or spec check rejects", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    auto made = Simulation::create(core::GridSpec{2, 0, 16, 1}, life());
    CHECK(std::holds_alternative<core::Error>(made));
    auto mismatch = Simulation::create(core::GridSpec{3, 8, 8, 8}, life());
    REQUIRE(std::holds_alternative<core::Error>(mismatch));
    CHECK(std::get<core::Error>(mismatch).message.find("3D") != std::string::npos);
}

TEST_CASE("a Simulation returned by value keeps mutating on the GPU path (moved stepper)", "[gpu][simulation]") {
    // Simulation::create returns by value, so the GpuStepper is moved. A
    // move that drops any per-step parameter shows up here as the GPU path
    // silently running without mutation while the CPU path mutates.
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 40, 30, 1};
    auto g = make(spec, life(), Path::Gpu);
    auto c = make(spec, life(), Path::Cpu);
    for (auto* s : {&g, &c}) {
        s->paintSpan(10, 20, 10, 0, 1);
        s->paintSpan(10, 20, 11, 0, 1);
        s->setCellMutation(0.05);
    }
    // Move again, deliberately.
    Simulation g2 = std::move(g);
    for (int i = 0; i < 20; ++i) { g2.step(); c.step(); }
    CHECK(snapshot(g2) == snapshot(c));

    // And mutation did happen: a static block under Life with p = 0 would
    // be unchanged, so with p > 0 the grid differs from the p = 0 run.
    auto z = make(spec, life(), Path::Gpu);
    z.paintSpan(10, 20, 10, 0, 1);
    z.paintSpan(10, 20, 11, 0, 1);
    for (int i = 0; i < 20; ++i) z.step();
    CHECK(snapshot(z) != snapshot(g2));
}
