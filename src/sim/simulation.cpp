#include "sim/simulation.hpp"

#include "sim/cpu_step.hpp"
#include "sim/fill.hpp"

#include <algorithm>
#include <format>
#include <type_traits>
#include <utility>

namespace aether::sim {

namespace {

// A placeholder CompiledRule so Simulation has a value before its first setRule.
// Never stepped: create() replaces it or fails.
rule::CompiledRule emptyLut() {
    return rule::CompiledRule{
        .ir_hash = 0, .dimensions = 2, .states = 2, .kind = rule::Kind::OuterTotalistic,
        .neighbourhood = {}, .counted = {}, .boundary = rule::Boundary::Wrap, .offsets = {},
        .layout = rule::TableLayout(rule::Kind::OuterTotalistic, 2, 0), .table = {}, .aux = {},
        .expression = {}, .expressionTypes = {}, .glsl = {}, .fields = {},
    };
}

}  // namespace

Simulation::Simulation(core::HostGrid host, core::GpuGrid gpu, Path path, uint64_t seedA, uint64_t seedB)
    : host_(std::move(host)), gpu_(std::move(gpu)), lut_(emptyLut()), streamA_(seedA), path_(path), seedA_(seedA) {
    mutation_.seedB = seedB;
    initial_.assign(host_.current().begin(), host_.current().end());
}

std::variant<Simulation, core::Error> Simulation::create(const core::GridSpec& spec, const rule::RuleIR& ir,
                                                         Path path, uint64_t seedA, uint64_t seedB) {
    if (const auto problems = spec.problems(); !problems.empty()) return core::Error{problems.front()};
    auto gpu = core::GpuGrid::create(spec, core::queryVram());
    if (const auto* e = std::get_if<core::Error>(&gpu)) return *e;

    Simulation sim(core::HostGrid(spec), std::get<core::GpuGrid>(std::move(gpu)), path, seedA, seedB);
    if (auto e = sim.installRule(ir, LineageOrigin::Initial, std::nullopt)) return *e;
    return sim;
}

std::optional<core::Error> Simulation::setRule(const rule::RuleIR& ir) {
    const auto err = installRule(ir, LineageOrigin::User, std::nullopt);
    if (!err) journal(generation_, EvSetRule{ir});
    return err;
}

std::optional<core::Error> Simulation::rewind(size_t entry) {
    if (entry >= lineage_.size()) return core::Error{"no such lineage entry"};
    const auto err = installRule(lineage_.at(entry).ir, LineageOrigin::Rewind, entry);
    if (!err) journal(generation_, EvRewind{entry});
    return err;
}

void Simulation::setRuleMutation(RuleMutationParams p) {
    p.interval = std::max(1u, p.interval);
    p.magnitude = std::max(1u, p.magnitude);
    ruleMutation_ = p;
    journal(generation_, EvRuleMutation{p});
}

// SPEC §9.1: every `interval` generations, before the step. Runs at most
// once per generation, so replay can perform it explicitly.
void Simulation::maybeMutateRule() {
    if (mutatedAt_ == generation_) return;
    mutatedAt_ = generation_;
    if (!ruleMutation_.enabled || generation_ == 0 || generation_ % ruleMutation_.interval != 0) return;
    MutationResult m = mutateRule(ir_, ruleMutation_.magnitude, streamA_);
    if (!m.ir) {
        ++counters_.rule_mutations_skipped;
        return;
    }
    if (installRule(*m.ir, LineageOrigin::Mutation, std::nullopt)) {
        ++counters_.rule_mutations_skipped;   // compile refused it; treated as a skip
        return;
    }
    ++counters_.rule_mutations;
}

std::optional<core::Error> Simulation::installRule(const rule::RuleIR& ir, LineageOrigin origin, std::optional<size_t> rewoundFrom) {
    if (ir.dimensions != spec().dimensions) {
        return core::Error{std::format("rule is {}D but the grid is {}D", ir.dimensions, spec().dimensions)};
    }
    // The grid's storage and the rule's cell type must agree: a u8 texture
    // stepped by a float rule is silent garbage, not an error, because the
    // formats are decided independently on either side. Checked here rather
    // than in create() alone, so that swapping the rule under a running grid
    // is guarded by the same test that guards building one.
    if (ir.cell_type != spec().cell_type) {
        return core::Error{std::format("grid holds {} cells but the rule is {}",
                                       core::toString(spec().cell_type), core::toString(ir.cell_type))};
    }
    auto compiled = rule::compileRule(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&compiled)) return core::Error{e->message};
    rule::CompiledRule lut = std::get<rule::CompiledRule>(std::move(compiled));

