#include "core/grid.hpp"
#include "sim/fill.hpp"
#include "sim/rng.hpp"

#include <catch2/catch_test_macros.hpp>

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
