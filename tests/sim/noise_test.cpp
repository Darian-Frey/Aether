// Patchy noise for seeding a resource (F-032 step 2).
//
// Pure and GL-free, so this file is about the numbers rather than about a run.
// What it pins: the output is in range and fills it, the same seed gives the same
// field, a different seed gives a different one, the draws depend on the
// parameters and the grid rather than on the values drawn, and the result is
// *patchy* — which is the whole reason this exists instead of a uniform fill,
// and the one property a test could easily fail to check.

#include "sim/noise.hpp"
#include "sim/rng.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace aether;

namespace {

core::GridSpec spec2d(uint32_t w, uint32_t h) {
    core::GridSpec s;
    s.dimensions = 2;
    s.width = w;
    s.height = h;
    return s;
}

std::vector<float> noiseOf(const core::GridSpec& spec, const sim::NoiseParams& p, uint64_t seed) {
    std::vector<float> out(spec.cellCount());
    sim::Pcg32 stream(seed);
    sim::fillNoise(spec, p, stream, out);
    return out;
}

// The mean absolute difference between horizontally adjacent cells, as a
// fraction of the field's range. Small means smooth, which is what patchy looks
// like from here; white noise would be around a third.
double neighbourVariation(const std::vector<float>& v, const core::GridSpec& spec) {
    double total = 0.0;
    size_t pairs = 0;
    for (uint32_t y = 0; y < spec.height; ++y) {
        for (uint32_t x = 0; x + 1 < spec.width; ++x) {
            const size_t i = size_t{y} * spec.width + x;
            total += std::fabs(static_cast<double>(v[i + 1]) - static_cast<double>(v[i]));
            ++pairs;
        }
    }
    return pairs == 0 ? 0.0 : total / static_cast<double>(pairs);
}

}  // namespace

TEST_CASE("noise stays inside the range it was given", "[noise]") {
    const auto spec = spec2d(64, 48);
    sim::NoiseParams p;
    p.low = 0.25f;
    p.high = 0.75f;
    const auto v = noiseOf(spec, p, 1234);

    REQUIRE(v.size() == spec.cellCount());
    const auto lo = *std::min_element(v.begin(), v.end());
    const auto hi = *std::max_element(v.begin(), v.end());
    CHECK(lo >= p.low);
    CHECK(hi <= p.high);
    // And uses it: a generator that returned the midpoint everywhere would pass
    // the two checks above and be useless.
    CHECK(hi - lo > 0.15f);
}

TEST_CASE("noise is patchy rather than white", "[noise]") {
    // The design note's reason for this feature existing: "uniform resources
    // produce uniform populations and nothing interesting happens". Neither
    // uniform nor uncorrelated will do.
    const auto spec = spec2d(96, 96);
    sim::NoiseParams p;
    p.frequency = 4;
    p.octaves = 3;
    const auto v = noiseOf(spec, p, 99);

    const double variation = neighbourVariation(v, spec);
    CHECK(variation > 0.0);        // not flat
    CHECK(variation < 0.05);       // and neighbours resemble each other

    // A finer lattice is less smooth, which is what the frequency knob means.
    sim::NoiseParams fine = p;
    fine.frequency = 24;
    CHECK(neighbourVariation(noiseOf(spec, fine, 99), spec) > variation);
}

TEST_CASE("noise is reproducible and seed-dependent", "[noise]") {
    const auto spec = spec2d(40, 40);
    const sim::NoiseParams p;
    CHECK(noiseOf(spec, p, 7) == noiseOf(spec, p, 7));
    CHECK(noiseOf(spec, p, 7) != noiseOf(spec, p, 8));

    // Different parameters, same seed: a different field, not a rescaling of one.
    sim::NoiseParams more = p;
    more.octaves = p.octaves + 1;
    CHECK(noiseOf(spec, p, 7) != noiseOf(spec, more, 7));
}

TEST_CASE("the number of draws depends on the parameters and not the values", "[noise]") {
    // The same discipline fillRandom keeps: if the draw count varied with what
    // came out, every later draw in the session would move when a parameter did.
    const auto spec = spec2d(37, 23);
    const sim::NoiseParams p;

    auto drawsFor = [&](uint64_t seed) {
        std::vector<float> out(spec.cellCount());
        sim::Pcg32 stream(seed);
        sim::fillNoise(spec, p, stream, out);
        // How far the stream moved, counted by how many draws it takes a fresh
        // stream to reach the same place.
        sim::Pcg32 fresh(seed);
        size_t n = 0;
        while (fresh.state().state != stream.state().state) {
            fresh.next();
            ++n;
            REQUIRE(n < 1'000'000);
        }
        return n;
    };
    CHECK(drawsFor(1) == drawsFor(2));
    CHECK(drawsFor(1) > 0);
}

TEST_CASE("noise works in one and three dimensions", "[noise]") {
    core::GridSpec oneD;
    oneD.dimensions = 1;
    oneD.width = 128;
    const auto line = noiseOf(oneD, sim::NoiseParams{}, 5);
    REQUIRE(line.size() == 128);
    CHECK(*std::max_element(line.begin(), line.end()) > *std::min_element(line.begin(), line.end()));

    core::GridSpec threeD;
    threeD.dimensions = 3;
    threeD.width = 16;
    threeD.height = 12;
    threeD.depth = 10;
    const auto volume = noiseOf(threeD, sim::NoiseParams{}, 5);
    REQUIRE(volume.size() == 16u * 12u * 10u);
    CHECK(*std::max_element(volume.begin(), volume.end()) > *std::min_element(volume.begin(), volume.end()));
}

TEST_CASE("degenerate parameters are clamped rather than refused", "[noise]") {
    // A caller reaching this with zeroes has a bug, but a seed that divided by
    // one of them would be a crash rather than a poor-looking world.
    const auto spec = spec2d(32, 32);
    sim::NoiseParams p;
    p.frequency = 0;
    p.octaves = 0;
    const auto v = noiseOf(spec, p, 3);
    REQUIRE(v.size() == spec.cellCount());
    for (float f : v) CHECK(std::isfinite(f));
}
