// GPU grid storage (SPEC §2) and the VRAM guard (AV-001).
//
// The ping-pong pair as two immutable-storage integer textures: GL_R8UI on
// GL_TEXTURE_2D for 1D and 2D grids, on GL_TEXTURE_3D for 3D. Requires a live
// GL 4.3 context; everything in grid.hpp works without one.

#pragma once

#include "core/grid.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace aether::core {

struct Error {
    std::string message;
};

// What the driver will say about free video memory, if anything. Intel Mesa
// says nothing; NVIDIA and AMD expose vendor extensions.
struct VramInfo {
    std::optional<uint64_t> available_bytes;
    std::string             source;   // "NVX", "ATI", or "unknown"
};

VramInfo queryVram();

// SPEC §2: footprint plus 25% headroom must fit in available VRAM. Passes
// when availability is unknown; the caller decides whether to warn. Pure, so
// it can be tested without a context.
std::optional<Error> checkFootprint(const GridSpec& spec, const VramInfo& vram);

// GL_MAX_TEXTURE_SIZE / GL_MAX_3D_TEXTURE_SIZE against the spec's extents.
std::optional<Error> checkTextureLimits(const GridSpec& spec);

class GpuGrid {
public:
    // Runs both checks, then allocates. A failure allocates nothing.
    static std::variant<GpuGrid, Error> create(const GridSpec& spec, const VramInfo& vram);

    GpuGrid(GpuGrid&&) noexcept;
    GpuGrid& operator=(GpuGrid&&) noexcept;
    GpuGrid(const GpuGrid&) = delete;
    GpuGrid& operator=(const GpuGrid&) = delete;
    ~GpuGrid();

    const GridSpec& spec() const { return spec_; }
    unsigned int target()  const { return target_; }   // GL_TEXTURE_2D or GL_TEXTURE_3D
    unsigned int format()  const;                       // internal format, e.g. GL_R8UI

    unsigned int current() const { return pair_.current(); }
    unsigned int next()    const { return pair_.next(); }
    void swap() { pair_.swap(); }

    // Host <-> current texture, whole grid. Both are synchronous and exist
    // for seeding, save/load and the equivalence tests, never for the step
    // loop (AV-002).
    void upload(std::span<const uint8_t> cells);
    void download(std::span<uint8_t> cells) const;

private:
    GpuGrid(GridSpec spec, unsigned int target, unsigned int a, unsigned int b);
    void release();

    GridSpec               spec_;
    unsigned int           target_ = 0;
    PingPong<unsigned int> pair_;
};

}  // namespace aether::core
