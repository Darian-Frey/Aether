#include "core/grid.hpp"
#include "sim/fill.hpp"
#include "rule/dsl.hpp"
#include "sim/rng.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

using aether::sim::Pcg32;

TEST_CASE("PCG32 matches the reference implementation's published sequence", "[rng]") {
    // pcg32_srandom_r(&rng, 42u, 54u), first outputs from the PCG demo.
    Pcg32 rng(42, 54);
    const std::array<uint32_t, 6> expected = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u,
                                              0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) CHECK(rng.next() == e);
}

TEST_CASE("same seed, same stream; different seed, different stream", "[rng]") {
    Pcg32 a(7), b(7), c(8);
    for (int i = 0; i < 100; ++i) {
        const uint32_t x = a.next();
        CHECK(x == b.next());
    }
    bool differs = false;
    Pcg32 a2(7);
    for (int i = 0; i < 10; ++i) differs |= (a2.next() != c.next());
    CHECK(differs);
}

TEST_CASE("below() stays in range and covers the range", "[rng]") {
    Pcg32 rng(1);
    std::array<int, 7> seen{};
    for (int i = 0; i < 7000; ++i) {
        const uint32_t v = rng.below(7);
        REQUIRE(v < 7);
        ++seen[v];
    }
    for (int n : seen) CHECK(n > 800);
}

TEST_CASE("random fill is reproducible and honours densities", "[rng]") {
    aether::core::HostGrid g({2, 200, 200, 1});
    const double density[2] = {0.30, 0.10};   // state 1: 30%, state 2: 10%
    Pcg32 rng(99);
    aether::sim::fillRandom(g, density, rng);

    std::array<int, 3> counts{};
    for (uint8_t c : g.current()) { REQUIRE(c < 3); ++counts[c]; }
    const double n = 40000.0;
    CHECK(counts[1] / n > 0.28); CHECK(counts[1] / n < 0.32);
    CHECK(counts[2] / n > 0.085); CHECK(counts[2] / n < 0.115);

    aether::core::HostGrid h({2, 200, 200, 1});
    Pcg32 rng2(99);
    aether::sim::fillRandom(h, density, rng2);
    CHECK(std::vector<uint8_t>(g.current().begin(), g.current().end()) ==
          std::vector<uint8_t>(h.current().begin(), h.current().end()));

    // One draw per cell: the streams are in the same place afterwards.
    CHECK(rng.next() == rng2.next());
}

TEST_CASE("the default densities never starve the last states (BUG-008)", "[rng]") {
    using aether::rule::parseDsl;
    for (const char* src : {"B3/S23", "B2/S/C3", "B2/S/C25",
                            "states 4; neighbourhood moore 1; 1: n(0) >= 0 -> 2;",
                            "states 2; neighbourhood moore 1; decay 60; 0: n(1) == 3 -> 1; 1: n(1) < 2 -> 0;"}) {
        const auto ir = *parseDsl(src).ir;
        const auto density = aether::sim::defaultDensity(ir);
        INFO(src);
        REQUIRE(density.size() == ir.states - 1u);
        double total = 0.0;
        for (double d : density) { CHECK(d >= 0.0); total += d; }
        CHECK(total <= 1.0);
        // Every state the rule lives in is reachable from a fill.
        const uint16_t live = ir.metadata.decay_from.value_or(ir.states);
        for (uint16_t s = 1; s < live; ++s) CHECK(density[s - 1u] > 0.0);
        // The ageing tail is not seeded: a half-faded cell is no way to start.
        for (uint16_t s = live; s < ir.states; ++s) CHECK(density[s - 1u] == 0.0);
    }
}

TEST_CASE("a many-state rule is seeded evenly across all its states", "[rng]") {
    // The fourteen-state cyclic rule is what found BUG-008: with weights that
    // summed past one, states ten and up never appeared.
    aether::rule::RuleIR ir;
    ir.states = 14;
    const auto density = aether::sim::defaultDensity(ir);
    for (double d : density) CHECK_THAT(d, Catch::Matchers::WithinAbs(1.0 / 14.0, 1e-12));

    aether::core::HostGrid g({2, 200, 200, 1});
    Pcg32 rng(5);
    aether::sim::fillRandom(g, density, rng);
    std::array<int, 14> seen{};
    for (uint8_t c : g.current()) { REQUIRE(c < 14); ++seen[c]; }
    for (int n : seen) {
        CHECK(n > 2400);   // 40000 / 14 = 2857
        CHECK(n < 3400);
    }
}

TEST_CASE("a two-state rule with no table to read keeps the conventional soup", "[rng]") {
    // A bare IR has an empty table, so there is no band to read and 30% is
    // what it falls back to. The same fallback covers an expression rule and
    // anything with an ageing tail (BUG-023).
    aether::rule::RuleIR ir;
    ir.states = 2;
    const auto density = aether::sim::defaultDensity(ir);
    REQUIRE(density.size() == 1);
    CHECK(density[0] == 0.3);
}

