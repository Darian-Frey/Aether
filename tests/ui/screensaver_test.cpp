// The screensaver's playlist (F-024).
//
// The playlist is where the screensaver decides what to show, and it is
// deterministic so that a run which showed something worth keeping can be
// found again. That property is what these check; the fullscreen loop around
// it needs a screen and does not.

#include "ui/screensaver.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <vector>

using namespace aether;
using ui::Playlist;
using ui::PlaylistEntry;
using ui::PlaylistOptions;

namespace {

std::vector<PlaylistEntry> take(Playlist& p, int n) {
    std::vector<PlaylistEntry> out;
    for (int i = 0; i < n; ++i) out.push_back(p.next());
    return out;
}

}  // namespace

TEST_CASE("the same seed shows the same things in the same order", "[screensaver]") {
    Playlist a(15, 1234);
    Playlist b(15, 1234);
    CHECK(take(a, 30) == take(b, 30));
}

TEST_CASE("a different seed shows something else", "[screensaver]") {
    Playlist a(15, 1);
    Playlist b(15, 2);
    CHECK_FALSE(take(a, 20) == take(b, 20));
}

TEST_CASE("no rule follows itself", "[screensaver]") {
    // A screensaver that repeats immediately reads as broken rather than
    // random, and with fifteen rules it would otherwise happen every fifteenth.
    Playlist p(15, 99);
    const auto entries = take(p, 500);
    for (size_t i = 1; i < entries.size(); ++i) {
        INFO("entry " << i);
        CHECK(entries[i].rule != entries[i - 1].rule);
    }
}

TEST_CASE("every rule gets shown", "[screensaver]") {
    Playlist p(8, 7);
    std::set<size_t> seen;
    for (const auto& e : take(p, 400)) seen.insert(e.rule);
    CHECK(seen.size() == 8);
}

TEST_CASE("a rule index is always in range", "[screensaver]") {
    for (size_t count : {1u, 2u, 3u, 15u}) {
        Playlist p(count, count * 31);
        for (const auto& e : take(p, 200)) {
            INFO("count " << count);
            CHECK(e.rule < count);
        }
    }
}

TEST_CASE("one rule repeats rather than deadlocking", "[screensaver]") {
    // The "never twice running" rule cannot be honoured with one rule, and
    // must not be honoured by looping forever looking for another.
    Playlist p(1, 5);
    for (const auto& e : take(p, 10)) CHECK(e.rule == 0);
}

TEST_CASE("an empty library asks for nothing", "[screensaver]") {
    Playlist p(0, 5);
    const PlaylistEntry e = p.next();
    CHECK(e.rule == 0);
    CHECK_FALSE(e.ruleMutation);
}

TEST_CASE("entries differ in their grid seeds, so a rule is reseeded each time", "[screensaver]") {
    Playlist p(4, 21);
    const auto entries = take(p, 40);
    std::set<uint64_t> seeds;
    for (const auto& e : entries) seeds.insert(e.seedA);
    // Forty draws from a 64-bit space: a collision would mean the seed is not
    // being advanced rather than bad luck.
    CHECK(seeds.size() == entries.size());
}

TEST_CASE("mutation settings stay inside the bounds the engine accepts", "[screensaver]") {
    Playlist p(10, 4242);
    for (const auto& e : take(p, 400)) {
        if (e.ruleMutation) {
            CHECK(e.ruleInterval >= 1);
            CHECK(e.ruleMagnitude >= 1);
        }
        CHECK(e.cellMutationP >= 0.0);
        CHECK(e.cellMutationP < 0.01);   // a speckle, not a snowstorm
    }
}

TEST_CASE("the drift and noise chances are honoured, and can be turned off", "[screensaver]") {
    PlaylistOptions none;
    none.driftChance = 0.0;
    none.noiseChance = 0.0;
    Playlist quiet(10, 8, none);
    for (const auto& e : take(quiet, 100)) {
        CHECK_FALSE(e.ruleMutation);
        CHECK(e.cellMutationP == 0.0);
    }

    PlaylistOptions all;
    all.driftChance = 1.0;
    all.noiseChance = 1.0;
    Playlist busy(10, 8, all);
    for (const auto& e : take(busy, 100)) {
        CHECK(e.ruleMutation);
        CHECK(e.cellMutationP > 0.0);
    }
}

TEST_CASE("an entry lasts at least a second whatever it was asked for", "[screensaver]") {
    PlaylistOptions silly;
    silly.seconds = -5.0;
    Playlist p(3, 1, silly);
    CHECK(p.options().seconds >= 1.0);
}
