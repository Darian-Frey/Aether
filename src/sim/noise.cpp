#include "sim/noise.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace aether::sim {

namespace {

// Every arithmetic step is its own statement and every value is a float. Both
// are deliberate. A compiler is allowed to contract `a*b+c` into an fma, which
// is a different result, and this buffer has to come out the same on every
// machine that replays the session (SPEC §11, AV-015). Written as separate
// operations there is nothing to contract, which is the same discipline the
// expression interpreter in cpu_step keeps and for the same reason.
float smoothstep(float t) {
    const float tt = t * t;
    const float three = 3.0f - 2.0f * t;   // one multiply, one subtract
    return tt * three;
}

float mix(float a, float b, float t) {
    const float d = b - a;
    const float scaled = d * t;
    return a + scaled;
}

// One octave: a lattice of `cells` values per axis, sampled at the cell centres
// of the grid. The lattice wraps, so the noise is seamless on a torus — which is
// what a `wrap` boundary wants, and harmless otherwise.
struct Lattice {
    uint32_t nx = 1, ny = 1, nz = 1;
    std::vector<float> value;

    float at(uint32_t x, uint32_t y, uint32_t z) const {
        return value[(size_t{z % nz} * ny + (y % ny)) * nx + (x % nx)];
    }
};

Lattice drawLattice(uint32_t nx, uint32_t ny, uint32_t nz, Pcg32& streamA) {
    Lattice l;
    l.nx = std::max(1u, nx);
    l.ny = std::max(1u, ny);
    l.nz = std::max(1u, nz);
    l.value.resize(size_t{l.nx} * l.ny * l.nz);
    // One draw per lattice point, in index order. The count depends only on the
    // extents, so the stream advances by a predictable amount.
    for (float& v : l.value) {
        v = static_cast<float>(streamA.next() >> 8) * (1.0f / 16777216.0f);   // [0, 1)
    }
    return l;
}

// Trilinear (or bilinear, or linear) interpolation of one lattice at a point
// given in lattice coordinates. Degenerate axes cost nothing: an axis with one
// lattice cell interpolates between a value and itself.
float sample(const Lattice& l, float fx, float fy, float fz) {
    const auto floorTo = [](float f) { return static_cast<uint32_t>(std::floor(f)); };
    const uint32_t x0 = floorTo(fx), y0 = floorTo(fy), z0 = floorTo(fz);
    const float tx = smoothstep(fx - std::floor(fx));
    const float ty = smoothstep(fy - std::floor(fy));
    const float tz = smoothstep(fz - std::floor(fz));

    const float c000 = l.at(x0, y0, z0),     c100 = l.at(x0 + 1, y0, z0);
    const float c010 = l.at(x0, y0 + 1, z0), c110 = l.at(x0 + 1, y0 + 1, z0);
    const float c001 = l.at(x0, y0, z0 + 1),     c101 = l.at(x0 + 1, y0, z0 + 1);
    const float c011 = l.at(x0, y0 + 1, z0 + 1), c111 = l.at(x0 + 1, y0 + 1, z0 + 1);

    const float x00 = mix(c000, c100, tx), x10 = mix(c010, c110, tx);
    const float x01 = mix(c001, c101, tx), x11 = mix(c011, c111, tx);
    const float y0v = mix(x00, x10, ty),   y1v = mix(x01, x11, ty);
    return mix(y0v, y1v, tz);
}

}  // namespace

void fillNoise(const core::GridSpec& spec, const NoiseParams& params,
               Pcg32& streamA, std::span<float> out) {
    assert(out.size() == spec.cellCount());

    const uint32_t octaves = std::clamp(params.octaves, 1u, 8u);
    const uint32_t base = std::max(1u, params.frequency);
    const uint32_t widest = std::max({spec.width, spec.height, spec.depth});

    // Every lattice is drawn before any is sampled, so the draws happen in a
    // fixed order that does not depend on how the sampling loop is written.
    std::vector<Lattice> lattices;
    lattices.reserve(octaves);
    for (uint32_t o = 0; o < octaves; ++o) {
        const uint32_t cells = base << o;
        // An axis gets lattice cells in proportion to its extent, so a long thin
        // grid is not stretched: patches are the same shape whichever way the
        // grid is. At least one per axis, and never more than the extent, since a
        // lattice finer than the grid is noise nobody can see.
        auto axis = [&](uint32_t extent) {
            if (extent <= 1) return 1u;
            const uint64_t scaled = (uint64_t{cells} * extent + widest - 1) / widest;
            return static_cast<uint32_t>(std::clamp<uint64_t>(scaled, 1u, extent));
        };
        lattices.push_back(drawLattice(axis(spec.width), axis(spec.height), axis(spec.depth), streamA));
    }

    // Normalise by the sum of the amplitudes rather than by the observed range:
    // the observed range depends on the values drawn, and a normalisation that
    // did would make one cell's value depend on every other cell's.
    float total = 0.0f;
    float amplitude = 1.0f;
    for (uint32_t o = 0; o < octaves; ++o) {
        total += amplitude;
        amplitude = amplitude * 0.5f;
    }
    const float inverse = 1.0f / total;
    const float span = params.high - params.low;

    for (uint32_t z = 0; z < spec.depth; ++z) {
        for (uint32_t y = 0; y < spec.height; ++y) {
            for (uint32_t x = 0; x < spec.width; ++x) {
                float sum = 0.0f;
                float a = 1.0f;
                for (const Lattice& l : lattices) {
                    // The cell's position in this lattice's coordinates. Cell
                    // centres rather than corners, so the first and last cells of
                    // an axis are not both exactly on a lattice point.
                    const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(l.nx)
                                     / static_cast<float>(spec.width);
                    const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(l.ny)
                                     / static_cast<float>(spec.height);
                    const float fz = (static_cast<float>(z) + 0.5f) * static_cast<float>(l.nz)
                                     / static_cast<float>(spec.depth);
                    const float v = sample(l, fx, fy, fz);
                    const float weighted = v * a;
                    sum += weighted;
                    a = a * 0.5f;
                }
                const float unit = sum * inverse;
                const float scaled = unit * span;
                out[(size_t{z} * spec.height + y) * spec.width + x] = params.low + scaled;
            }
        }
    }
}

}  // namespace aether::sim
