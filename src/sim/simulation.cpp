#include "sim/simulation.hpp"

#include "sim/cpu_step.hpp"
#include "sim/fill.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace aether::sim {

namespace {

// A placeholder LutRule so Simulation has a value before its first setRule.
// Never stepped: create() replaces it or fails.
rule::LutRule emptyLut() {
    return rule::LutRule{
        .ir_hash = 0, .dimensions = 2, .states = 2, .kind = rule::Kind::OuterTotalistic,
        .neighbourhood = {}, .boundary = rule::Boundary::Wrap, .offsets = {},
        .layout = rule::TableLayout(rule::Kind::OuterTotalistic, 2, 0), .table = {}, .w = {},
    };
}

}  // namespace

Simulation::Simulation(core::HostGrid host, core::GpuGrid gpu, Path path, uint64_t seedA)
    : host_(std::move(host)), gpu_(std::move(gpu)), lut_(emptyLut()), streamA_(seedA), path_(path) {}

std::variant<Simulation, core::Error> Simulation::create(const core::GridSpec& spec, const rule::RuleIR& ir,
                                                         Path path, uint64_t seedA) {
    if (const auto problems = spec.problems(); !problems.empty()) return core::Error{problems.front()};
    auto gpu = core::GpuGrid::create(spec, core::queryVram());
    if (const auto* e = std::get_if<core::Error>(&gpu)) return *e;

    Simulation sim(core::HostGrid(spec), std::get<core::GpuGrid>(std::move(gpu)), path, seedA);
    if (auto e = sim.setRule(ir)) return *e;
    return sim;
}

std::optional<core::Error> Simulation::setRule(const rule::RuleIR& ir) {
    if (ir.dimensions != spec().dimensions) {
        return core::Error{std::format("rule is {}D but the grid is {}D", ir.dimensions, spec().dimensions)};
    }
    if (rule::selectBackend(ir) != rule::Backend::Lut) {
        return core::Error{"this rule needs the codegen backend, which arrives in Phase 4"};
    }
    auto compiled = rule::compileLut(ir);
    if (const auto* e = std::get_if<rule::CompileError>(&compiled)) return core::Error{e->message};
    rule::LutRule lut = std::get<rule::LutRule>(std::move(compiled));

    // The GPU stepper keeps its previous rule if this fails.
    if (auto e = gpuStepper_.setRule(lut, spec())) return e;

    // Everything that can fail has succeeded; commit.
    if (lut.states < lut_.states || lut_.table.empty()) resetOutOfRangeStates(lut.states);
    ir_  = ir;
    lut_ = std::move(lut);
    return std::nullopt;
}

void Simulation::resetOutOfRangeStates(uint16_t states) {
    if (path_ == Path::Gpu) syncToHost();
    bool changed = false;
    for (uint8_t& c : host_.current()) {
        if (c >= states) { c = 0; changed = true; }
    }
    if (changed || path_ == Path::Gpu) commitHost();
}

void Simulation::step() {
    if (path_ == Path::Gpu) {
        gpuStepper_.setGeneration(generation_);
        gpuStepper_.step(gpu_);
    } else {
        cpuStep(lut_, host_);
        gpu_.upload(host_.current());   // keep the renderer's texture current
    }
    ++generation_;
}

uint32_t Simulation::frame(double dt) {
    return scheduler_.update(dt, [this] { step(); });
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
}

void Simulation::fillRandom(std::span<const double> density) {
    sim::fillRandom(host_, density, streamA_);
    commitHost();
}

}  // namespace aether::sim
