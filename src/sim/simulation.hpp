// The running simulation: one grid, one compiled rule, two execution paths,
// and the scheduler that paces them.
//
// This is the object main.cpp and ui/ talk to. It owns the "step then swap"
// sequence, the CPU/GPU path flag (F-002), and the rule swap that AV-014
// requires to be all-or-nothing. Rendering reads texture() and nothing else.
//
// Authority: on the GPU path the GPU pair is the truth and the host copy is
// stale until syncToHost(); on the CPU path the host pair is the truth and
// its current buffer is mirrored to the GPU after every step so the renderer
// always has something to draw. Requires a GL 4.3 context.

#pragma once

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "rule/ir.hpp"
#include "rule/lut.hpp"
#include "sim/gpu_step.hpp"
#include "sim/hash.hpp"
#include "sim/rng.hpp"
#include "sim/scheduler.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace aether::sim {

enum class Path : uint8_t { Gpu, Cpu };

class Simulation {
public:
    static std::variant<Simulation, core::Error> create(const core::GridSpec& spec, const rule::RuleIR& ir,
                                                        Path path = Path::Gpu, uint64_t seedA = 0,
                                                        uint64_t seedB = 0);

    Simulation(Simulation&&) noexcept = default;
    Simulation& operator=(Simulation&&) noexcept = default;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // --- Rule ---------------------------------------------------------------
    // Compiles completely before anything is replaced; on failure the
    // running rule is untouched. Cells at or above the new state count are
    // reset to 0 so no lookup can index past the new table.
    std::optional<core::Error> setRule(const rule::RuleIR& ir);
    const rule::RuleIR&  rule() const { return ir_; }
    const rule::LutRule& lut()  const { return lut_; }
    rule::Backend        backend() const { return rule::Backend::Lut; }

    // --- Time ---------------------------------------------------------------
    void     step();                  // one generation on the active path
    uint32_t frame(double dt);        // as many as the scheduler says
    uint64_t generation() const { return generation_; }
    Scheduler&       scheduler()       { return scheduler_; }
    const Scheduler& scheduler() const { return scheduler_; }

    // --- Cell mutation (F-016, SPEC §9.2) -------------------------------------
    void   setCellMutation(double p);
    double cellMutation() const { return cellMutationP_; }
    uint64_t seedB() const { return mutation_.seedB; }

    // --- Path ---------------------------------------------------------------
    Path path() const { return path_; }
    std::optional<core::Error> setPath(Path p);   // syncs state across

    // --- State --------------------------------------------------------------
    const core::GridSpec& spec() const { return host_.spec(); }
    core::HostGrid& host() { return host_; }      // edit, then commitHost()
    void syncToHost();                            // GPU -> host (a readback; never per frame)
    void commitHost();                            // host -> GPU
    void clear();
    void fillRandom(std::span<const double> density);   // draws from stream A

    // Sets cells x0..x1 inclusive on row (y, z) to `state`, on both the
    // host copy and the GPU texture, with no readback. The canvas's one way
    // in. Coordinates must be in range; x0 <= x1.
    void paintSpan(uint32_t x0, uint32_t x1, uint32_t y, uint32_t z, uint8_t state);
    Pcg32& streamA() { return streamA_; }

    // The texture holding the current generation, for the renderer.
    unsigned int texture() const { return gpu_.current(); }
    unsigned int textureTarget() const { return gpu_.target(); }

private:
    Simulation(core::HostGrid host, core::GpuGrid gpu, Path path, uint64_t seedA, uint64_t seedB);
    void resetOutOfRangeStates(uint16_t states);

    core::HostGrid host_;
    core::GpuGrid  gpu_;
    rule::RuleIR   ir_;
    rule::LutRule  lut_;
    GpuStepper     gpuStepper_;
    Scheduler      scheduler_;
    Pcg32          streamA_;
    Path           path_;
    uint64_t       generation_ = 0;
    CellMutation   mutation_;
    double         cellMutationP_ = 0.0;
};

}  // namespace aether::sim
