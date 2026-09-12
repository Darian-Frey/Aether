// Random number streams (SPEC §10).
//
// Stream A: sequential PCG32, CPU only, for rule mutation and random fill.
// Stream B (cell mutation, stateless hash) arrives in Phase 2 alongside its
// GLSL twin. Nothing in sim/ or shaders/ may draw randomness from anywhere
// else (AV-006).

#pragma once

#include <cstdint>

namespace aether::sim {

// PCG32 (XSH RR, 64/32) exactly as in the reference implementation, so a
// seed produces the same sequence on every platform.
class Pcg32 {
public:
    explicit Pcg32(uint64_t seed, uint64_t sequence = 0) {
        state_ = 0;
        inc_ = (sequence << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // Uniform in [0, bound) without modulo bias (Lemire's method, using the
    // rejection loop so the result does not depend on 64-bit multiply width).
    uint32_t below(uint32_t bound) {
        const uint32_t threshold = (-bound) % bound;
        for (;;) {
            const uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform in [0, 1).
    double unit() { return next() * (1.0 / 4294967296.0); }

    // Full generator state, so a session can resume exactly where it was.
    struct State { uint64_t state; uint64_t inc; };
    State state() const { return {state_, inc_}; }
    static Pcg32 fromState(State s) { Pcg32 r; r.state_ = s.state; r.inc_ = s.inc; return r; }

private:
    Pcg32() = default;
    uint64_t state_ = 0;
    uint64_t inc_ = 1;
};

}  // namespace aether::sim