    // The GPU stepper keeps its previous rule if this fails.
    if (auto e = gpuStepper_.setRule(lut, spec())) return e;

    // Everything that can fail has succeeded; commit, and record it. A rule
    // change that is not in the lineage is an incomplete operation
    // (ARCHITECTURE §Key invariants 7).
    if (lut.states < lut_.states || lut_.table.empty()) resetOutOfRangeStates(lut.states);
    ir_  = ir;
    lut_ = std::move(lut);
    // A user change journals *after* install, so the entry's journal index
    // excludes its own event; replaying [0, index) then applying the event
    // itself reproduces the entry.
    lineage_.append(generation_, ir_, origin, journal_.size(), rewoundFrom);
    return std::nullopt;
}

void Simulation::resetOutOfRangeStates(uint16_t states) {
    // A continuous cell holds a value, not an index, so there is no such
    // thing as out of range for it: every float in [0, 1] stays valid under
    // any kernel. Walking the buffer as bytes here would shred it.
    if (spec().cell_type == core::CellType::F32) return;

    if (path_ == Path::Gpu) syncToHost();
    bool changed = false;
    for (uint8_t& c : host_.current()) {
        if (c >= states) { c = 0; changed = true; }
    }
    if (changed || path_ == Path::Gpu) commitHost();
}

void Simulation::setCellMutation(double p, uint8_t blockShift) {
    cellMutationP_ = std::clamp(p, 0.0, 1.0);
    mutation_.threshold = mutationThreshold(cellMutationP_);
    mutation_.blockShift = std::min<uint8_t>(blockShift, 16);
    gpuStepper_.setCellMutation(mutation_);
    journal(generation_, EvCellMutation{cellMutationP_, mutation_.blockShift});
}

void Simulation::step() {
    maybeMutateRule();
    if (path_ == Path::Gpu) {
        gpuStepper_.setGeneration(generation_);
        gpuStepper_.setCellMutation(mutation_);
        gpuStepper_.step(gpu_);
    } else {
        cpuStep(lut_, host_, generation_, mutation_);
        gpu_.upload(host_.current());   // keep the renderer's texture current
    }
    ++generation_;
}

uint32_t Simulation::frame(double dt) {
    return scheduler_.update(dt, [this] { step(); });
}

uint32_t Simulation::frame(double dt, const std::function<void()>& afterStep) {
    return scheduler_.update(dt, [&] { step(); afterStep(); });
}

std::optional<core::Error> Simulation::setPath(Path p) {
    if (p == path_) return std::nullopt;
    if (p == Path::Cpu) {
        syncToHost();          // GPU was authoritative; take a copy
    } else {
        commitHost();          // host was authoritative; push it up
    }
    path_ = p;
    return std::nullopt;
}

void Simulation::syncToHost() {
    if (path_ == Path::Gpu) gpu_.download(host_.current());
}

void Simulation::commitHost() {
    gpu_.upload(host_.current());
}

void Simulation::clear() {
    host_.clear();
    commitHost();
    journal(generation_, EvClear{});
}

void Simulation::paintSpan(uint32_t x0, uint32_t x1, uint32_t y, uint32_t z, uint8_t state) {
    auto cells = host_.current();
    const size_t start = host_.index(x0, y, z);
    const size_t n = x1 - x0 + 1;
    if (spec().cell_type == core::CellType::F32) {
        // The brush carries a state index, which a continuous grid reads as
        // full or empty: anything but the quiescent state paints 1.0. A finer
        // brush would need a value in the event, and the journal format with
        // it (SPEC §11).
        auto values = host_.currentFloats();
        std::fill_n(values.begin() + static_cast<std::ptrdiff_t>(start), n, state == 0 ? 0.0f : 1.0f);
    } else {
        std::fill_n(cells.begin() + static_cast<std::ptrdiff_t>(start), n, state);
    }
    const uint32_t bytes = core::cellBytes(spec().cell_type);
    gpu_.uploadRegion(x0, y, z, static_cast<uint32_t>(n), 1, 1, cells.subspan(start * bytes, n * bytes));
    journal(generation_, EvPaint{x0, x1, y, z, state});
}

namespace {

// A pattern's lattice is the rule's: hex neighbourhoods are the only ones
// stored axially, and the pattern format names the lattice rather than the
// neighbourhood because a pattern has no radius (SPEC §14).
}  // namespace

std::optional<core::Error> Simulation::canPlace(const Pattern& p, uint32_t x, uint32_t y, uint32_t z) const {
    if (auto e = patternFits(p, ir_, spec(), x, y, z)) return core::Error{e->message};
    return std::nullopt;
}

