// Screensaver mode (F-024).
//
// A fullscreen run with no interface that walks a playlist of bundled rules,
// reseeding each and sometimes letting it drift. This was the project's
// original motivation, and what it mostly needs is restraint: the engine
// already runs rules, mutates them and reseeds grids, so the only new thing
// is deciding what to show next.
//
// That decision is here and is deterministic. A playlist built from the same
// seed produces the same sequence of rules, grid seeds and mutation settings,
// so a run that showed something worth keeping can be found again — which is
// what F-024's fourth acceptance point asks for, and the reason nothing here
// reaches for the clock or `rand()`. It draws from its own PCG32 rather than
// stream A: the run's own randomness must stay exactly what the session says
// it is (SPEC §10, AV-006), so the playlist's draws are kept clear of it.

#pragma once

#include "sim/rng.hpp"

#include <cstdint>
#include <cstddef>

namespace aether::ui {

struct PlaylistEntry {
    size_t   rule          = 0;     // index into the library the playlist was built from
    uint64_t seedA         = 0;     // the grid's fill and rule mutation
    uint64_t seedB         = 0;     // cell mutation
    bool     ruleMutation  = false;
    uint32_t ruleInterval  = 250;
    uint32_t ruleMagnitude = 1;
    double   cellMutationP = 0.0;   // 0 = off

    bool operator==(const PlaylistEntry&) const = default;
};

struct PlaylistOptions {
    double   seconds        = 45.0;   // how long each entry runs
    double   driftChance    = 0.45;   // an entry mutates its rule as it runs
    double   noiseChance    = 0.30;   // an entry has cell mutation on
};

class Playlist {
public:
    // `ruleCount` is how many rules there are to choose between.
    Playlist(size_t ruleCount, uint64_t seed, PlaylistOptions opts = {});

    // The next thing to show. Never the same rule twice running, where there
    // is more than one to choose from: a screensaver that repeats itself
    // immediately looks broken rather than random.
    PlaylistEntry next();

    size_t ruleCount() const { return ruleCount_; }
    const PlaylistOptions& options() const { return opts_; }

private:
    size_t          ruleCount_;
    PlaylistOptions opts_;
    sim::Pcg32      rng_;
    size_t          previous_ = SIZE_MAX;
};

}  // namespace aether::ui
