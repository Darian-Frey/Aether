// GPU stepper for the lookup-table backend (F-003).
//
// Holds one compiled program per rule shape (dimensionality, N, S, kind,
// boundary) and the SSBOs for the current rule. Changing the rule while the
// shape is unchanged — which is what rule mutation does — is a buffer upload
// with no shader compile (D-004). Requires a GL 4.3 context.

#pragma once

#include "core/gpu_grid.hpp"
#include "rule/compile.hpp"
#include "sim/hash.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace aether::sim {

// One auxiliary field's texture pair for a step, matching the host side's
// `sim::FieldReads`/`FieldWrites` pair (F-031).
struct FieldTextures {
    unsigned int src = 0;
    unsigned int dst = 0;
};

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
    std::optional<core::Error> setRule(const rule::CompiledRule& rule, const core::GridSpec& spec);

    bool hasRule() const { return cfg_.program != 0; }

    // One generation: reads `src`, writes `dst`, then a memory barrier so
    // the result is visible to the next dispatch, to samplers and to
    // download. The two must be distinct textures. Does not swap.
    // `fields` is the auxiliary field pairs in the rule's declaration order
    // and must be as long as the rule's field list (F-031); a rule that
    // declares none takes the default and reads as it always did.
    void step(unsigned int srcTexture, unsigned int dstTexture,
              std::span<const FieldTextures> fields = {});

    // One generation on a GpuGrid, then swap.
    void step(core::GpuGrid& grid);

    uint64_t generation() const { return cfg_.generation; }
    void setGeneration(uint64_t g) { cfg_.generation = g; }

    // Cell mutation parameters, applied from the next step on.
    void setCellMutation(CellMutation m) { cfg_.mutation = m; }
    CellMutation cellMutation() const { return cfg_.mutation; }

    size_t cachedPrograms() const { return owned_.programs.size(); }

private:
    // The shape a shader is specialised for. A table rule's program depends
    // on nothing but its shape, so changing the table is an upload; a
    // generated rule's program is the rule, so its hash joins the key and
    // each distinct rule gets its own entry (D-004).
    using ShapeKey = std::tuple<uint8_t, uint32_t, uint16_t, rule::Kind, rule::Boundary, uint64_t>;

    std::optional<core::Error> compileVariant(const ShapeKey& key, const rule::CompiledRule& rule);
    void releaseBuffers();

    // GL handles, exchanged on move; everything else is plain data copied
    // wholesale, so a field added to Config can never be forgotten by the
    // move constructor.
    struct Owned {
        std::map<ShapeKey, unsigned int> programs;
        unsigned int paramsSsbo = 0, offsetsSsbo = 0, compsSsbo = 0, tableSsbo = 0;
    };
    struct Config {
        unsigned int program = 0;
        int locGenLo = -1, locGenHi = -1, locThreshold = -1, locSeedLo = -1, locSeedHi = -1, locBlockShift = -1;
        int locSelfWeight = -1;
        bool  continuous = false;   // picks the shader, the image format and the buffer at binding 3
        float selfWeight = 0.0f;
        unsigned int target = 0;
        uint32_t     groupsX = 0, groupsY = 0, groupsZ = 0;
        uint32_t     width = 0, height = 0, depth = 0;
        uint64_t     generation = 0;
        CellMutation mutation;
        // The internal format of each declared field, in declaration order,
        // so that step() binds R8UI or R32F without consulting the rule again.
        std::vector<unsigned int> fieldFormats;
    };
    Owned  owned_;
    Config cfg_;
};

}  // namespace aether::sim
