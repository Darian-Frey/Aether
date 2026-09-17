// Stream B (SPEC §10): the stateless hash for cell mutation.
//
// Exact twin of shaders/hash.glsl. All arithmetic is 32-bit unsigned with
// wraparound, which is what GLSL guarantees too. Never change one file
// without the other; tests/sim/hash_test.cpp compares them.

#pragma once

#include <cstdint>

namespace aether::sim {

constexpr uint32_t mix32(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

constexpr uint32_t hash32(uint32_t x, uint32_t y, uint32_t z, uint64_t generation, uint64_t seedB) {
    const uint32_t genLo  = static_cast<uint32_t>(generation);
    const uint32_t genHi  = static_cast<uint32_t>(generation >> 32);
    const uint32_t seedLo = static_cast<uint32_t>(seedB);
    const uint32_t seedHi = static_cast<uint32_t>(seedB >> 32);
    uint32_t h = seedLo ^ 0x9e3779b9u;
    h = mix32(h ^ x);
    h = mix32(h ^ y);
    h = mix32(h ^ z);
    h = mix32(h ^ genLo);
    h = mix32(h ^ genHi);
    h = mix32(h ^ seedHi);
    return h;
}

// State in 0..states-1 by multiply-shift.
constexpr uint32_t uniformState(uint32_t h, uint32_t states) {
    return static_cast<uint32_t>((static_cast<uint64_t>(h) * states) >> 32);
}

// Probability p in [0, 1] as the threshold the hash is compared against.
// p = 0 gives 0 (never); p = 1 gives 0xFFFFFFFF (all but one hash value).
constexpr uint32_t mutationThreshold(double p) {
    if (p <= 0.0) return 0;
    if (p >= 1.0) return 0xFFFFFFFFu;
    return static_cast<uint32_t>(p * 4294967296.0);
}

// The step-time parameters of cell mutation (SPEC §9.2). Passed to both
// steppers alongside the rule; not part of the rule.
struct CellMutation {
    uint32_t threshold  = 0;   // from mutationThreshold(p)
    uint64_t seedB      = 0;
    uint8_t  blockShift = 0;   // 0 = one cell per block; k groups 2^k per axis
};

// The decision hash for a cell: the hash of its block. At blockShift 0 this
// is the cell's own hash, so grouping off is bit-for-bit the original
// per-cell behaviour and old sessions replay unchanged.
constexpr uint32_t blockHash(uint32_t x, uint32_t y, uint32_t z, uint64_t generation, const CellMutation& m) {
    return hash32(x >> m.blockShift, y >> m.blockShift, z >> m.blockShift, generation, m.seedB);
}

// The decision for one cell. The replacement state comes from a second
// mixing of the *cell's* hash, both so that it does not correlate with the
// test (BUG-005) and so that a mutating block is a burst of noise rather
// than one flat colour.
constexpr bool mutates(uint32_t h, const CellMutation& m) {
    return h < m.threshold;
}
constexpr uint32_t mutatedState(uint32_t h, uint32_t states) {
    return uniformState(mix32(h ^ 0xa5a5a5a5u), states);
}

// The continuous counterpart: a value in [0, 1). Mixed the same second time
// and with the same constant as mutatedState, for the same reason (BUG-005) —
// a hash that passed the threshold is small by construction, so its own bits
// would cluster near zero. 2^-32 is exact in f32's exponent, so the scaling
// itself introduces no rounding (Phase 5).
inline float mutatedValue(uint32_t h) {
    return static_cast<float>(mix32(h ^ 0xa5a5a5a5u)) * 2.3283064365386963e-10f;
}

}  // namespace aether::sim
