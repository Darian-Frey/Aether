#include "rule/dsl.hpp"
#include "sim/lineage.hpp"
#include "sim/simulation.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

TEST_CASE("lineage is append-only with hashes and pins", "[lineage]") {
    sim::Lineage log;
    const auto life = *rule::parseDsl("B3/S23").ir;
    const auto high = *rule::parseDsl("B36/S23").ir;
    CHECK(log.append(0, life) == 0);
    CHECK(log.append(250, high) == 1);
    CHECK(log.size() == 2);
    CHECK(log.at(0).ir_hash == rule::irHash(life));
    CHECK(log.at(1).generation == 250);
    CHECK_FALSE(log.at(1).pinned);
    log.pin(1, "HighLife");
    CHECK(log.at(1).pinned);
    CHECK(log.at(1).name == "HighLife");
    CHECK(log.at(1).ir.metadata.name == "HighLife");
    log.unpin(1);
    CHECK_FALSE(log.at(1).pinned);
    CHECK(log.at(1).name == "HighLife");   // the name survives an unpin
}

TEST_CASE("every rule change goes into the lineage; rewind restores and records", "[gpu][lineage]") {
    GlContext gl;
    requireGl(gl);
    auto made = sim::Simulation::create(core::GridSpec{2, 16, 16, 1}, *rule::parseDsl("B3/S23").ir);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    auto& s = std::get<sim::Simulation>(made);
    REQUIRE(s.lineage().size() == 1);
    CHECK(s.lineage().at(0).generation == 0);

    for (int i = 0; i < 5; ++i) s.step();
    REQUIRE_FALSE(s.setRule(*rule::parseDsl("B36/S23").ir).has_value());
    REQUIRE(s.lineage().size() == 2);
    CHECK(s.lineage().at(1).generation == 5);
    CHECK(s.lineage().at(1).ir_hash == rule::irHash(s.rule()));

    // A refused rule leaves no trace.
    CHECK(s.setRule(*rule::parseDsl("B2/S/C25").ir).has_value());
    CHECK(s.lineage().size() == 2);

    for (int i = 0; i < 3; ++i) s.step();
    REQUIRE_FALSE(s.rewind(0).has_value());
    REQUIRE(s.lineage().size() == 3);
    CHECK(s.lineage().at(2).generation == 8);
    CHECK(s.lineage().at(2).rewound_from == 0);
    CHECK(rule::irHash(s.rule()) == s.lineage().at(0).ir_hash);
    CHECK(s.rewind(99).has_value());
}

TEST_CASE("rule mutation fires on the interval, logs each rule, and replays from the seeds", "[gpu][lineage]") {
    GlContext gl;
    requireGl(gl);
    auto run = [](sim::Path path) {
        auto made = sim::Simulation::create(core::GridSpec{2, 32, 32, 1}, *rule::parseDsl("B3/S23").ir, path, 11, 22);
        REQUIRE(std::holds_alternative<sim::Simulation>(made));
        auto s = std::get<sim::Simulation>(std::move(made));
        const double d[1] = {0.35};
        s.fillRandom(d);
        s.setRuleMutation({true, 10, 1});
        s.setCellMutation(0.001);
        for (int i = 0; i < 100; ++i) s.step();
        return s;
    };
    auto a = run(sim::Path::Gpu);
    auto b = run(sim::Path::Gpu);
    auto c = run(sim::Path::Cpu);

    // 1 initial + one per interval boundary at 10, 20, ..., 90 (100 is
    // never stepped past), minus any skipped.
    CHECK(a.lineage().size() + a.counters().rule_mutations_skipped == 10);
    CHECK(a.counters().rule_mutations == a.lineage().size() - 1);
    for (size_t i = 1; i < a.lineage().size(); ++i) {
        CHECK(a.lineage().at(i).generation % 10 == 0);
        CHECK(a.lineage().at(i).ir_hash != a.lineage().at(i - 1).ir_hash);
    }

    // Same seeds, same lineage, same grid — on either path.
    REQUIRE(a.lineage().size() == b.lineage().size());
    REQUIRE(a.lineage().size() == c.lineage().size());
    for (size_t i = 0; i < a.lineage().size(); ++i) {
        CHECK(a.lineage().at(i).ir_hash == b.lineage().at(i).ir_hash);
        CHECK(a.lineage().at(i).ir_hash == c.lineage().at(i).ir_hash);
    }
    a.syncToHost(); b.syncToHost();
    const std::vector<uint8_t> ga(a.host().current().begin(), a.host().current().end());
    const std::vector<uint8_t> gb(b.host().current().begin(), b.host().current().end());
    const std::vector<uint8_t> gc(c.host().current().begin(), c.host().current().end());
    CHECK(ga == gb);
    CHECK(ga == gc);
}

TEST_CASE("toggling cell mutation does not change the rule sequence (D-005)", "[gpu][lineage]") {
    GlContext gl;
    requireGl(gl);
    auto run = [](double p) {
        auto made = sim::Simulation::create(core::GridSpec{2, 16, 16, 1}, *rule::parseDsl("B3/S23").ir, sim::Path::Cpu, 5, 6);
        auto s = std::get<sim::Simulation>(std::move(made));
        s.setRuleMutation({true, 7, 2});
        s.setCellMutation(p);
        for (int i = 0; i < 70; ++i) s.step();
        std::vector<uint64_t> hashes;
        for (const auto& e : s.lineage().entries()) hashes.push_back(e.ir_hash);
        return hashes;
    };
    CHECK(run(0.0) == run(0.05));
}