std::optional<core::Error> Simulation::placePattern(const Pattern& p, uint32_t x, uint32_t y, uint32_t z) {
    if (auto e = canPlace(p, x, y, z)) return e;
    blitPattern(p, spec(), host_.current(), x, y, z);
    // The GPU takes the pattern's own buffer whole, which is exactly the
    // layout uploadRegion wants.
    gpu_.uploadRegion(x, y, z, p.width, p.height, p.depth, p.cells);
    journal(generation_, EvPlace{p, x, y, z});
    return std::nullopt;
}

std::variant<Pattern, core::Error> Simulation::extractPattern(uint32_t x, uint32_t y, uint32_t z,
                                                              uint32_t w, uint32_t h, uint32_t d) {
    syncToHost();
    auto got = extractRegion(ir_, spec(), host_.current(), x, y, z, w, h, d);
    if (const auto* e = std::get_if<PatternError>(&got)) return core::Error{e->message};
    return std::get<Pattern>(got);
}

void Simulation::fillRegion(uint32_t x, uint32_t y, uint32_t z, uint32_t w, uint32_t h, uint32_t d,
                            std::span<const double> density) {
    if (w == 0 || h == 0 || d == 0) return;
    // The host is the authority for a fill either way: on the GPU path the
    // copy here is stale, so it has to come down before part of it is rewritten
    // or the untouched cells would be written back from an old snapshot.
    syncToHost();
    sim::fillRandomRegion(host_, x, y, z, w, h, d, density, streamA_);
    commitHost();
    journal(generation_, EvFillRegion{x, y, z, w, h, d, std::vector<double>(density.begin(), density.end())});
}

void Simulation::fillRandom(std::span<const double> density) {
    sim::fillRandom(host_, density, streamA_);
    commitHost();
    journal(generation_, EvFill{std::vector<double>(density.begin(), density.end())});
}

// --- Sessions ----------------------------------------------------------------------

Session Simulation::session() {
    syncToHost();
    Session s;
    s.spec = spec();
    s.boundary = ir_.boundary;
    s.initial = initial_;
    s.seedA = seedA_;
    s.seedB = mutation_.seedB;
    s.journal = journal_;
    s.lineage = lineage_.entries();
    s.ruleMutation = ruleMutation_;
    s.cellMutationP = cellMutationP_;
    s.cellMutationBlock = mutation_.blockShift;
    s.generation = generation_;
    s.current.assign(host_.current().begin(), host_.current().end());
    s.streamA = streamA_.state();
    s.rule = ir_;
    s.ruleMutationsApplied = counters_.rule_mutations;
    s.ruleMutationsSkipped = counters_.rule_mutations_skipped;
    return s;
}

void Simulation::applyEvent(const Event& ev) {
    std::visit([&](const auto& b) {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, EvSetRule>)            (void)setRule(b.ir);
        else if constexpr (std::is_same_v<T, EvRewind>)        (void)rewind(b.entry);
        else if constexpr (std::is_same_v<T, EvPaint>)         paintSpan(b.x0, b.x1, b.y, b.z, b.state);
        else if constexpr (std::is_same_v<T, EvPlace>)         (void)placePattern(b.pattern, b.x, b.y, b.z);
        else if constexpr (std::is_same_v<T, EvFill>)          fillRandom(b.density);
        else if constexpr (std::is_same_v<T, EvFillRegion>)    fillRegion(b.x, b.y, b.z, b.w, b.h, b.d, b.density);
        else if constexpr (std::is_same_v<T, EvClear>)         clear();
        else if constexpr (std::is_same_v<T, EvCellMutation>)  setCellMutation(b.p, b.blockShift);
        else if constexpr (std::is_same_v<T, EvRuleMutation>)  setRuleMutation(b.params);
    }, ev.body);
}

std::variant<Simulation, core::Error> Simulation::replay(const Session& s, ReplayTarget target, Path path) {
    if (s.lineage.empty()) return core::Error{"session has no lineage; no initial rule"};
    if (s.initial.size() != s.spec.bytesPerBuffer()) return core::Error{"session initial cells do not match the grid"};
    auto made = create(s.spec, s.lineage.front().ir, path, s.seedA, s.seedB);
    if (const auto* e = std::get_if<core::Error>(&made)) return *e;
    Simulation sim = std::get<Simulation>(std::move(made));

    // Initial cells are the state before any event; they are not journaled.
    std::copy(s.initial.begin(), s.initial.end(), sim.host_.current().begin());
    sim.commitHost();
    sim.initial_ = s.initial;

    const size_t journalEnd = std::min(target.journalEnd, s.journal.size());
    size_t idx = 0;
    for (;;) {
        while (idx < journalEnd && s.journal[idx].generation <= sim.generation_) {
            if (s.journal[idx].generation < sim.generation_) {
                return core::Error{std::format("journal event at generation {} is out of order", s.journal[idx].generation)};
            }
            sim.applyEvent(s.journal[idx]);
            ++idx;
        }
        if (sim.generation_ >= target.generation) break;
        sim.step();
    }
    if (target.mutateAtEnd) sim.maybeMutateRule();
    return sim;
}

