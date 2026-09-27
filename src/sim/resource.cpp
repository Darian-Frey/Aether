#include "sim/resource.hpp"

#include <cmath>
#include <limits>

namespace aether::sim {

const char* resourceProblem(const ResourceParams& params, rule::Boundary boundary) {
    if (params.diffusion != 0.0f && boundary == rule::Boundary::Mirror) {
        return "diffusion cannot run against a mirror boundary: an edge cell counts its inward "
               "neighbour twice while that neighbour counts it once, so the exchange creates "
               "material rather than moving it (AV-018)";
    }
    return nullptr;
}

float applyResource(float drawnDown, float before, float capacity,
                    std::span<const float> neighbours,
                    const ResourceParams& params,
                    SiteLedger* ledger) {
    SiteLedger book;

    // Step 1 is already done: `drawnDown` is what the rule's expression produced.
    // Signed, so a rule whose write raised the resource reads as a second source
    // rather than being absorbed into a total that still balances.
    book.consumed = before - drawnDown;
    float value = drawnDown;

    // Step 2: toward the capacity, plus the trickle. Written as separate
    // statements in float throughout, so nothing can be contracted into an fma
    // and the shader's twin can match it bit for bit (AV-015).
    const float headroom = capacity - value;
    const float toward = params.regen * headroom;
    const float trickle = params.minSeed * capacity;
    float added = toward + trickle;
    // Regeneration never removes material. Above capacity the headroom is
    // negative and the clamp below is what brings the value down, not this.
    if (added < 0.0f) added = 0.0f;
    book.regenerated = added;
    value = value + added;

    // Step 3: diffusion. Skipped rather than multiplied by zero, so a run with it
    // off costs nothing and cannot drift by a rounding error either.
    if (params.diffusion != 0.0f && !neighbours.empty()) {
        float sum = 0.0f;
        for (float n : neighbours) sum += n;
        const float count = static_cast<float>(neighbours.size());
        // The reciprocal on the host and a multiply, never a divide: GLSL permits
        // float division 2.5 ULP of error where C++ is correctly rounded, so a
        // divide is a guaranteed disagreement between the two paths (AV-015).
        const float inverse = 1.0f / count;
        const float mean = sum * inverse;
        // Against `before`, not `value`. Both sides of the gradient must come from
        // the same generation: the neighbours' values are this generation's, so
        // this site's must be too. Measuring the gradient against the
        // post-consumption value mixes two snapshots, and because consumption only
        // ever lowers it, the gradient is biased upward at every site that ate —
        // so diffusion *creates* material. That was not a rounding error: 98 units
        // conjured over a thousand generations on a 768-cell grid, caught by the
        // balance test rather than by reading the code (AV-018).
        const float gradient = mean - before;
        const float flow = params.diffusion * gradient;
        book.diffused = flow;
        value = value + flow;
    }

    // Step 4: the clamp. A resource above its carrying capacity is the capacity
    // meaning nothing, and one below zero is a debt this engine has no notion of.
    // Subnormals go to zero here as everywhere a float is produced, or a decaying
    // resource parts company with the shader (BUG-021, SPEC §6).
    const float unclamped = value;
    if (value < 0.0f) value = 0.0f;
    if (value > capacity) value = capacity;
    if (std::fabs(value) < std::numeric_limits<float>::min()) value = 0.0f;
    book.clamped = value - unclamped;

    if (ledger != nullptr) *ledger = book;
    return value;
}

}  // namespace aether::sim
