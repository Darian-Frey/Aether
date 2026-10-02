// The step-rate ladder (F-014's control, IMP-012).
//
// The rate slider used to span five decades of generations per second as a
// continuous logarithm. Five decades across 180 pixels is about 36 pixels a
// decade, so a pixel of drag was a 6% change and landing on a particular rate
// was luck — worst at the slow end, which is exactly where somebody watching an
// automaton wants to be.
//
// So the control steps along a 1-2-5 ladder instead. Every stop is a round
// number, sixteen of them cover the same five decades, and a key can move one
// stop at a time. 1-2-5 rather than powers of two because the stops people ask
// for out loud — one, ten, sixty, a thousand — are round in decimal.
//
// Pure, so it is tested without a window.

#pragma once

#include <cstddef>
#include <span>

namespace aether::ui {

// Generations per second, ascending. The one place these numbers are written.
inline constexpr double kRateLadder[] = {
    0.1, 0.2, 0.5,
    1.0, 2.0, 5.0,
    10.0, 20.0, 50.0,
    100.0, 200.0, 500.0,
    1000.0, 2000.0, 5000.0,
    10000.0,
};
inline constexpr int kRateStops = static_cast<int>(std::size(kRateLadder));

inline std::span<const double> rateLadder() { return kRateLadder; }

// The rate at a stop, clamped to the ladder's ends.
inline double rateAt(int stop) {
    if (stop < 0) return kRateLadder[0];
    if (stop >= kRateStops) return kRateLadder[kRateStops - 1];
    return kRateLadder[stop];
}

// The stop nearest `gps`, measured in *ratio* rather than difference: the
// ladder is geometric, so 70 gen/s is nearer to 50 than to 100 by difference
// and nearer to 100 by ratio, and the ratio is what the eye agrees with on a
// logarithmic control.
inline int nearestRateStop(double gps) {
    if (gps <= kRateLadder[0]) return 0;
    if (gps >= kRateLadder[kRateStops - 1]) return kRateStops - 1;
    int best = 0;
    double bestRatio = 0.0;
    for (int i = 0; i < kRateStops; ++i) {
        const double a = kRateLadder[i] > gps ? gps / kRateLadder[i] : kRateLadder[i] / gps;
        if (a > bestRatio) {
            bestRatio = a;
            best = i;
        }
    }
    return best;
}

// One stop slower or faster than whatever rate is in force, which is what a key
// press means. Taken from the *rate* rather than from a remembered index, so a
// rate set from a session or the command line steps sensibly rather than
// jumping to wherever the slider happened to be left.
inline int slowerStop(double gps) {
    const int at = nearestRateStop(gps);
    // A rate between two stops steps to the one below it, not past it.
    return rateAt(at) < gps ? at : (at > 0 ? at - 1 : 0);
}
inline int fasterStop(double gps) {
    const int at = nearestRateStop(gps);
    return rateAt(at) > gps ? at : (at + 1 < kRateStops ? at + 1 : kRateStops - 1);
}

}  // namespace aether::ui