std::variant<Simulation, core::Error> Simulation::resume(const Session& s, Path path) {
    if (s.current.empty() || !s.streamA) {
        return replay(s, ReplayTarget{s.generation}, path);
    }
    if (s.lineage.empty()) return core::Error{"session has no lineage; no initial rule"};
    if (s.current.size() != s.spec.bytesPerBuffer() || s.initial.size() != s.spec.bytesPerBuffer()) {
        return core::Error{"session cells do not match the grid"};
    }
    auto made = create(s.spec, s.lineage.front().ir, path, s.seedA, s.seedB);
    if (const auto* e = std::get_if<core::Error>(&made)) return *e;
    Simulation sim = std::get<Simulation>(std::move(made));

    // Install the current rule without journaling or appending: the lineage
    // and journal come from the file as they were.
    sim.lineage_ = Lineage{};
    for (const LineageEntry& e : s.lineage) {
        sim.lineage_.append(e.generation, e.ir, e.origin, e.journal_index, e.rewound_from);
        if (e.pinned) sim.lineage_.pin(sim.lineage_.size() - 1, e.name.value_or(""));
        else if (e.name) sim.lineage_.pin(sim.lineage_.size() - 1, *e.name), sim.lineage_.unpin(sim.lineage_.size() - 1);
    }
    sim.journal_ = s.journal;
    sim.initial_ = s.initial;
    sim.generation_ = s.generation;
    sim.streamA_ = Pcg32::fromState(*s.streamA);
    sim.ruleMutation_ = s.ruleMutation;
    sim.cellMutationP_ = s.cellMutationP;
    sim.mutation_.threshold = mutationThreshold(s.cellMutationP);
    sim.mutation_.blockShift = s.cellMutationBlock;
    sim.gpuStepper_.setCellMutation(sim.mutation_);
    sim.counters_.rule_mutations = s.ruleMutationsApplied;
    sim.counters_.rule_mutations_skipped = s.ruleMutationsSkipped;

    // The current rule, compiled; lineage already holds it, so bypass the append.
    auto compiled = rule::compileRule(s.rule);
    if (const auto* e = std::get_if<rule::CompileError>(&compiled)) return core::Error{e->message};
    rule::CompiledRule lut = std::get<rule::CompiledRule>(std::move(compiled));
    if (auto e = sim.gpuStepper_.setRule(lut, s.spec)) return *e;
    sim.ir_ = s.rule;
    sim.lut_ = std::move(lut);

    std::copy(s.current.begin(), s.current.end(), sim.host_.current().begin());
    sim.commitHost();
    // A rule mutation due at this generation has already happened if the
    // save came after it; the lineage tells us.
    if (!sim.lineage_.empty() && sim.lineage_.back().generation == sim.generation_ &&
        sim.lineage_.back().origin == LineageOrigin::Mutation) {
        sim.mutatedAt_ = sim.generation_;
    }
    return sim;
}

std::variant<Simulation, core::Error> Simulation::rewindGrid(const Session& s, size_t entry, Path path) {
    if (entry >= s.lineage.size()) return core::Error{"no such lineage entry"};
    const LineageEntry& target = s.lineage[entry];
    ReplayTarget t{target.generation, target.journal_index, target.origin == LineageOrigin::Mutation};
    auto made = replay(s, t, path);
    if (const auto* e = std::get_if<core::Error>(&made)) return *e;
    Simulation sim = std::get<Simulation>(std::move(made));
    if (target.origin == LineageOrigin::User || target.origin == LineageOrigin::Rewind) {
        // The event that created the entry is the next one in the journal.
        if (target.journal_index < s.journal.size()) sim.applyEvent(s.journal[target.journal_index]);
    }
    if (rule::irHash(sim.ir_) != target.ir_hash) {
        return core::Error{std::format("replay reached generation {} with rule {:#018x}, expected {:#018x}",
                                       sim.generation_, rule::irHash(sim.ir_), target.ir_hash)};
    }
    // Names and pins survive on the entries that remain.
    for (size_t i = 0; i < sim.lineage_.size() && i < s.lineage.size(); ++i) {
        if (s.lineage[i].pinned) sim.lineage_.pin(i, s.lineage[i].name.value_or(""));
    }
    return sim;
}

}  // namespace aether::sim
