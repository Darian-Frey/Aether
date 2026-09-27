// The resource's dynamics (F-032, D-024).
//
// One declared field is the resource and another is its per-site carrying
// capacity. The rule's write on that field is the draw-down and nothing more;
// what follows is the engine's, and it is applied in this order:
//
//   1. the draw-down, which the rule's own expression has already produced
//   2. regeneration toward the capacity, plus the minimum seed trickle
//   3. diffusion with the neighbours' resource, when enabled
//   4. the clamp to [0, capacity]
//
// Diffusion's gradient is measured between *this generation's* values on both
// sides — the neighbours as they are and this site as it was, before the
// draw-down. That is not a detail. Measuring it against the post-consumption value
// mixes two snapshots, and since consumption only ever lowers it the gradient is
// biased upward wherever anything ate, so diffusion silently creates material.
// The order above is the order the terms are *applied* in; the order they are
// *read* in is one generation, everywhere.
//
// The order is not an implementation detail. AV-018 names "a regeneration step
// that runs before consumption instead of after" as one of its leaks, and a leak
// in a quantity under selection is exploited rather than merely wrong: a lineage
// that finds it outbreeds every lineage that does not, so the first symptom is a
// population thriving for no visible reason.
//
// `applyResource` here and the GLSL `sim/gpu_step` generates are twins, like
// `sim/hash.hpp` and `shaders/hash.glsl`. Change both or neither; the resource
// fixture in the equivalence sweep is what says they agree.

#pragma once

#include "rule/ir.hpp"

#include <cstdint>
#include <span>

namespace aether::sim {

// Run-time controls, not rule text. They live beside the cell-mutation
// probability rather than in the IR, because a constant in the rule would make
// the primary control of this feature the one thing that cannot be moved without
// recompiling and appending to the lineage (D-024).
struct ResourceParams {
    // Toward the capacity, per generation. The primary harshness control: 0 is a
    // world that never recovers what is taken, 1 is one that recovers instantly.
    float regen = 0.02f;
    // A trickle proportional to capacity, applied whatever the current level.
    // This is the damping on harshness — it is what lets a patch scoured to
    // nothing come back, so a low regeneration rate makes a poor world rather
    // than a permanently dead one.
    float minSeed = 0.0f;
    // Exchange with the neighbours' resource, per generation. Zero disables it,
    // and the arithmetic is skipped rather than multiplied by zero.
    float diffusion = 0.0f;

    bool operator==(const ResourceParams&) const = default;
};

// Whether these parameters can be honoured under this boundary. Diffusion
// conserves under `wrap`, leaks at a `zero` edge — a cell at the edge diffuses
// into nothing, which is a sink and is counted as one — and under `mirror` it can
// *create*, because the neighbour relation is not symmetric there: an edge cell
// counts its inward neighbour twice while that neighbour counts it once. Measured
// rather than argued — 1D, five cells, D = 0.5, a single unit at cell 1 diffuses
// to a total of 1.25 — and a source that is not regeneration is exactly what
// AV-018 forbids, so it is refused rather than counted.
//
// Returns a message, or nothing when the combination is sound.
const char* resourceProblem(const ResourceParams& params, rule::Boundary boundary);

// What one site's resource did, term by term. The identity is exact by
// construction:
//
//     after - before == regenerated + diffused + clamped - consumed
//
// Four terms rather than two because AV-018 asks for more than a total that
// balances: "a sudden divergence between the counted total and the measured one
// localises the leak to the step that opened it", and a single net figure cannot
// say which step opened it. `consumed` and `diffused` are signed — a rule whose
// write *raised* the resource is a second source and shows up as a negative
// consumption rather than being quietly absorbed, which is the whole point.
struct SiteLedger {
    float consumed    = 0.0f;   // before -> drawn down; negative means the rule added
    float regenerated = 0.0f;   // the regeneration term and the trickle, never negative
    float diffused    = 0.0f;   // exchange with the neighbours; signed
    float clamped     = 0.0f;   // what the clamp and the subnormal flush adjusted; signed
};

// One site's resource, after the rule has had it. `drawnDown` is what the rule's
// write expression produced, `before` what it read, `capacity` this site's
// ceiling, and `neighbours` the resource at each neighbour in canonical order with
// a zero boundary's absent cells already zero — the same values the rule read.
//
// `ledger` may be null, and is on both execution paths: it is a diagnostic for the
// balance test of step 4 and for F-036's readout, and nothing reads it back into
// the simulation.
float applyResource(float drawnDown, float before, float capacity,
                    std::span<const float> neighbours,
                    const ResourceParams& params,
                    SiteLedger* ledger = nullptr);

}  // namespace aether::sim
