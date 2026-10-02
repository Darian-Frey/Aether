// The GPU half of the readouts (F-036).
//
// Owns one compiled reduction program per shape and the two buffers it needs,
// and hands back a `GridStats` whose numbers are the ones `sim::reduce` would
// have produced on the host. The contract those two share is in `sim/stats.hpp`
// and is about summation *order*, which is the only part of this that is
// difficult.
//
// What comes back from the GPU is the per-tile partials — a few words per tile,
// never the grid (AV-002). The host then adds the tiles up in tile order, which
// is cheap, bounded by the tile count rather than the cell count, and the one
// place the order is fixed by something other than a shader's scheduling.
//
// GL handles and plain state are split the way `GpuStepper` splits them, so a
// member added to the second cannot be dropped by the move constructor (BUG-007).

#pragma once

#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "sim/gpu_step.hpp"
#include "sim/stats.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace aether::sim {

class GpuReducer {
public:
    GpuReducer() = default;
    ~GpuReducer();
    GpuReducer(GpuReducer&&) noexcept;
    GpuReducer& operator=(GpuReducer&&) noexcept;
    GpuReducer(const GpuReducer&) = delete;
    GpuReducer& operator=(const GpuReducer&) = delete;

    // Compiles for this rule and grid if not already. Leaves the previous
    // program in place on failure, as GpuStepper::setRule does (AV-014).
    std::optional<core::Error> setRule(const rule::CompiledRule& rule, const core::GridSpec& spec);

    bool ready() const { return cfg_.program != 0; }

    // Reduces `stateTexture` and the field textures, and returns the totals.
    // Synchronises — it reads a buffer back — which is why nothing calls this
    // every generation; the sampling interval is the caller's business and is
    // what the session records (SPEC §11).
    // `previousTexture` is the generation before `stateTexture`, for the change
    // count; pass 0 when there is none and the count comes back absent.
    GridStats sample(unsigned int stateTexture, std::span<const unsigned int> fieldTextures,
                     unsigned int previousTexture = 0);

private:
    using ShapeKey = std::tuple<uint8_t, uint16_t, uint32_t, uint32_t, uint64_t>;

    std::optional<core::Error> compileVariant(const ShapeKey& key, const rule::CompiledRule& rule);

    struct Owned {
        std::map<ShapeKey, unsigned int> programs;
        unsigned int paramsSsbo = 0, partialsSsbo = 0;
    };
    struct Config {
        unsigned int program = 0;
        unsigned int target = 0;
        uint32_t width = 0, height = 0, depth = 0;
        uint32_t tiles = 0;
        uint16_t states = 0;
        uint32_t fields = 0;
        uint32_t buckets = 0;
        std::vector<unsigned int> fieldFormats;
    };
    Owned  owned_;
    Config cfg_;
    // Scratch for the readback, a member so that sampling allocates nothing
    // after the first time (invariant 8 is about the step loop, but there is no
    // reason to be careless here either).
    std::vector<uint32_t> readback_;
};

}  // namespace aether::sim
