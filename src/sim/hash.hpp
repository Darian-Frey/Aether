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
    uint32_t threshold = 0;   // from mutationThreshold(p)
    uint64_t seedB     = 0;
};

// The decision for one cell. `h2` is a second mixing of the hash so that
// the state does not correlate with the test (BUG-005).
constexpr bool mutates(uint32_t h, const CellMutation& m) {
    return h < m.threshold;
}
constexpr uint32_t mutatedState(uint32_t h, uint32_t states) {
    return uniformState(mix32(h ^ 0xa5a5a5a5u), states);
}

}  // namespace aether::sim
