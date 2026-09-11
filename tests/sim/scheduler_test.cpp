#include "sim/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

using aether::sim::Scheduler;

namespace {

// A clock the test advances by hand, and a step that costs a fixed time.
struct Harness {
    Scheduler s;
    double    now = 0.0;
    double    stepCost = 0.0;
    int       steps = 0;

    uint32_t update(double dt) {
        return s.update(dt, [&] { ++steps; now += stepCost; }, [&] { return now; });
    }
};

}  // namespace

TEST_CASE("target rate below frame rate accumulates fractional steps", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(30.0);
    int total = 0;
    for (int i = 0; i < 60; ++i) total += static_cast<int>(h.update(1.0 / 60.0));
    CHECK(total == 30);
    CHECK_FALSE(h.s.stats().below_target);
}

TEST_CASE("target rate above frame rate runs several steps per frame", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(1000.0);
    h.s.setMaxStepsPerFrame(100);
    int total = 0;
    for (int i = 0; i < 60; ++i) total += static_cast<int>(h.update(1.0 / 60.0));
    CHECK(total == 1000);
}

TEST_CASE("a very low rate steps once every few seconds", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(0.5);
    int total = 0;
    for (int i = 0; i < 240; ++i) total += static_cast<int>(h.update(1.0 / 60.0));   // 4 s
    CHECK(total == 2);
}

TEST_CASE("the per-frame cap drops the excess rather than carrying a debt", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(100000.0);
    h.s.setMaxStepsPerFrame(16);
    CHECK(h.update(1.0 / 60.0) == 16);
    CHECK(h.s.stats().below_target);
    // The next frame is not owed the thousands it missed.
    CHECK(h.update(1.0 / 60.0) == 16);
}

TEST_CASE("the wall-clock budget cuts a frame short and reports it", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(600.0);          // 10 per frame at 60 Hz
    h.s.setMaxStepsPerFrame(100);
    h.s.setFrameBudget(0.004);
    h.stepCost = 0.001;                // budget allows ~4
    const uint32_t n = h.update(1.0 / 60.0);
    CHECK(n >= 4);
    CHECK(n < 10);
    CHECK(h.s.stats().below_target);
    CHECK(h.s.stats().steps_last_frame == n);
}

TEST_CASE("pause stops stepping; single-step works while paused", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(60.0);
    h.s.setPaused(true);
    for (int i = 0; i < 10; ++i) CHECK(h.update(1.0 / 60.0) == 0);
    h.s.requestSingleStep();
    CHECK(h.update(1.0 / 60.0) == 1);
    CHECK(h.update(1.0 / 60.0) == 0);
    h.s.setPaused(false);
    CHECK(h.update(1.0 / 60.0) == 1);
}

TEST_CASE("pausing discards accumulated time", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(60.0);
    h.update(0.5 / 60.0);   // half a step banked
    h.s.setPaused(true);
    h.s.setPaused(false);
    CHECK(h.update(0.5 / 60.0) == 0);   // would be 1 had the bank survived
}

TEST_CASE("burst runs the requested count across frames, ignoring pause and rate", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(0.0);
    h.s.setPaused(true);
    h.s.setMaxStepsPerFrame(8);
    h.s.requestBurst(20);
    CHECK(h.update(1.0 / 60.0) == 8);
    CHECK(h.s.burstRemaining() == 12);
    CHECK(h.update(1.0 / 60.0) == 8);
    CHECK(h.update(1.0 / 60.0) == 4);
    CHECK(h.s.burstRemaining() == 0);
    CHECK(h.update(1.0 / 60.0) == 0);
    CHECK(h.steps == 20);
}

TEST_CASE("burst respects the budget without losing count", "[scheduler]") {
    Harness h;
    h.s.setMaxStepsPerFrame(100);
    h.s.setFrameBudget(0.003);
    h.stepCost = 0.001;
    h.s.requestBurst(10);
    int frames = 0;
    while (h.s.burstRemaining() > 0 && frames < 100) { h.update(1.0 / 60.0); ++frames; }
    CHECK(h.steps == 10);
    CHECK(frames >= 3);
    h.s.requestBurst(5);
    h.s.cancelBurst();
    CHECK(h.update(1.0 / 60.0) == 1);   // back to the 60 gps default
}

TEST_CASE("achieved rate is reported", "[scheduler]") {
    Harness h;
    h.s.setTargetRate(120.0);
    for (int i = 0; i < 30; ++i) h.update(1.0 / 60.0);
    CHECK(h.s.stats().achieved_gps > 100.0);
    CHECK(h.s.stats().achieved_gps < 140.0);
}
