#include "sim/stats.hpp"

#include <cstring>

namespace aether::sim {

namespace {

// The palette's hash (render/palette2d.frag's aetherPaletteMix), not sim::mix32
// — same constants, different salt, and presentational in both places. Written
// out rather than included because `sim/` does not depend on `render/`.
uint32_t paletteMix(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

float readFloat(std::span<const uint8_t> bytes, size_t i) {
    float v = 0.0f;
    std::memcpy(&v, bytes.data() + i * sizeof(float), sizeof(float));
    return v;
}

uint32_t readU32(std::span<const uint8_t> bytes, size_t i) {
    uint32_t v = 0;
    std::memcpy(&v, bytes.data() + i * sizeof(uint32_t), sizeof(uint32_t));
    return v;
}

}  // namespace

uint32_t genomeBucket(uint32_t genome, uint32_t bits) {
    const uint32_t mask = bits >= 32u ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    return paletteMix((genome & mask) ^ 0x5bd1e995u) % kGenomeBuckets;
}

GridStats reduce(const rule::CompiledRule& rule, const core::GridSpec& spec,
                 std::span<const uint8_t> cells,
                 std::span<const std::span<const uint8_t>> fields,
                 std::span<const uint8_t> previous) {
    GridStats out;
    out.stateCounts.assign(rule.states ? rule.states : 1u, 0);
    out.fieldTotals.assign(rule.fields.size(), 0.0);
    if (rule.genome) out.genomeBuckets.assign(kGenomeBuckets, 0);
    out.changedKnown = previous.size() == cells.size();

    const uint64_t n = spec.cellCount();
    const uint64_t tiles = (n + kTileCells - 1u) / kTileCells;

    // Tile by tile, and within a tile cell by cell. The brackets are what make
    // this reproducible: a float total summed straight through the grid is a
    // different number from the same total summed in tiles, and the shader can
    // only do the second.
    for (uint64_t t = 0; t < tiles; ++t) {
        const uint64_t begin = t * kTileCells;
        const uint64_t end = begin + kTileCells < n ? begin + kTileCells : n;

        for (size_t f = 0; f < rule.fields.size(); ++f) {
            if (f >= fields.size()) continue;
            // One tile's partial, in f32 exactly as the shader's shared
            // accumulator is, then widened once. Accumulating the partial in
            // double here would make the oracle *more* accurate than the
            // shader, which is the same defect as less (AV-015).
            float partial = 0.0f;
            for (uint64_t i = begin; i < end; ++i) {
                switch (rule.fields[f].cell_type) {
                    case core::CellType::F32: partial += readFloat(fields[f], i); break;
                    case core::CellType::U32: partial += static_cast<float>(readU32(fields[f], i)); break;
                    case core::CellType::U8:  partial += static_cast<float>(fields[f][i]); break;
                }
            }
            out.fieldTotals[f] += static_cast<double>(partial);
        }

        for (uint64_t i = begin; i < end; ++i) {
            const uint8_t s = cells[i];
            if (s < out.stateCounts.size()) ++out.stateCounts[s];
            if (out.changedKnown && previous[i] != s) ++out.changed;
            if (rule.genome && s != 0 && rule.genome->field < fields.size()) {
                const uint32_t g = readU32(fields[rule.genome->field], i);
                ++out.genomeBuckets[genomeBucket(g, rule.genome->bits)];
            }
        }
    }
    return out;
}

}  // namespace aether::sim
