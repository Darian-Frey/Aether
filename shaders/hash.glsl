// Stream B (SPEC §10): the stateless hash for cell mutation. This file is
// prepended to every shader that mutates cells and is the exact twin of
// sim/hash.hpp; the agreement test compares the two over a large sweep.

uint aetherMix32(uint v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

uint aetherHash32(uint x, uint y, uint z, uint genLo, uint genHi, uint seedLo, uint seedHi) {
    uint h = seedLo ^ 0x9e3779b9u;
    h = aetherMix32(h ^ x);
    h = aetherMix32(h ^ y);
    h = aetherMix32(h ^ z);
    h = aetherMix32(h ^ genLo);
    h = aetherMix32(h ^ genHi);
    h = aetherMix32(h ^ seedHi);
    return h;
}

// State in 0..states-1 from a hash by multiply-shift (no modulo bias).
// Twin of sim::hashSalted. A further draw from the same cell and generation,
// distinguished by `salt`, with the salt mixed before it is folded in so that
// adjacent salts do not give adjacent results (F-033).
uint aetherHashSalted(uint x, uint y, uint z, uint genLo, uint genHi, uint seedLo, uint seedHi, uint salt) {
    return aetherMix32(aetherHash32(x, y, z, genLo, genHi, seedLo, seedHi) ^ aetherMix32(salt + 0x9e3779b9u));
}

uint aetherUniformState(uint h, uint states) {
    uint hi, lo;
    umulExtended(h, states, hi, lo);
    return hi;
}

// The decision for one cell, as in sim/hash.hpp.
uint aetherBlockHash(uint x, uint y, uint z, uint genLo, uint genHi, uint seedLo, uint seedHi, uint shift) {
    return aetherHash32(x >> shift, y >> shift, z >> shift, genLo, genHi, seedLo, seedHi);
}
bool aetherMutates(uint h, uint threshold) { return h < threshold; }
uint aetherMutatedState(uint h, uint states) { return aetherUniformState(aetherMix32(h ^ 0xa5a5a5a5u), states); }

// The continuous counterpart: a value in [0, 1). Twin of sim::mutatedValue,
// including the second mixing, which is there for the BUG-005 reason.
float aetherMutatedValue(uint h) { return float(aetherMix32(h ^ 0xa5a5a5a5u)) * 2.3283064365386963e-10; }
