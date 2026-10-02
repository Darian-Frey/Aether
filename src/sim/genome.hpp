// Inheritance at birth (F-033, D-025).
//
// A genome is a `u32` field the engine carries and does not read. When a cell is
// newly alive, the engine derives its bits from the genomes of its live
// neighbours and then mutates them per bit; it never learns that bit 3 means
// "born on three neighbours". That division is what lets later features put more
// genes in the same field without the engine learning anything.
//
// This is the third mutation control. F-015 searches rule space over time and
// F-016 flips cells over space; both are *applied*. This one is inherited, and
// therefore selected — the first variation in this engine that competes.
//
// Every draw comes from stream B, hashed on the cell's coordinate and the
// generation, so a run replays bit-identically without storing anything. The
// draws are salted apart: one for each parent pick and one per bit, because a
// single hash reused would correlate the parent with the mutations and the
// mutations with each other.
//
// `inherit` here and the GLSL `sim/gpu_step` generates are twins, like the hash
// pair and the resource's arithmetic. Change both or neither.

#pragma once

#include "sim/hash.hpp"

#include <cstdint>
#include <span>

namespace aether::sim {

// How a child's bits come from its parents.
enum class Inheritance : uint8_t {
    // Per bit, the value held by more than half the parents. A tie leaves the
    // bit clear, which is arbitrary but has to be *some* fixed thing or the two
    // paths would be free to differ.
    Majority = 0,
    // One parent, drawn uniformly, copied whole.
    RandomParent = 1,
    // Per bit, from one of two parents drawn uniformly — uniform crossover
    // rather than single-point, because it needs no extra notion of gene order
    // and the IR has none to offer.
    Crossover = 2,
};

// Run-time controls, not rule text — the same reasoning D-024 gives for the
// resource's rates. They are journalled and travel in the session.
struct GenomeParams {
    Inheritance scheme = Inheritance::Majority;
    // Per-bit mutation probability, as a stream B threshold.
    uint32_t    threshold = 0;
    uint64_t    seedB = 0;
    // Grouping, as F-026 gives cell mutation: 0 is one cell per block, k groups
    // 2^k per axis, so a whole clan is mutated the same way at once. The
    // *parent* draws are never grouped — a block sharing one parent pick would
    // make a clan's births identical rather than merely correlated.
    uint8_t     blockShift = 0;

    bool operator==(const GenomeParams&) const = default;
};

// The salts. Named rather than written as numbers at the call sites, because two
// draws that accidentally shared one would be correlated in a way no test of
// either alone would notice.
constexpr uint32_t kSaltParentA   = 0x1000u;
constexpr uint32_t kSaltParentB   = 0x2000u;
constexpr uint32_t kSaltCrossover = 0x3000u;   // + bit index
constexpr uint32_t kSaltMutate    = 0x4000u;   // + bit index

// What a newly born cell's genome becomes. `parents` are the genomes of its live
// neighbours, in the canonical neighbourhood order with the dead ones left out —
// both paths gather in that order, so both see the same list.
//
// With no parents there is nothing to inherit: `own` comes back unchanged, which
// is what happens to a cell that mutated into existence away from anything
// living. `bits` is how many low bits of the field the rule uses, so that
// mutation does not scatter noise through bits nothing reads and the genome hash
// stays a hash of the genome.
uint32_t inherit(uint32_t own, std::span<const uint32_t> parents, uint32_t bits,
                 uint32_t x, uint32_t y, uint32_t z, uint64_t generation,
                 const GenomeParams& params);

}  // namespace aether::sim
