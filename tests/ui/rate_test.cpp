// The step-rate ladder (IMP-012).
//
// Pure arithmetic, so it is tested without a window. What matters is that every
// stop is a round number, that stepping from an arbitrary rate goes somewhere
// sensible rather than jumping, and that the ends do not run off.

#include "ui/rate.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::ui;

TEST_CASE("the ladder is 1-2-5 and ascending", "[rate]") {
    CHECK(kRateStops == 16);
    CHECK(rateAt(0) == 0.1);
    CHECK(rateAt(kRateStops - 1) == 10000.0);
    for (int i = 1; i < kRateStops; ++i) CHECK(rateAt(i) > rateAt(i - 1));

    // Every stop is a round number, which is the whole point: the mantissa is
    // 1, 2 or 5 at every decade.
    for (int i = 0; i < kRateStops; ++i) {
        double m = rateAt(i);
        while (m < 1.0) m *= 10.0;
        while (m >= 10.0) m /= 10.0;
        const int mantissa = static_cast<int>(m + 0.5);
        CHECK((mantissa == 1 || mantissa == 2 || mantissa == 5));
    }

    // The ends clamp rather than reading past the array.
    CHECK(rateAt(-5) == rateAt(0));
    CHECK(rateAt(999) == rateAt(kRateStops - 1));
}

TEST_CASE("the nearest stop is nearest by ratio", "[rate]") {
    CHECK(rateAt(nearestRateStop(60.0)) == 50.0);
    CHECK(rateAt(nearestRateStop(1.0)) == 1.0);

    // 70 is nearer 50 than 100 by *difference* and nearer 100 by *ratio*, and
    // the ratio is what the eye agrees with on a logarithmic control: 70/50 is
    // 1.4 against 100/70's 1.43 — so this one is genuinely close, and 80 is
    // where the answer is unambiguous.
    CHECK(rateAt(nearestRateStop(80.0)) == 100.0);
    CHECK(rateAt(nearestRateStop(30.0)) == 20.0);

    // Beyond the ends, the ends.
    CHECK(nearestRateStop(0.001) == 0);
    CHECK(nearestRateStop(1e9) == kRateStops - 1);
}

TEST_CASE("stepping from an arbitrary rate does not jump", "[rate]") {
    // A rate from a session or the command line is any double at all. Stepping
    // works from the *rate* rather than from a remembered slider position, so
    // 47 gen/s goes to 50 when asked to go faster and 20 when asked to go
    // slower — not to wherever the control was last left.
    CHECK(rateAt(fasterStop(47.0)) == 50.0);
    CHECK(rateAt(slowerStop(47.0)) == 20.0);

    // Exactly on a stop, a step moves to the next one rather than standing
    // still, which is what makes a key repeat feel like a key repeat.
    CHECK(rateAt(fasterStop(50.0)) == 100.0);
    CHECK(rateAt(slowerStop(50.0)) == 20.0);

    // And the ends hold.
    CHECK(rateAt(slowerStop(0.1)) == 0.1);
    CHECK(rateAt(fasterStop(10000.0)) == 10000.0);
    CHECK(rateAt(slowerStop(0.001)) == 0.1);
}
