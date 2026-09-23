// Langton's loops (F-010).
//
// The rule is 219 transcribed transitions and the pattern is a transcribed
// initial configuration: both are published data rather than anything this
// project derived, so the thing worth testing is not that they parse but that
// they do what Langton said they do. A table with a few wrong entries compiles
// happily, runs happily, and quietly fails to reproduce — which is
// indistinguishable from a correct table on a mistyped seed, and is exactly
// the failure this guards.
//
// It reads the bundled files rather than copies of them, so an edit to either
// has to keep the loop replicating.

#include "rule/compile.hpp"
#include "rule/dsl.hpp"
#include "sim/cpu_step.hpp"
#include "sim/pattern.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace aether;

namespace {

std::string readOrSkip(const std::string& path) {
    // The bundled files sit beside the source tree; a build run from somewhere
    // else should say so rather than fail as though the rule were wrong.
    for (const std::string prefix : {"", "../", "../../"}) {
        std::ifstream in(prefix + path);
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }
    SKIP("cannot find " + path + " from the working directory");
    return {};
}

rule::CompiledRule loopsRule() {
    const std::string source = readOrSkip("rules/langtons-loops.rule");
    rule::DslContext ctx;
    ctx.dimensions = 2;
    ctx.boundary = rule::Boundary::Zero;
    auto parsed = rule::parseDsl(source, ctx);
    REQUIRE(parsed);
    rule::RuleIR ir = *parsed.ir;
    ir.boundary = rule::Boundary::Zero;   // a colony must not wrap into itself
    auto c = rule::compileRule(ir);
    REQUIRE(std::holds_alternative<rule::CompiledRule>(c));
    return std::get<rule::CompiledRule>(std::move(c));
}

sim::Pattern loopSeed() {
    auto p = sim::parsePattern(readOrSkip("patterns/langtons-loop.rle"));
    if (const auto* e = std::get_if<sim::PatternError>(&p)) FAIL(e->message);
    return std::get<sim::Pattern>(std::move(p));
}

size_t population(const core::HostGrid& g) {
    size_t n = 0;
    for (uint8_t v : g.current()) {
        if (v != 0) ++n;
    }
    return n;
}

}  // namespace

TEST_CASE("Langton's loops is eight states on von Neumann, and fits a table", "[langton]") {
    const rule::CompiledRule rule = loopsRule();
    CHECK(rule.states == 8);
    CHECK(rule.neighbourCount() == 4);
    CHECK(rule.kind == rule::Kind::NonTotalistic);
    // 8^5. F-010 predicted this would fit the table backend, and it does.
    CHECK(rule.table.size() == 32768);
    CHECK(rule.backend == rule::Backend::Lut);
}

TEST_CASE("the bundled seed is the published configuration", "[langton]") {
    const sim::Pattern seed = loopSeed();
    CHECK(seed.width == 14);
    CHECK(seed.height == 10);
    CHECK(seed.states == 8);
    size_t live = 0;
    for (uint8_t c : seed.cells) {
        if (c != 0) ++live;
    }
    CHECK(live == 85);
}

TEST_CASE("the loop reproduces itself and then fills the space", "[langton]") {
    const rule::CompiledRule rule = loopsRule();
    const sim::Pattern seed = loopSeed();

    const core::GridSpec spec{2, 120, 120, 1};
    core::HostGrid g(spec);
    for (uint32_t y = 0; y < seed.height; ++y) {
        for (uint32_t x = 0; x < seed.width; ++x) {
            g.set(40 + x, 50 + y, 0, seed.cells[size_t{y} * seed.width + x]);
        }
    }

    const size_t start = population(g);
    REQUIRE(start == 85);

    std::vector<size_t> at;
    for (int gen = 1; gen <= 600; ++gen) {
        sim::cpuStep(rule, g);
        if (gen == 200 || gen == 400 || gen == 600) at.push_back(population(g));
    }

    // The figures below are from the run whose colony was looked at: two loops
    // side by side at generation 200, a spreading colony by 500. They are a
    // pin on a deterministic rule, not a target — if they move, the
    // transcription has changed and the picture needs looking at again.
    REQUIRE(at.size() == 3);
    CHECK(at[0] == 219);
    CHECK(at[1] == 550);
    CHECK(at[2] == 1235);

    // What those numbers mean, said in a way that survives a re-transcription:
    // it reproduces rather than dying, and it builds structure rather than
    // flooding. A grid this size holds 14,400 cells.
    CHECK(at[0] > start * 2);
    CHECK(at[2] > at[1]);
    CHECK(at[2] < 3000);
}
