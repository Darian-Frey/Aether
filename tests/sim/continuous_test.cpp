// The continuous step on the CPU path (F-006, Phase 5 step 3): convolution,
// then growth, then a clamp. The GPU half arrives with the generator.

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "rule/growth.hpp"
#include "rule/lua.hpp"
#include "sim/cpu_step.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace aether;
using Catch::Approx;

namespace {

// A kernel that is flat over its whole support, so the convolution of a
// uniform grid is exactly that uniform value and the growth function is the
// only thing under test.
rule::CompiledRule flatRule(rule::GrowthSpec g, uint8_t radius = 1) {
    rule::RuleIR ir;
    ir.dimensions = 2;
    ir.cell_type = core::CellType::F32;
    ir.kind = rule::Kind::Continuous;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, radius};
    rule::Kernel k;
    k.shape = rule::Kernel::Shape::Radial;
    k.profile = {1.0f};
    k.growth = rule::growthExpression(g);
    ir.transition = k;
    auto c = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&c)) FAIL(e->message);
    return std::get<rule::CompiledRule>(std::move(c));
}

core::HostGrid uniformGrid(uint32_t w, uint32_t h, float v) {
    core::HostGrid g({2, w, h, 1, core::CellType::F32});
    auto cells = g.currentFloats();
    std::fill(cells.begin(), cells.end(), v);
    return g;
}

float valueAt(const core::HostGrid& g, uint32_t x, uint32_t y) {
    return g.currentFloats()[g.index(x, y)];
}

}  // namespace

TEST_CASE("a uniform grid convolves to its own value", "[continuous]") {
    const rule::CompiledRule r = flatRule({rule::GrowthForm::Polynomial, 0.30f, 0.05f, 1.0f});
    core::HostGrid g = uniformGrid(8, 8, 0.30f);

    sim::StepScratch scratch(r);
    const auto t = sim::stepCell(r, g.spec(), g.current(), 4, 4, 0, 0, {}, scratch);
    CHECK(t.ownValue == Approx(0.30f));
    CHECK(t.convolution == Approx(0.30f));     // the weights sum to one
    CHECK(t.increment == Approx(1.0f));        // sitting on the peak
    CHECK(t.nextValue == Approx(1.0f));        // 0.3 + 1.0, clamped
}

TEST_CASE("growth at the peak fills the grid and away from it empties it", "[continuous]") {
    const rule::GrowthSpec spec{rule::GrowthForm::Polynomial, 0.30f, 0.05f, 0.1f};
    const rule::CompiledRule r = flatRule(spec);

    core::HostGrid grows = uniformGrid(8, 8, 0.30f);
    for (int i = 0; i < 10; ++i) sim::cpuStep(r, grows);
    CHECK(valueAt(grows, 4, 4) > 0.30f);

    // A grid starting above the band cannot simply drain: on the way down it
    // passes through the band and is held there, which is the whole point of a
    // growth function and not something to assert away.
    core::HostGrid held = uniformGrid(8, 8, 0.90f);
    for (int i = 0; i < 40; ++i) sim::cpuStep(r, held);
    CHECK(valueAt(held, 4, 4) > 0.2f);
    CHECK(valueAt(held, 4, 4) < 0.5f);

    // Starting below it there is nothing to cross, so the grid drains and
    // stays drained: 0 is a floor, not a wrap.
    const rule::CompiledRule high = flatRule({rule::GrowthForm::Polynomial, 0.90f, 0.05f, 0.1f});
    core::HostGrid drains = uniformGrid(8, 8, 0.20f);
    for (int i = 0; i < 10; ++i) sim::cpuStep(high, drains);
    CHECK(valueAt(drains, 4, 4) == Approx(0.0f));
    sim::cpuStep(high, drains);
    CHECK(valueAt(drains, 4, 4) == Approx(0.0f));
}

