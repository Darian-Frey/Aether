#include "sim/fill.hpp"

#include "rule/neighbourhood.hpp"
#include "rule/table_layout.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace aether::sim {

namespace {

// The mean of the live-neighbour counts at which a cell is alive next
// generation — born from nothing or surviving — read out of the rule's own
// table (BUG-023).
//
// Two shapes of rule can be read this way. A two-state table rule, where the
// one live state is 1; and a rule with a hard **lifespan**, where the live
// states are the ages 1..L and `metadata.lifespan` says so (BUG-026). In both
// the question is the same — at which neighbour counts does a cell end up alive
// — and the only difference is which next states count as alive.
//
// Everything else keeps the conventional 30%, and the restriction is the point
// rather than laziness. An expression rule has no table to look in; a cyclic or
// Generations rule means something different by each of its states, so there is
// no band to aim at; and a counted rule counting something other than the live
// states is answering a different question. A known default beats a derivation
// that is really a guess.
std::optional<double> meanLiveBand(const rule::RuleIR& ir) {
    if (ir.cell_type != core::CellType::U8) return std::nullopt;
    if (ir.kind != rule::Kind::OuterTotalistic && ir.kind != rule::Kind::CountedTotalistic) {
        return std::nullopt;
    }
    // The last state that counts as alive. Ordinarily state 1 and nothing else;
    // with a lifespan, every age up to L. A tail beyond that is a dying cell and
    // is not alive, which is why this is the *last live* state and not `states`.
    const uint16_t lastLive = ir.metadata.lifespan.value_or(1);
    if (lastLive >= ir.states) return std::nullopt;
    if (!ir.metadata.lifespan && ir.states != 2) return std::nullopt;
    if (ir.metadata.lifespan && ir.kind != rule::Kind::CountedTotalistic) return std::nullopt;
    const auto* table = std::get_if<rule::Table>(&ir.transition);
    if (!table) return std::nullopt;
    // A counted rule's `k` is the number of neighbours in the set that own
    // state counts. Unless that set is exactly the live state, `k` is not a
    // live-neighbour count and the band would be read off the wrong axis.
    if (ir.kind == rule::Kind::CountedTotalistic) {
        if (ir.counted.size() < 2) return std::nullopt;
        for (uint16_t own = 0; own < 2; ++own) {
            if (!ir.counted[own].test(1) || ir.counted[own].test(0)) return std::nullopt;
        }
    }

    const uint32_t N = rule::neighbourCount(ir.dimensions, ir.neighbourhood);
    if (N == 0) return std::nullopt;
    const rule::TableLayout layout(ir.kind, ir.states, N);

    double sum   = 0.0;
    uint32_t hits = 0;
    for (uint32_t k = 0; k <= N; ++k) {
        bool alive = false;
        // Own state 0 answers "is a cell born here", own state 1 "does one
        // survive". Every later age gives the same answer as age 1 — a lifespan
        // rule's table is the base rule's answer with a deadline on top — so two
        // probes cover the band whatever L is.
        for (uint8_t own = 0; own < 2 && !alive; ++own) {
            // For two states the outer form's count vector is one digit — the
            // number of live neighbours — so both kinds index the same shape.
            const uint32_t counts[1] = {k};
            const uint64_t idx = ir.kind == rule::Kind::CountedTotalistic
                                     ? layout.indexCounted(own, k)
                                     : layout.indexOuterTotalistic(own, counts);
            if (idx >= table->entries.size()) return std::nullopt;
            const uint8_t next = table->entries[idx];
            alive = next >= 1 && next <= lastLive;
        }
        if (alive) { sum += k; ++hits; }
    }
    // A rule nothing is ever alive under has no band to aim at.
    if (hits == 0) return std::nullopt;
    return sum / hits;
}

}  // namespace

