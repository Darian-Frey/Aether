#include "rule/dsl.hpp"
#include "sim/session.hpp"
#include "sim/simulation.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;
using sim::Path;
using sim::Simulation;

namespace {

std::vector<uint8_t> cells(Simulation& s) {
    s.syncToHost();
    return {s.host().current().begin(), s.host().current().end()};
}

Simulation unwrap(std::variant<Simulation, core::Error>&& v) {
    if (const auto* e = std::get_if<core::Error>(&v)) FAIL(e->message);
    return std::get<Simulation>(std::move(v));
}

// A run with everything that can happen in it: fills, paints, parameter
// changes, a user rule change, a rule-only rewind, both mutations.
Simulation busyRun(Path path, int generations) {
    auto s = unwrap(Simulation::create(core::GridSpec{2, 40, 30, 1}, *rule::parseDsl("B3/S23").ir, path, 101, 202));
    const double d[1] = {0.4};
    s.fillRandom(d);
    s.setRuleMutation({true, 25, 1});
    s.setCellMutation(0.002);
    for (int g = 0; g < generations; ++g) {
        if (g == 10) { s.paintSpan(3, 20, 7, 0, 1); s.paintSpan(3, 20, 8, 0, 0); }
        if (g == 40) (void)s.setRule(*rule::parseDsl("B36/S23").ir);
        if (g == 60) s.setCellMutation(0.0);
        if (g == 70) (void)s.rewind(0);
        if (g == 90) { const double d2[1] = {0.2}; s.fillRandom(d2); }
        if (g == 95) s.setRuleMutation({true, 7, 2});
        s.step();
    }
    return s;
}

std::string tmpPath(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

}  // namespace

TEST_CASE("cell codec round-trips and rejects bad sizes", "[session]") {
    std::vector<uint8_t> c;
    for (int i = 0; i < 1000; ++i) c.push_back(static_cast<uint8_t>(i < 600 ? 0 : (i / 7 % 3)));
    const std::string text = sim::encodeCells(c);
    CHECK(text.size() < 400);   // runs compress: 600 zeros are three pairs
    const auto back = sim::decodeCells(text, c.size());
    REQUIRE(std::holds_alternative<std::vector<uint8_t>>(back));
    CHECK(std::get<std::vector<uint8_t>>(back) == c);
    CHECK(std::holds_alternative<sim::SessionError>(sim::decodeCells(text, c.size() + 1)));
    CHECK(std::holds_alternative<std::vector<uint8_t>>(sim::decodeCells(sim::encodeCells({}), 0)));
}

TEST_CASE("a session round-trips through JSON text", "[gpu][session]") {
    GlContext gl;
    requireGl(gl);
    auto s = busyRun(Path::Gpu, 120);
    const sim::Session snap = s.session();
    const std::string text = sim::sessionToJson(snap);
    CHECK(text.find("\"format_version\": 1") != std::string::npos);
    auto back = sim::sessionFromJson(text);
    if (const auto* e = std::get_if<sim::SessionError>(&back)) FAIL(e->message);
    const sim::Session& b = std::get<sim::Session>(back);
    CHECK(b.spec == snap.spec);
    CHECK(b.initial == snap.initial);
    CHECK(b.current == snap.current);
    CHECK(b.seedA == snap.seedA);
    CHECK(b.seedB == snap.seedB);
    CHECK(b.generation == 120);
    CHECK(b.journal.size() == snap.journal.size());
    REQUIRE(b.lineage.size() == snap.lineage.size());
    for (size_t i = 0; i < b.lineage.size(); ++i) {
        CHECK(b.lineage[i].ir_hash == snap.lineage[i].ir_hash);
        CHECK(b.lineage[i].ir == snap.lineage[i].ir);
        CHECK(b.lineage[i].origin == snap.lineage[i].origin);
        CHECK(b.lineage[i].journal_index == snap.lineage[i].journal_index);
    }
    CHECK(b.rule == snap.rule);
    CHECK(b.streamA->state == snap.streamA->state);
    CHECK(b.ruleMutation.interval == 7);
    CHECK(b.cellMutationP == 0.0);
}

TEST_CASE("an unknown format version is an error, not a best-effort parse", "[session]") {
    auto r = sim::sessionFromJson("{\"format_version\": 2}");
    REQUIRE(std::holds_alternative<sim::SessionError>(r));
    CHECK(std::get<sim::SessionError>(r).message.find("format_version 2") != std::string::npos);
    CHECK(std::holds_alternative<sim::SessionError>(sim::sessionFromJson("not json")));
}

TEST_CASE("replay from the initial state reproduces the run bitwise (AV-006)", "[gpu][session][replay]") {
    GlContext gl;
    requireGl(gl);
    auto original = busyRun(Path::Gpu, 300);
    const sim::Session snap = original.session();
    const auto expected = cells(original);

    // Replay on both paths, to the same generation, from initial + journal.
    for (Path p : {Path::Gpu, Path::Cpu}) {
        auto replayed = unwrap(Simulation::replay(snap, {300}, p));
        CHECK(replayed.generation() == 300);
        CHECK(cells(replayed) == expected);
        REQUIRE(replayed.lineage().size() == snap.lineage.size());
        for (size_t i = 0; i < snap.lineage.size(); ++i) {
            CHECK(replayed.lineage().at(i).ir_hash == snap.lineage[i].ir_hash);
            CHECK(replayed.lineage().at(i).generation == snap.lineage[i].generation);
        }
        CHECK(replayed.journal().size() == snap.journal.size());
        CHECK(replayed.streamA().state().state == snap.streamA->state);
    }
}

TEST_CASE("resume continues exactly where a replay would", "[gpu][session]") {
    GlContext gl;
    requireGl(gl);
    auto original = busyRun(Path::Gpu, 150);
    const sim::Session snap = original.session();

    // Continue the original 100 more; resume the snapshot 100 more.
    for (int i = 0; i < 100; ++i) original.step();
    auto resumed = unwrap(Simulation::resume(snap, Path::Cpu));
    CHECK(resumed.generation() == 150);
    for (int i = 0; i < 100; ++i) resumed.step();
    CHECK(cells(resumed) == cells(original));
    CHECK(resumed.lineage().size() == original.lineage().size());
    CHECK(resumed.lineage().back().ir_hash == original.lineage().back().ir_hash);

    // A session without stored state resumes by replay.
    sim::Session bare = snap;
    bare.current.clear();
    bare.streamA.reset();
    auto viaReplay = unwrap(Simulation::resume(bare, Path::Gpu));
    CHECK(viaReplay.generation() == 150);
    CHECK(cells(viaReplay) == snap.current);
}

TEST_CASE("rewindGrid lands on the entry's grid and rule, and truncates the future", "[gpu][session]") {
    GlContext gl;
    requireGl(gl);
    auto original = busyRun(Path::Gpu, 200);
    const sim::Session snap = original.session();
    REQUIRE(snap.lineage.size() >= 4);

    // Pick a mutation entry and a user entry and check both.
    for (size_t i = 1; i < snap.lineage.size(); ++i) {
        const auto& entry = snap.lineage[i];
        if (entry.origin != sim::LineageOrigin::Mutation && entry.origin != sim::LineageOrigin::User) continue;
        auto back = unwrap(Simulation::rewindGrid(snap, i, Path::Gpu));
        CHECK(back.generation() == entry.generation);
        CHECK(rule::irHash(back.rule()) == entry.ir_hash);
        CHECK(back.lineage().size() == i + 1);
        CHECK(back.lineage().back().ir_hash == entry.ir_hash);
        // Its own replay to the same point agrees with itself.
        const auto snap2 = back.session();
        auto again = unwrap(Simulation::replay(snap2, {entry.generation, SIZE_MAX, true}, Path::Cpu));
        CHECK(cells(again) == snap2.current);
        break;
    }
}

TEST_CASE("save and load through a file, inline and sidecar", "[gpu][session]") {
    GlContext gl;
    requireGl(gl);
    auto s = busyRun(Path::Gpu, 50);
    const sim::Session snap = s.session();
    const std::string path = tmpPath("aether_test_session.aether");
    REQUIRE_FALSE(sim::saveSession(path, snap).has_value());
    auto loaded = sim::loadSession(path);
    if (const auto* e = std::get_if<sim::SessionError>(&loaded)) FAIL(e->message);
    CHECK(std::get<sim::Session>(loaded).current == snap.current);
    CHECK(std::get<sim::Session>(loaded).initial == snap.initial);
    std::remove(path.c_str());

    // A big grid goes to a sidecar and comes back the same.
    auto big = unwrap(Simulation::create(core::GridSpec{2, 2048, 2100, 1}, *rule::parseDsl("B3/S23").ir, Path::Gpu, 1, 2));
    const double d[1] = {0.3};
    big.fillRandom(d);
    big.step();
    const sim::Session bigSnap = big.session();
    const std::string bigPath = tmpPath("aether_test_big.aether");
    REQUIRE_FALSE(sim::saveSession(bigPath, bigSnap).has_value());
    CHECK(std::filesystem::exists(bigPath + ".grid"));
    auto bigLoaded = sim::loadSession(bigPath);
    if (const auto* e = std::get_if<sim::SessionError>(&bigLoaded)) FAIL(e->message);
    CHECK(std::get<sim::Session>(bigLoaded).initial == bigSnap.initial);
    CHECK(std::get<sim::Session>(bigLoaded).current == bigSnap.current);
    std::remove(bigPath.c_str());
    std::remove((bigPath + ".grid").c_str());

    CHECK(std::holds_alternative<sim::SessionError>(sim::loadSession(tmpPath("does_not_exist.aether"))));
}
