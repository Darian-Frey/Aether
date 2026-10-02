#include "sim/genome.hpp"

namespace aether::sim {

namespace {

// The mask of live bits. `bits` is validated to 1..32, so a shift of 32 — which
// is undefined in both languages — cannot arise from a valid rule; the branch is
// here because this function is also reachable from a test with a raw number.
uint32_t liveMask(uint32_t bits) {
    return bits >= 32u ? 0xFFFFFFFFu : ((1u << bits) - 1u);
}

}  // namespace

uint32_t inherit(uint32_t own, std::span<const uint32_t> parents, uint32_t bits,
                 uint32_t x, uint32_t y, uint32_t z, uint64_t generation,
                 const GenomeParams& params) {
    if (parents.empty()) return own;

    const uint32_t mask = liveMask(bits);
    const uint32_t n = static_cast<uint32_t>(parents.size());
    uint32_t child = 0;

    switch (params.scheme) {
        case Inheritance::Majority: {
            // Counted per bit rather than per parent, so the arithmetic is the
            // same shape the shader will want: a loop over bits with a loop over
            // parents inside it, both statically bounded.
            for (uint32_t b = 0; b < bits; ++b) {
                uint32_t set = 0;
                for (uint32_t p = 0; p < n; ++p) set += (parents[p] >> b) & 1u;
                // Strictly more than half. A tie leaves the bit clear.
                if (2u * set > n) child |= 1u << b;
            }
            break;
        }
        case Inheritance::RandomParent: {
            const uint32_t h = hashSalted(x, y, z, generation, params.seedB, kSaltParentA);
            child = parents[uniformState(h, n)] & mask;
            break;
        }
        case Inheritance::Crossover: {
            const uint32_t ha = hashSalted(x, y, z, generation, params.seedB, kSaltParentA);
            const uint32_t hb = hashSalted(x, y, z, generation, params.seedB, kSaltParentB);
            const uint32_t a = parents[uniformState(ha, n)];
            const uint32_t b = parents[uniformState(hb, n)];
            for (uint32_t i = 0; i < bits; ++i) {
                // A fresh draw per bit, salted apart from the mutation draws
                // below: sharing a salt would make a bit's parent and its
                // mutation the same coin.
                const uint32_t h = hashSalted(x, y, z, generation, params.seedB, kSaltCrossover + i);
                const uint32_t from = (h & 0x80000000u) != 0u ? a : b;
                child |= ((from >> i) & 1u) << i;
            }
            break;
        }
    }

    child &= mask;
    if (params.threshold == 0) return child;

    // Per-bit mutation. Grouped on the *coordinate* when blockShift says so, so
    // a clan is mutated the same way at once — but the parent draws above are
    // never grouped, because a block sharing one parent pick would make a
    // clan's births identical rather than merely correlated.
    const uint32_t bx = x >> params.blockShift;
    const uint32_t by = y >> params.blockShift;
    const uint32_t bz = z >> params.blockShift;
    for (uint32_t b = 0; b < bits; ++b) {
        const uint32_t h = hashSalted(bx, by, bz, generation, params.seedB, kSaltMutate + b);
        if (h < params.threshold) child ^= 1u << b;
    }
    return child & mask;
}

}  // namespace aether::sim
