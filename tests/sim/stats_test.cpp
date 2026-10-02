// Population and field readouts (F-036).
//
// The whole difficulty of this feature is summation *order*, so that is what
// this file is about. The host reduction and the shader are twins in the same
// sense `cpuStep` and `lut_step.comp` are, and the contract they share — tiles
// in cell order, tiles in tile order — is written down in `sim/stats.hpp`.
//
// A float total summed in a different order is a different number, so these
// comparisons are exact equalities rather than tolerances. Were they
// tolerances, a shader that reduced in whatever order its driver happened to
// schedule would pass here and give a different answer on another machine.

#include "rule/compile.hpp"
#include "rule/dsl.hpp"
#include "sim/cpu_step.hpp"
#include "sim/gpu_reduce.hpp"
#include "sim/simulation.hpp"
#include "sim/stats.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <set>
#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

namespace {

rule::CompiledRule compiled(const rule::RuleIR& ir) {
    auto r = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&r)) FAIL(e->message);
    return std::get<rule::CompiledRule>(r);
}

rule::RuleIR life() {
    const auto p = rule::parseDsl("B3/S23");
    REQUIRE(p.ir);
    return *p.ir;
}

}  // namespace

TEST_CASE("the host reduction counts every cell exactly once", "[stats]") {
    const rule::RuleIR ir = life();
    const rule::CompiledRule rule = compiled(ir);
    core::GridSpec spec{2, 37, 23, 1, core::CellType::U8};   // not a multiple of the tile
    core::HostGrid grid(spec);

    size_t expectedLive = 0;
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        const uint8_t v = (i % 7 == 0) ? 1 : 0;
        grid.current()[i] = v;
        if (v) ++expectedLive;
    }

    const sim::GridStats s = sim::reduce(rule, spec, grid.current());
    REQUIRE(s.stateCounts.size() == 2);
    CHECK(s.stateCounts[0] + s.stateCounts[1] == spec.cellCount());
    CHECK(s.stateCounts[1] == expectedLive);
    CHECK(s.live() == expectedLive);

    // A grid whose cell count is not a multiple of the tile size is the case a
    // reduction gets wrong: the last tile is short and its tail must not be
    // counted. 37 x 23 is 851 cells, which is three full tiles and 83 cells.
    CHECK(spec.cellCount() % sim::kTileCells != 0);
}

TEST_CASE("genome buckets group the way the palette colours", "[stats]") {
    // The plot's bands are the grid's colours, so the bucket is the palette's
    // hash and not a second grouping that would look unrelated to what is on
    // screen. Checked as a property rather than against pinned numbers: the
    // hash is presentational and a change to it is odd, not wrong.
    std::set<uint32_t> seen;
    for (uint32_t g = 0; g < 2000; ++g) {
        const uint32_t b = sim::genomeBucket(g, 18);
        CHECK(b < sim::kGenomeBuckets);
        seen.insert(b);
    }
    CHECK(seen.size() == sim::kGenomeBuckets);   // every bucket is reachable

    // Only the live bits decide, or a lineage would be split across bands by
    // bits nothing reads (the same reason mutation is masked).
    CHECK(sim::genomeBucket(0x3ffff, 18) == sim::genomeBucket(0xfff3ffff, 18));
}

