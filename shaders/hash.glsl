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
