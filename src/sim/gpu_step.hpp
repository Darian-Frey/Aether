// GPU stepper for the lookup-table backend (F-003).
//
// Holds one compiled program per rule shape (dimensionality, N, S, kind,
// boundary) and the SSBOs for the current rule. Changing the rule while the
// shape is unchanged — which is what rule mutation does — is a buffer upload
// with no shader compile (D-004). Requires a GL 4.3 context.

#pragma once

#include "core/gpu_grid.hpp"
#include "rule/lut.hpp"
#include "sim/hash.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

namespace aether::sim {

class GpuStepper {
public:
    GpuStepper() = default;
    ~GpuStepper();
    GpuStepper(GpuStepper&&) noexcept;
    GpuStepper& operator=(GpuStepper&&) noexcept;
    GpuStepper(const GpuStepper&) = delete;
    GpuStepper& operator=(const GpuStepper&) = delete;

    // Compiles the shape variant if not cached and uploads the rule's
    // buffers. On failure nothing is changed and the previous rule, if any,
    // stays active (AV-014).
    std::optional<core::Error> setRule(const rule::LutRule& rule, const core::GridSpec& spec);

    bool hasRule() const { return program_ != 0; }

    // One generation: reads `src`, writes `dst`, then a memory barrier so
    // the result is visible to the next dispatch, to samplers and to
    // download. The two must be distinct textures. Does not swap.
    void step(unsigned int srcTexture, unsigned int dstTexture);

    // One generation on a GpuGrid, then swap.
    void step(core::GpuGrid& grid);

    uint64_t generation() const { return generation_; }
    void setGeneration(uint64_t g) { generation_ = g; }

    // Cell mutation parameters, applied from the next step on.
    void setCellMutation(CellMutation m) { mutation_ = m; }
    CellMutation cellMutation() const { return mutation_; }

    size_t cachedPrograms() const { return programs_.size(); }

private:
    using ShapeKey = std::tuple<uint8_t, uint32_t, uint16_t, rule::Kind, rule::Boundary>;

    std::optional<core::Error> compileVariant(const ShapeKey& key);
    void releaseBuffers();

    std::map<ShapeKey, unsigned int> programs_;
    unsigned int program_ = 0;
    int locGenLo_ = -1, locGenHi_ = -1, locThreshold_ = -1, locSeedLo_ = -1, locSeedHi_ = -1;
    unsigned int paramsSsbo_ = 0, offsetsSsbo_ = 0, compsSsbo_ = 0, tableSsbo_ = 0;
    unsigned int target_ = 0;
    uint32_t     groupsX_ = 0, groupsY_ = 0, groupsZ_ = 0;
    uint32_t     width_ = 0, height_ = 0, depth_ = 0;
    uint64_t     generation_ = 0;
    CellMutation mutation_;
};

}  // namespace aether::sim