TEST_CASE("the GPU reduction agrees with the host exactly", "[stats][gpu]") {
    GlContext gl;
    requireGl(gl);

    // Two shapes and two rules, because the reduction's index arithmetic is
    // where a dimensionality bug would hide.
    struct Case { const char* why; core::GridSpec spec; };
    const Case cases[] = {
        {"2D, a whole number of tiles", {2, 64, 64, 1, core::CellType::U8}},
        {"2D, a short last tile",       {2, 37, 23, 1, core::CellType::U8}},
        {"3D",                          {3, 16, 16, 16, core::CellType::U8}},
    };

    for (const Case& c : cases) {
        INFO(c.why);
        rule::RuleIR ir = life();
        ir.dimensions = c.spec.dimensions;
        if (c.spec.dimensions == 3) {
            const auto p = rule::parseDsl("B5/S45", {3, rule::Boundary::Wrap});
            REQUIRE(p.ir);
            ir = *p.ir;
        }
        const rule::CompiledRule rule = compiled(ir);

        auto made = sim::Simulation::create(c.spec, ir, sim::Path::Gpu, 3u, 5u);
        if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
        sim::Simulation& s = std::get<sim::Simulation>(made);
        s.fillRandom(std::vector<double>{0.3});
        for (int g = 0; g < 10; ++g) s.step();
        s.syncToHost();

        const sim::GridStats host = sim::reduce(rule, c.spec, s.host().current());

        sim::GpuReducer red;
        if (auto e = red.setRule(rule, c.spec)) FAIL(e->message);
        const sim::GridStats gpu = red.sample(s.texture(), {});

        REQUIRE(host.stateCounts.size() == gpu.stateCounts.size());
        CHECK(host.stateCounts == gpu.stateCounts);
        // And the change count, which needs the generation before this one.
        // Taken by copying the grid and stepping once more rather than by
        // reading `host().next()`: on the GPU path only `current()` is
        // downloaded, so the other host buffer holds whatever it last held and
        // comparing against it would be comparing against nothing in
        // particular. The first draft of this case did exactly that and read
        // 320 against the GPU's 374.
        const std::vector<uint8_t> before(s.host().current().begin(), s.host().current().end());
        s.step();
        s.syncToHost();
        const sim::GridStats hostCh = sim::reduce(rule, c.spec, s.host().current(), {}, before);
        const sim::GridStats gpuCh = red.sample(s.texture(), {}, s.previousTexture());
        REQUIRE(hostCh.changedKnown);
        REQUIRE(gpuCh.changedKnown);
        CHECK(hostCh.changed == gpuCh.changed);
        CHECK(hostCh.changed > 0);   // ten generations of Life is not a still life
        // Something was alive, or this compares two grids of zeroes (IMP-011).
        CHECK(host.live() > 0);
        CHECK(host.live() < c.spec.cellCount());
    }
}

TEST_CASE("field totals agree between the paths, bit for bit", "[stats][gpu]") {
    GlContext gl;
    requireGl(gl);

    // The case the summation contract exists for. Integer counts agree whatever
    // the order; a float total does not, so this is where a reduction that let
    // the driver choose would show up — and only on some drivers, which is
    // worse than always.
    const auto p = rule::parseDsl("B3/S23");
    REQUIRE(p.ir);
    rule::RuleIR ir = *p.ir;
    ir.kind = rule::Kind::Expression;
    {
        // A two-state rule carrying one f32 field that holds something awkward
        // to add up: values of very different magnitudes, so the order of
        // addition genuinely changes the answer.
        rule::Field f;
        f.name = "heat";
        f.cell_type = core::CellType::F32;
        rule::Expression w;
        w.nodes = {
            {rule::ExprOp::FieldSelf, 0},                            // 0
            {rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.000123f},     // 1
            {rule::ExprOp::FloatLiteral, 0, 0, 0, 0, 0.9f},          // 2
            {rule::ExprOp::Mul, 0, 2},                               // 3  decay
            {rule::ExprOp::Add, 3, 1},                               // 4  plus a trickle
        };
        f.write = w;
        ir.fields = {f};

        rule::Expression t;
        t.nodes = {
            {rule::ExprOp::Count, 1},
            {rule::ExprOp::IntLiteral, 0, 0, 0, 3},
            {rule::ExprOp::Eq, 0, 1},
            {rule::ExprOp::IntLiteral, 0, 0, 0, 1},
            {rule::ExprOp::IntLiteral, 0, 0, 0, 0},
            {rule::ExprOp::Select, 2, 3, 4},
        };
        ir.transition = t;
    }
    const rule::CompiledRule rule = compiled(ir);
    const core::GridSpec spec{2, 48, 48, 1, core::CellType::U8};

    auto made = sim::Simulation::create(spec, ir, sim::Path::Gpu, 3u, 5u);
    if (const auto* e = std::get_if<core::Error>(&made)) FAIL(e->message);
    sim::Simulation& s = std::get<sim::Simulation>(made);
    s.fillRandom(std::vector<double>{0.3});
    for (int g = 0; g < 40; ++g) s.step();
    s.syncToHost();

    const std::span<const uint8_t> fieldBytes = s.fieldHost(0).current();
    const std::span<const uint8_t> fieldList[] = {fieldBytes};
    const sim::GridStats host = sim::reduce(rule, spec, s.host().current(), fieldList);

    sim::GpuReducer red;
    if (auto e = red.setRule(rule, spec)) FAIL(e->message);
    const unsigned int textures[] = {s.fieldTexture(0)};
    const sim::GridStats gpu = red.sample(s.texture(), textures);

    REQUIRE(host.fieldTotals.size() == 1);
    REQUIRE(gpu.fieldTotals.size() == 1);
    INFO("host " << host.fieldTotals[0] << ", gpu " << gpu.fieldTotals[0]);

    // Non-trivial, or this is two zeroes agreeing (IMP-011).
    CHECK(host.fieldTotals[0] > 1.0);
    CHECK(host.stateCounts == gpu.stateCounts);

    // Exactly equal, not nearly. The tiling is what buys this, and a tolerance
    // here would quietly accept a reduction that summed in some other order.
    CHECK(host.fieldTotals[0] == gpu.fieldTotals[0]);

    // And the tiling is doing work here rather than being a formality: the same
    // values added straight through, cell by cell with no brackets, give a
    // different number. Without this the equality above could hold for a grid
    // whose values happened to sum associatively, and the contract would be
    // untested on the only data it was asked about.
    float straight = 0.0f;
    for (size_t i = 0; i < spec.cellCount(); ++i) {
        float v = 0.0f;
        std::memcpy(&v, fieldBytes.data() + i * sizeof(float), sizeof(float));
        straight += v;
    }
    INFO("tiled " << host.fieldTotals[0] << ", straight through " << straight);
    CHECK(static_cast<double>(straight) != host.fieldTotals[0]);
}

