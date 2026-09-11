// Simulation rate control (F-014, AV-003).
//
// Converts a target generations-per-second into a number of steps per frame
// through an accumulator, with pause, single-step and burst. Two guards keep
// the UI alive when the target is unreachable: a hard cap on steps per frame
// and a wall-clock budget, with the shortfall reported rather than silently
// absorbed. Pure timing logic; knows nothing about grids or GL.
//
// Timing decides how many steps happen per frame, never what a step does, so
// the session's determinism (D-006) is untouched by anything here.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>

namespace aether::sim {

class Scheduler {
public:
    struct Stats {
        uint32_t steps_last_frame = 0;
        double   achieved_gps     = 0.0;   // smoothed
        bool     below_target     = false; // a cap or the budget cut the last frame short
        uint32_t effective_cap    = 0;     // the adaptive per-frame cap in force
    };

    void   setTargetRate(double gps) { target_ = std::max(0.0, gps); }
    double targetRate() const { return target_; }

    void     setMaxStepsPerFrame(uint32_t n) { maxSteps_ = std::max(1u, n); }
    uint32_t maxStepsPerFrame() const { return maxSteps_; }

    void   setFrameBudget(double seconds) { budget_ = std::max(0.0, seconds); }
    double frameBudget() const { return budget_; }

    // Frame-time feedback. GPU steps return to the host in microseconds and
    // their real cost only shows up as a long frame, which the wall-clock
    // budget cannot see. When a frame exceeds `seconds` the effective cap
    // halves; when frames are comfortably short it grows back toward the
    // hard cap. Pacing only: what a step does is unaffected (D-006).
    void   setSlowFrame(double seconds) { slowFrame_ = std::max(0.0, seconds); }
    double slowFrame() const { return slowFrame_; }

    void setPaused(bool p) { paused_ = p; if (p) accumulator_ = 0.0; }
    bool paused() const { return paused_; }

    // One step on the next update, whether or not paused.
    void requestSingleStep() { singleStep_ = true; }

    // Run `generations` as fast as the cap and budget allow, across as many
    // frames as it takes, then stop. Independent of pause and target rate.
    void     requestBurst(uint64_t generations) { burst_ = generations; }
    void     cancelBurst() { burst_ = 0; }
    uint64_t burstRemaining() const { return burst_; }

    const Stats& stats() const { return stats_; }

    // Advances by `dt` seconds of wall-clock and calls `step()` as many times
    // as due. `now()` returns seconds on any monotonic clock and is consulted
    // between steps for the budget. Returns the number of steps taken.
    template <typename Step, typename Clock>
    uint32_t update(double dt, Step&& step, Clock&& now) {
        // Adapt the cap to how the last frame actually went.
        if (effectiveCap_ == 0 || effectiveCap_ > maxSteps_) effectiveCap_ = maxSteps_;
        if (slowFrame_ > 0.0 && dt > 0.0) {
            if (dt > slowFrame_ && stats_.steps_last_frame > 1) {
                effectiveCap_ = std::max(1u, stats_.steps_last_frame / 2);
            } else if (dt < slowFrame_ * 0.5 && effectiveCap_ < maxSteps_ &&
                       stats_.steps_last_frame >= effectiveCap_) {
                effectiveCap_ = std::min(maxSteps_, effectiveCap_ + effectiveCap_ / 2 + 1);
            }
        }
        const uint32_t cap = effectiveCap_;

        uint32_t due = 0;
        bool capped = false;

        if (singleStep_) {
            singleStep_ = false;
            due = 1;
        } else if (burst_ > 0) {
            due = static_cast<uint32_t>(std::min<uint64_t>(burst_, cap));
        } else if (!paused_ && target_ > 0.0) {
            accumulator_ += dt * target_;
            // Epsilon so 240 frames of 1/60 s at 0.5 gps sum to 2, not 1.9999.
            const double whole = std::floor(accumulator_ + 1e-9);
            if (whole > cap) {
                due = cap;
                capped = true;
                accumulator_ = 0.0;   // do not carry a debt that can never be repaid
            } else {
                due = static_cast<uint32_t>(whole);
                accumulator_ -= whole;
            }
        }

        const double t0 = now();
        uint32_t done = 0;
        while (done < due) {
            step();
            ++done;
            if (budget_ > 0.0 && done < due && now() - t0 >= budget_) break;
        }
        const bool budgetCut = done < due;
        if (budgetCut && burst_ == 0) accumulator_ = 0.0;
        if (burst_ > 0) burst_ -= done;

        stats_.steps_last_frame = done;
        stats_.below_target = capped || budgetCut;
        stats_.effective_cap = cap;
        if (dt > 0.0) {
            const double inst = done / dt;
            stats_.achieved_gps = stats_.achieved_gps == 0.0 ? inst : stats_.achieved_gps * 0.9 + inst * 0.1;
        }
        return done;
    }

    uint32_t update(double dt, const std::function<void()>& step) {
        return update(dt, step, [] {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        });
    }

private:
    double   target_      = 60.0;
    uint32_t maxSteps_    = 64;
    double   budget_      = 1.0 / 120.0;   // half a 60 Hz frame
    double   slowFrame_   = 1.0 / 30.0;    // a frame longer than this is too long
    uint32_t effectiveCap_ = 0;            // 0 = not yet initialised from maxSteps_
    bool     paused_      = false;
    bool     singleStep_  = false;
    uint64_t burst_       = 0;
    double   accumulator_ = 0.0;
    Stats    stats_;
};

}  // namespace aether::sim
