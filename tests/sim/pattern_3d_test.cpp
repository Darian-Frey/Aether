// Three-dimensional patterns against a grid (F-037).
//
// `sim/pattern` has been able to carry and blit a 3D pattern since F-012 — the
// extents, `blitPattern`'s z loop and the native format all take a depth — but
// until F-037 nothing in the interface could place one, so the one bundled 3D
// pattern sat in `patterns/` unreachable. These cases cover the copy itself and
// the claim the bundled pattern's header makes, which is the kind of claim
// BUG-024 is about.
//
// No GL: a `Simulation` allocates GPU textures whatever path it is on, and
// nothing here needs one. `blitPattern` into a `HostGrid` and `cpuStep` over it
// is the same arithmetic the interface drives.
//
// The library directories arrive as compile definitions rather than as relative
// paths, because ctest runs the binary from `build/` and a bare "rules" finds
// nothing there. The failure mode matters: these cases FAIL when the directory
// is missing rather than skipping, so a path that stops resolving is loud.

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "rule/library.hpp"
#include "sim/cpu_step.hpp"
#include "sim/pattern.hpp"
#include "sim/pattern_library.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace aether;

namespace {

rule::RuleIR bundledRule(const char* id) {
    for (const auto& e : rule::loadLibrary({AETHER_RULES_DIR})) {
        if (e.id != id) continue;
        auto c = rule::compileLibraryRule(e, rule::Boundary::Wrap);
        if (auto* ir = std::get_if<rule::RuleIR>(&c)) return *ir;
    }
    FAIL("no bundled rule " << id);
    return {};
}

sim::Pattern bundledPattern(const char* id) {
    for (const auto& p : sim::loadPatternLibrary({AETHER_PATTERNS_DIR})) {
        if (p.id == id) return p.pattern;
    }
    FAIL("no bundled pattern " << id);
    return {};
}

}  // namespace

TEST_CASE("a 3D pattern lands where it is put, at every depth", "[pattern]") {
    const auto ir = bundledRule("life-3d-4555");
    const core::GridSpec spec{3, 12, 12, 12, core::CellType::U8};
    const auto shell = bundledPattern("bays-shell");
    REQUIRE(shell.dimensions == 3);
    REQUIRE(shell.depth > 1);

    // The arithmetic is per row and strided by the grid's width, so a mistake
    // in the z term shows as cells in the wrong plane rather than as a crash.
    // Walking every origin is what proves the stride, not one placement.
    for (uint32_t z = 0; z + shell.depth <= spec.depth; ++z) {
        std::vector<uint8_t> cells(spec.cellCount(), 0);
        REQUIRE_FALSE(sim::patternFits(shell, ir, spec, 2, 3, z).has_value());
        sim::blitPattern(shell, spec, cells, 2, 3, z);

        // Read it back out and compare: extractRegion is the reverse copy, so
        // a round trip catches an index that is wrong in both directions the
        // same way, which a one-way check would not.
        auto back = sim::extractRegion(ir, spec, cells, 2, 3, z,
                                       shell.width, shell.height, shell.depth);
        const auto* got = std::get_if<sim::Pattern>(&back);
        REQUIRE(got);
        CHECK(got->cells == shell.cells);

        // And nothing outside the box was touched.
        size_t inside = 0;
        for (uint8_t v : shell.cells) if (v) ++inside;
        size_t total = 0;
        for (uint8_t v : cells) if (v) ++total;
        CHECK(total == inside);
    }
}

TEST_CASE("a 2D pattern goes onto one plane of a 3D grid", "[pattern]") {
    // This is what placing on a slice means: a pattern one cell deep occupies
    // exactly the slab it was dropped on, and the planes either side stay
    // empty. Nothing in `patternFits` compares dimensions, which is deliberate
    // — a flat pattern is a legal thing to put into a volume.
    const auto ir = bundledRule("life-3d-4555");
    const core::GridSpec spec{3, 10, 10, 10, core::CellType::U8};
    sim::Pattern flat;
    flat.dimensions = 2;
    flat.width = 3; flat.height = 3; flat.depth = 1;
    flat.states = 2;
    flat.cells = {1, 1, 1, 1, 0, 1, 1, 1, 1};

    std::vector<uint8_t> cells(spec.cellCount(), 0);
    REQUIRE_FALSE(sim::patternFits(flat, ir, spec, 1, 1, 4).has_value());
    sim::blitPattern(flat, spec, cells, 1, 1, 4);

    const core::HostGrid probe(spec);
    auto at = [&](uint32_t x, uint32_t y, uint32_t z) { return cells[probe.index(x, y, z)]; };
    CHECK(at(1, 1, 4) == 1);
    CHECK(at(2, 2, 4) == 0);   // the hole in the middle
    CHECK(at(3, 3, 4) == 1);
    for (uint32_t z = 0; z < spec.depth; ++z) {
        if (z == 4) continue;
        for (uint32_t y = 0; y < spec.height; ++y) {
            for (uint32_t x = 0; x < spec.width; ++x) {
                INFO("z=" << z);
                CHECK(at(x, y, z) == 0);
            }
        }
    }
}

TEST_CASE("the bundled Bays shell is stable, as its header claims", "[pattern]") {
    // Its comment says the twelve cells "hold their shape for ever". That is a
    // behavioural claim about a data file, so it is checked by running the rule
    // rather than by the file parsing (the same reason Langton's loop is tested
    // by reproducing). It is also the only bundled pattern that F-037 made
    // reachable from the interface, so it is now worth guarding.
    const auto ir = bundledRule("life-3d-4555");
    auto compiled = rule::compileRule(ir);
    const auto* cr = std::get_if<rule::CompiledRule>(&compiled);
    REQUIRE(cr);

    const auto shell = bundledPattern("bays-shell");
    size_t cellsAlive = 0;
    for (uint8_t v : shell.cells) if (v) ++cellsAlive;
    CHECK(cellsAlive == 12);

    // Wide enough that the shell cannot see itself round the wrap.
    const core::GridSpec spec{3, 16, 16, 16, core::CellType::U8};
    core::HostGrid grid(spec);
    REQUIRE_FALSE(sim::patternFits(shell, ir, spec, 6, 6, 6).has_value());
    sim::blitPattern(shell, spec, grid.current(), 6, 6, 6);

    const std::vector<uint8_t> start(grid.current().begin(), grid.current().end());
    for (uint64_t g = 0; g < 200; ++g) {
        sim::cpuStep(*cr, grid, g);
        const std::vector<uint8_t> now(grid.current().begin(), grid.current().end());
        INFO("generation " << g + 1);
        REQUIRE(now == start);
    }
}