TEST_CASE("the change count tells a still life from an oscillator", "[stats]") {
    // The number a population count cannot give you, and the reason it exists:
    // a block and a blinker both hold a constant population for ever, and one
    // of them is moving. Getting this wrong is not hypothetical — the first
    // report on the bundled 3D rules said "frozen residue" on the strength of a
    // population that had stopped moving, and it was a period-4 oscillator
    // (BUG-024's correction).
    const auto p = rule::parseDsl("B3/S23");
    REQUIRE(p.ir);
    const rule::CompiledRule rule = compiled(*p.ir);
    const core::GridSpec spec{2, 16, 16, 1, core::CellType::U8};

    auto at = [&](uint32_t x, uint32_t y) { return size_t{y} * spec.width + x; };

    SECTION("a block never changes") {
        core::HostGrid grid(spec);
        for (auto [x, y] : {std::pair<uint32_t, uint32_t>{4, 4}, {5, 4}, {4, 5}, {5, 5}}) {
            grid.current()[at(x, y)] = 1;
        }
        std::vector<uint8_t> before(grid.current().begin(), grid.current().end());
        sim::cpuStep(rule, grid, 0);
        const sim::GridStats s = sim::reduce(rule, spec, grid.current(), {}, before);
        REQUIRE(s.changedKnown);
        CHECK(s.live() == 4);
        CHECK(s.changed == 0);
    }

    SECTION("a blinker changes four cells every generation") {
        core::HostGrid grid(spec);
        for (uint32_t x = 4; x <= 6; ++x) grid.current()[at(x, 5)] = 1;
        std::vector<uint8_t> before(grid.current().begin(), grid.current().end());
        sim::cpuStep(rule, grid, 0);
        const sim::GridStats s = sim::reduce(rule, spec, grid.current(), {}, before);
        REQUIRE(s.changedKnown);
        CHECK(s.live() == 3);          // a blinker is three cells and stays three
        CHECK(s.changed == 4);         // the two ends go out, the two sides come on
        // Both this and the block above hold a *constant* population for ever,
        // which is all a population count can see. One of them is moving.
    }

    SECTION("with no previous generation the count is absent, not zero") {
        core::HostGrid grid(spec);
        grid.current()[at(1, 1)] = 1;
        const sim::GridStats s = sim::reduce(rule, spec, grid.current());
        CHECK_FALSE(s.changedKnown);
        CHECK(s.changed == 0);
    }
}