TEST_CASE("a cell never leaves [0, 1], whatever the growth asks for", "[continuous]") {
    // dt = 1 is the hardest case: the increment is the full -1 .. 1.
    const rule::CompiledRule r = flatRule({rule::GrowthForm::Rectangular, 0.5f, 0.2f, 1.0f});
    core::HostGrid g({2, 16, 16, 1, core::CellType::F32});
    auto cells = g.currentFloats();
    for (size_t i = 0; i < cells.size(); ++i) cells[i] = static_cast<float>(i % 101) / 100.0f;

    for (int i = 0; i < 25; ++i) {
        sim::cpuStep(r, g);
        for (float v : g.currentFloats()) {
            INFO("generation " << i << " produced " << v);
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
    }
}

TEST_CASE("cell mutation on a continuous grid lands in range and replays", "[continuous]") {
    const rule::CompiledRule r = flatRule({rule::GrowthForm::Rectangular, 0.5f, 0.2f, 0.1f});
    const sim::CellMutation mutation{sim::mutationThreshold(0.25), 0xc0ffeeull, 0};

    auto run = [&] {
        core::HostGrid g = uniformGrid(24, 24, 0.5f);
        for (int i = 0; i < 12; ++i) sim::cpuStep(r, g, static_cast<uint64_t>(i), mutation);
        return std::vector<float>(g.currentFloats().begin(), g.currentFloats().end());
    };
    const auto a = run();
    for (float v : a) {
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
    // Stream B is a hash of coordinate and generation, so it replays exactly.
    CHECK(a == run());

    // And it did something: a mutated cell takes a value from the hash, which
    // a uniform grid under a flat rule would never reach on its own.
    CHECK(std::ranges::any_of(a, [&](float v) { return v != a[0]; }));
}

TEST_CASE("a continuous rule from Lua sustains a structure of its own size", "[continuous]") {
    // A kernel shell and a growth band, which is the shape of a Lenia rule.
    // The band has to be wider than a step: at sigma 0.015 with dt 0.1 — the
    // published orbium numbers — a uniform blob overshoots the band every
    // generation and drains, because every interior cell moves together.
    // Orbium works from its own seed pattern, where the convolution varies
    // across the ring, and that pattern is a data item rather than something
    // to invent here; the bundled pattern library of F-027 is where it belongs.
    const char* src = R"(
        local R = 6
        local profile = {}
        for i = 0, R do
            local x = i / R
            profile[i + 1] = math.exp(-((x - 0.5) ^ 2) / (2 * 0.15 ^ 2))
        end
        return {
            cell_type = "f32", kind = "continuous", dimensions = 2,
            neighbourhood = { type = "moore", radius = R },
            kernel = { shape = "radial", profile = profile },
            growth = { form = "polynomial", mu = 0.20, sigma = 0.05, dt = 0.05 },
        }
    )";
    auto compiled = rule::compileLua(src, {});
    if (const auto* e = std::get_if<rule::LuaError>(&compiled)) FAIL(e->message);
    auto made = rule::compileRule(std::get<rule::RuleIR>(compiled));
    if (const auto* e = std::get_if<rule::CompileError>(&made)) FAIL(e->message);
    const rule::CompiledRule r = std::get<rule::CompiledRule>(std::move(made));

    core::HostGrid g({2, 64, 64, 1, core::CellType::F32});
    auto cells = g.currentFloats();
    for (uint32_t y = 0; y < 64; ++y) {
        for (uint32_t x = 0; x < 64; ++x) {
            const float dx = static_cast<float>(x) - 32.0f, dy = static_cast<float>(y) - 32.0f;
            if (dx * dx + dy * dy < 64.0f) cells[g.index(x, y)] = 0.3f;
        }
    }

    auto mass = [&] {
        float sum = 0.0f;
        for (float v : g.currentFloats()) {
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
            sum += v;
        }
        return sum;
    };

    for (int i = 0; i < 400; ++i) sim::cpuStep(r, g, static_cast<uint64_t>(i));
    const float settled = mass();
    for (int i = 400; i < 600; ++i) sim::cpuStep(r, g, static_cast<uint64_t>(i));
    const float later = mass();

    const float cellCount = 64.0f * 64.0f;
    INFO("mass " << settled << " then " << later << " of " << cellCount);
    CHECK(settled > 0.05f * cellCount);        // it did not die
    CHECK(settled < 0.60f * cellCount);        // nor fill the grid
    CHECK(later == Approx(settled).epsilon(0.05));   // and it is holding its size
}
