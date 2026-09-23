#include "ui/screensaver.hpp"

#include <algorithm>

namespace aether::ui {

namespace {

// A stream of its own, distinct from the simulation's. The constant keeps a
// playlist seeded with N from walking the same numbers stream A does with the
// same N, which would tie what is shown to what it looks like.
constexpr uint64_t kPlaylistStream = 0x5CAEDEDULL;

}  // namespace

Playlist::Playlist(size_t ruleCount, uint64_t seed, PlaylistOptions opts)
    : ruleCount_(ruleCount), opts_(opts), rng_(seed, kPlaylistStream) {
    opts_.seconds = std::max(1.0, opts_.seconds);
    opts_.driftChance = std::clamp(opts_.driftChance, 0.0, 1.0);
    opts_.noiseChance = std::clamp(opts_.noiseChance, 0.0, 1.0);
}

PlaylistEntry Playlist::next() {
    PlaylistEntry e;
    if (ruleCount_ == 0) return e;

    if (ruleCount_ == 1) {
        e.rule = 0;
    } else {
        // Draw from the other rules rather than redrawing until it differs,
        // so the number of draws does not depend on luck and the sequence
        // stays the same length for a given seed.
        const size_t offset = rng_.below(static_cast<uint32_t>(ruleCount_ - 1));
        e.rule = previous_ == SIZE_MAX ? rng_.below(static_cast<uint32_t>(ruleCount_))
                                       : (previous_ + 1 + offset) % ruleCount_;
    }
    previous_ = e.rule;

    e.seedA = (static_cast<uint64_t>(rng_.next()) << 32) | rng_.next();
    e.seedB = (static_cast<uint64_t>(rng_.next()) << 32) | rng_.next();

    if (rng_.unit() < opts_.driftChance) {
        e.ruleMutation = true;
        // Often enough to see it happen within one entry, rarely enough that
        // the rule is still recognisable while it does.
        e.ruleInterval = 120 + rng_.below(400);
        e.ruleMagnitude = 1 + rng_.below(2);
    }
    if (rng_.unit() < opts_.noiseChance) {
        // A light speckle: enough to keep a stable pattern from settling for
        // good, not enough to drown the rule in noise.
        e.cellMutationP = 1e-5 * (1.0 + rng_.unit() * 20.0);
    }
    return e;
}

}  // namespace aether::ui
