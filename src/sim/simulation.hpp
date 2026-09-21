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
#include "rule/compile.hpp"
#include "sim/gpu_step.hpp"
#include "sim/hash.hpp"
#include "sim/journal.hpp"
#include "sim/lineage.hpp"
#include "sim/rule_mutation.hpp"
#include "sim/session.hpp"
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

    // --- Sessions (F-020, SPEC §11) --------------------------------------------
    // The complete record of this run, including the current cells and
    // stream A's state so it can be resumed without replay.
    Session session();

    // Resumes a session at its saved generation from the stored state. If the
    // file carries no state, replays to it.
    static std::variant<Simulation, core::Error> resume(const Session& s, Path path = Path::Gpu);

    // Rebuilds the run from the initial cells, the seeds and the journal,
    // stopping at `generation` with journal events [0, journalEnd) applied
    // (SIZE_MAX = all events up to and at that generation) and, if
    // `mutateAtEnd`, the rule mutation due at that generation performed.
    struct ReplayTarget {
        uint64_t generation;
        size_t   journalEnd  = SIZE_MAX;
        bool     mutateAtEnd = false;
    };
    static std::variant<Simulation, core::Error> replay(const Session& s, ReplayTarget target, Path path = Path::Gpu);

    // Time travel to lineage entry `i`: the grid and rule as they were when
    // that entry took effect. Journal and lineage are truncated to that
    // point, since the run's future from there is abandoned.
    static std::variant<Simulation, core::Error> rewindGrid(const Session& s, size_t entry, Path path = Path::Gpu);

    const Journal& journal() const { return journal_; }
    const std::vector<uint8_t>& initialCells() const { return initial_; }
    uint64_t seedA() const { return seedA_; }

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
    const rule::CompiledRule& compiled() const { return lut_; }
    rule::Backend        backend() const { return lut_.backend; }

    // --- Time ---------------------------------------------------------------
    void     step();                  // one generation on the active path
    uint32_t frame(double dt);        // as many as the scheduler says
    uint64_t generation() const { return generation_; }
    Scheduler&       scheduler()       { return scheduler_; }
    const Scheduler& scheduler() const { return scheduler_; }

    // --- Rule mutation and lineage (F-015, F-017) -------------------------------
    void setRuleMutation(RuleMutationParams p);
    const RuleMutationParams& ruleMutation() const { return ruleMutation_; }
    const Lineage& lineage() const { return lineage_; }
    void pin(size_t entry, std::string name) { lineage_.pin(entry, std::move(name)); }
    void unpin(size_t entry) { lineage_.unpin(entry); }
    // Restores the rule of lineage entry `i` as the current rule, recorded
    // as a new entry at the current generation. Grid state is untouched.
    std::optional<core::Error> rewind(size_t entry);

    struct Counters {
        uint64_t rule_mutations         = 0;
        uint64_t rule_mutations_skipped = 0;   // eight invalid draws in a row
    };
    const Counters& counters() const { return counters_; }

    // --- Cell mutation (F-016, SPEC §9.2) -------------------------------------
    void    setCellMutation(double p, uint8_t blockShift = 0);
    double  cellMutation() const { return cellMutationP_; }
    uint8_t cellMutationBlock() const { return mutation_.blockShift; }
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

    // Writes a pattern into the grid with its own (0,0,0) at (x, y, z), on
    // both the host copy and the GPU texture (F-012, SPEC §14). Journalled,
    // so a session replays the paste; the pattern travels in the event
    // because a path could change under the session.
    //
    // Refused rather than coerced when the pattern does not belong here: a
    // different lattice means the same cells are a different shape, a
    // different cell type is a different thing entirely, a state the rule
    // does not have would index past its table, and a pattern hanging over
    // the edge would have to be clipped or wrapped and neither is obviously
    // what was meant.
    // Why this pattern cannot go there, or nothing. Asked before the click so
    // that a refusal can be shown rather than only logged after the fact.
    std::optional<core::Error> canPlace(const Pattern& p, uint32_t x, uint32_t y, uint32_t z) const;
    std::optional<core::Error> placePattern(const Pattern& p, uint32_t x, uint32_t y, uint32_t z);

    // The opposite: a region of the grid as a pattern, ready to write out.
    // Syncs from the GPU first, so it is not for the step loop (AV-002).
    std::variant<Pattern, core::Error> extractPattern(uint32_t x, uint32_t y, uint32_t z,
                                                      uint32_t w, uint32_t h, uint32_t d);
    Pcg32& streamA() { return streamA_; }

    // The texture holding the current generation, for the renderer.
    unsigned int texture() const { return gpu_.current(); }
    unsigned int textureTarget() const { return gpu_.target(); }

private:
    Simulation(core::HostGrid host, core::GpuGrid gpu, Path path, uint64_t seedA, uint64_t seedB);
    void resetOutOfRangeStates(uint16_t states);
    std::optional<core::Error> installRule(const rule::RuleIR& ir, LineageOrigin origin, std::optional<size_t> rewoundFrom);
    void maybeMutateRule();
    void applyEvent(const Event& ev);
    void journal(uint64_t generation, EventBody body) { journal_.push_back({generation, std::move(body)}); }

    core::HostGrid host_;
    core::GpuGrid  gpu_;
    rule::RuleIR   ir_;
    rule::CompiledRule  lut_;
    GpuStepper     gpuStepper_;
    Scheduler      scheduler_;
    Pcg32          streamA_;
    Path           path_;
    uint64_t       generation_ = 0;
    CellMutation   mutation_;
    double         cellMutationP_ = 0.0;
    RuleMutationParams ruleMutation_;
    Lineage        lineage_;
    Counters       counters_;
    Journal        journal_;
    std::vector<uint8_t> initial_;         // cells at generation 0 before any event
    uint64_t       seedA_ = 0;
    uint64_t       mutatedAt_ = UINT64_MAX;   // generation whose rule mutation has already run
};

}  // namespace aether::sim