TEST_CASE("a two-state rule is seeded into the band it is alive in (BUG-023)", "[rng]") {
    using aether::rule::parseDsl;
    const uint32_t N2 = 8, N3 = 26;

    struct Case { const char* src; uint8_t dims; double band; uint32_t N; const char* why; };
    const Case cases[] = {
        // Life: survive 2-3, born 3. The band is {2, 3}, so 2.5 expected
        // neighbours — which is where the conventional 0.3 came from.
        {"B3/S23", 2, 2.5, N2, "2D Life"},
        // Bays' 4555 in 3D: survive 4-5, born 5. Band {4, 5}.
        {"B5/S45", 3, 4.5, N3, "3D 4555"},
        // 5766: survive 5-7, born 6. Band {5, 6, 7}.
        {"B6/S567", 3, 6.0, N3, "3D 5766"},
        // A birth count outside the survival band widens it: {2, 3, 6}.
        {"B36/S23", 2, (2.0 + 3.0 + 6.0) / 3.0, N2, "HighLife"},
    };

    for (const Case& c : cases) {
        INFO(c.why << ": " << c.src);
        const auto parsed = parseDsl(c.src, {c.dims, aether::rule::Boundary::Wrap});
        REQUIRE(parsed.ir);
        const auto density = aether::sim::defaultDensity(*parsed.ir);
        REQUIRE(density.size() == 1);
        CHECK_THAT(density[0], Catch::Matchers::WithinAbs(c.band / c.N, 1e-9));
        // The point of the exercise: the expected number of live neighbours a
        // seeded cell sees is in the middle of the band, whatever the lattice.
        CHECK_THAT(density[0] * c.N, Catch::Matchers::WithinAbs(c.band, 1e-9));
    }

    // The 3D rules are the reason this exists. Under a flat 0.3 they saw 7.8
    // expected neighbours against a rule that survives on four or five.
    const auto bays = parseDsl("B5/S45", {3, aether::rule::Boundary::Wrap});
    REQUIRE(bays.ir);
    const double d = aether::sim::defaultDensity(*bays.ir)[0];
    CHECK(d < 0.2);
    CHECK(d > 0.15);
    CHECK(0.3 * N3 > 7.0);   // what it used to be
}

TEST_CASE("a lifespan rule is seeded at age 1, not spread across its ages (BUG-026)", "[rng]") {
    using aether::rule::parseDsl;
    // The states of a lifespan rule are one cell growing older. Spreading cells
    // evenly across them seeds (live-1)/live of the grid alive — 88.9% at
    // lifespan 8 — and nothing Life-like survives that.
    const auto spanned = parseDsl("states 2; neighbourhood moore 1; lifespan 8;"
                                  "0: n(1) == 2 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(spanned.ir);
    REQUIRE(spanned.ir->states == 9);
    REQUIRE(spanned.ir->metadata.lifespan == 8);

    const auto density = aether::sim::defaultDensity(*spanned.ir);
    REQUIRE(density.size() == 8);
    // Age 1 takes the whole share and every later age takes none.
    CHECK_THAT(density[0], Catch::Matchers::WithinAbs(2.5 / 8.0, 1e-9));
    for (size_t i = 1; i < density.size(); ++i) {
        INFO("age " << i + 1);
        CHECK(density[i] == 0.0);
    }
    // The band is the base rule's: B2/S23 is alive on 2 or 3 neighbours, so a
    // seeded cell expects 2.5 of them — the same answer the two-state branch
    // gives for the rule this was made from.
    double total = 0.0;
    for (double d : density) total += d;
    CHECK(total < 0.4);

    // A lifespan with a tail on top keeps the same answer: the tail states are
    // a dying cell and were never seeded, and the ages are now not either.
    const auto both = parseDsl("states 2; neighbourhood moore 1; lifespan 8; decay 3;"
                               "0: n(1) == 2 -> 1; 1: n(1) < 2 or n(1) > 3 -> 0;");
    REQUIRE(both.ir);
    REQUIRE(both.ir->metadata.lifespan == 8);
    const auto d2 = aether::sim::defaultDensity(*both.ir);
    REQUIRE(d2.size() == both.ir->states - 1u);
    CHECK_THAT(d2[0], Catch::Matchers::WithinAbs(2.5 / 8.0, 1e-9));
    for (size_t i = 1; i < d2.size(); ++i) {
        INFO("state " << i + 1);
        CHECK(d2[i] == 0.0);
    }
}

TEST_CASE("a many-state rule that is not ages keeps its even spread", "[rng]") {
    // The guard on the case above: a Generations rule's states are a tail, and
    // a cyclic rule's are phases. Neither carries the lifespan hint, so neither
    // is touched by it — the fix is not "seed state 1 only" in general.
    const auto gens = aether::rule::parseDsl("B2/S/C3");
    REQUIRE(gens.ir);
    CHECK_FALSE(gens.ir->metadata.lifespan.has_value());
    const auto d = aether::sim::defaultDensity(*gens.ir);
    REQUIRE(d.size() == gens.ir->states - 1u);
    CHECK(d[0] > 0.0);

    aether::rule::RuleIR cyclic;
    cyclic.states = 14;
    const auto dc = aether::sim::defaultDensity(cyclic);
    for (double v : dc) CHECK_THAT(v, Catch::Matchers::WithinAbs(1.0 / 14.0, 1e-12));
}

TEST_CASE("a two-state rule nothing survives under falls back rather than seeding empty", "[rng]") {
    // B/S is a legal rule and its band is empty: every cell is dead next
    // generation whatever the count. Dividing by nothing is the trap, so the
    // fallback catches it (BUG-023).
    const auto parsed = aether::rule::parseDsl("B/S");
    REQUIRE(parsed.ir);
    const auto density = aether::sim::defaultDensity(*parsed.ir);
    REQUIRE(density.size() == 1);
    CHECK(density[0] == 0.3);
}