std::vector<double> defaultDensity(const rule::RuleIR& ir) {
    // A continuous rule has no states to share out: the one weight is the
    // fraction of cells seeded at all (SPEC §1).
    if (ir.cell_type == core::CellType::F32) return {0.3};

    // Tail states take no share: the weights are indexed by state - 1, so
    // entries from decay_from onward stay zero.
    const uint16_t live = ir.metadata.decay_from.value_or(ir.states);
    std::vector<double> out(ir.states > 1 ? ir.states - 1u : 0u, 0.0);
    if (live < 2 || out.empty()) return out;

    // A lifespan rule's live states are one cell's ages, so seeding them evenly
    // is not a fair spread over several kinds of cell — it is most of the grid
    // alive at once, and nothing Life-like survives that (BUG-026). Every cell
    // starts newly born, at the density the rule's own band asks for, which is
    // the same question the two-state branch below answers.
    //
    // Before the `live == 2` test deliberately: a lifespan rule has more than
    // two live states and would otherwise fall to the even spread.
    if (ir.metadata.lifespan) {
        const uint32_t N = rule::neighbourCount(ir.dimensions, ir.neighbourhood);
        const auto band = meanLiveBand(ir);
        out[0] = band && N > 0 ? std::clamp(*band / N, 0.02, 0.5) : 0.3;
        return out;   // every other age stays at zero
    }

    if (live == 2) {
        // Seed so that the expected number of live neighbours lands in the
        // middle of the band the rule is alive in (BUG-023). The conventional
        // 30% is not an arbitrary number — it is 2.4 expected neighbours in 2D
        // Moore, which sits in Life's survival band of 2 to 3 — but it is the
        // right number for *that* neighbourhood only. The same 0.3 in 3D Moore
        // is 7.8 expected neighbours against a rule that survives on 4 or 5, so
        // almost every cell is over-crowded on the first generation.
        //
        // Deriving it keeps 2D Life at 2.5/8 = 0.3125, within a hair of the
        // 0.3 it had, and gives 3D 4555 about 0.17 without anybody choosing it.
        const uint32_t N = rule::neighbourCount(ir.dimensions, ir.neighbourhood);
        const auto band = meanLiveBand(ir);
        out[0] = band && N > 0 ? std::clamp(*band / N, 0.02, 0.5) : 0.3;
        return out;
    }
    const double share = 1.0 / live;   // state 0 keeps the same share
    for (uint16_t s = 1; s < live; ++s) out[s - 1u] = share;
    return out;
}

void fillRandomRegion(core::HostGrid& grid, uint32_t x, uint32_t y, uint32_t z,
                      uint32_t w, uint32_t h, uint32_t d,
                      std::span<const double> density, Pcg32& streamA) {
    const auto& spec = grid.spec();
    if (x >= spec.width || y >= spec.height || z >= spec.depth) return;
    w = std::min(w, spec.width - x);
    h = std::min(h, spec.height - y);
    d = std::min(d, spec.depth - z);

    if (spec.cell_type == core::CellType::F32) {
        // The first weight is the fraction of cells seeded; a seeded cell
        // takes a uniform value. Two draws a cell either way, so the stream
        // advances by the same amount whatever the density.
        const double p = density.empty() ? 0.0 : std::clamp(density[0], 0.0, 1.0);
        auto cells = grid.currentFloats();
        for (uint32_t cz = z; cz < z + d; ++cz) {
            for (uint32_t cy = y; cy < y + h; ++cy) {
                for (uint32_t cx = x; cx < x + w; ++cx) {
                    const double u = streamA.unit();
                    const double v = streamA.unit();
                    cells[grid.index(cx, cy, cz)] = u < p ? static_cast<float>(v) : 0.0f;
                }
            }
        }
        return;
    }

    // Cumulative thresholds so one draw decides the state.
    std::vector<double> cumulative(density.size());
    double acc = 0.0;
    for (size_t i = 0; i < density.size(); ++i) {
        acc += density[i] < 0.0 ? 0.0 : density[i];
        cumulative[i] = acc;
    }
    auto cells = grid.current();
    for (uint32_t cz = z; cz < z + d; ++cz) {
        for (uint32_t cy = y; cy < y + h; ++cy) {
            for (uint32_t cx = x; cx < x + w; ++cx) {
                const double u = streamA.unit();
                uint8_t state = 0;
                for (size_t i = 0; i < cumulative.size(); ++i) {
                    if (u < cumulative[i]) { state = static_cast<uint8_t>(i + 1); break; }
                }
                cells[grid.index(cx, cy, cz)] = state;
            }
        }
    }
}

void fillRandom(core::HostGrid& grid, std::span<const double> density, Pcg32& streamA) {
    // The whole grid is the box that covers it, and walking it x-fastest is
    // the order this always walked, so the stream is consumed identically.
    const auto& s = grid.spec();
    fillRandomRegion(grid, 0, 0, 0, s.width, s.height, s.depth, density, streamA);
}

}  // namespace aether::sim
