// Auxiliary field storage for a test (F-031).
//
// The steppers want a buffer pair per declared field and a span apiece; the
// engine's own storage for that is `Simulation`'s, which a test comparing the
// two steppers directly does not have. This is that, and only that: allocated
// up front so the step loop allocates nothing, with a matching set of GPU pairs
// for the side that needs them.
//
// Here rather than in one of the test files because two of them need it, and a
// second copy of the pairing between a field's index, its cell type and its
// buffer is a second place for it to be wrong.

#pragma once

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "rule/compile.hpp"
#include "sim/cpu_step.hpp"
#include "sim/gpu_step.hpp"

#include <cstring>
#include <span>
#include <variant>
#include <vector>

namespace aether::test {

// The host halves: a byte pair per field, with the spans cpuStep wants.
class HostFields {
public:
    HostFields(const rule::CompiledRule& rule, uint64_t cells) {
        for (const rule::CompiledField& f : rule.fields) {
            types_.push_back(f.cell_type);
            const size_t bytes = cells * core::cellBytes(f.cell_type);
            buffers_[0].emplace_back(bytes, uint8_t{0});
            buffers_[1].emplace_back(bytes, uint8_t{0});
        }
        rebuild();
    }

    size_t size() const { return types_.size(); }
    sim::FieldReads  reads()  const { return reads_; }
    sim::FieldWrites writes() const { return writes_; }

    void swap() {
        cur_ ^= 1;
        rebuild();
    }

    const std::vector<uint8_t>& raw(size_t f) const { return buffers_[cur_][f]; }
    core::CellType type(size_t f) const { return types_[f]; }

    void setU8(size_t f, size_t i, uint8_t v) { buffers_[cur_][f][i] = v; }
    uint8_t u8(size_t f, size_t i) const { return buffers_[cur_][f][i]; }

    void setF32(size_t f, size_t i, float v) {
        std::memcpy(buffers_[cur_][f].data() + i * sizeof(float), &v, sizeof(float));
    }
    float f32(size_t f, size_t i) const {
        float v = 0.0f;
        std::memcpy(&v, buffers_[cur_][f].data() + i * sizeof(float), sizeof(float));
        return v;
    }

    void fillU8(size_t f, uint8_t v) {
        for (uint8_t& b : buffers_[cur_][f]) b = v;
    }

private:
    void rebuild() {
        reads_.clear();
        writes_.clear();
        for (auto& b : buffers_[cur_])     reads_.emplace_back(b);
        for (auto& b : buffers_[cur_ ^ 1]) writes_.emplace_back(b);
    }

    std::vector<core::CellType>           types_;
    std::vector<std::vector<uint8_t>>     buffers_[2];
    std::vector<std::span<const uint8_t>> reads_;
    std::vector<std::span<uint8_t>>       writes_;
    int cur_ = 0;
};

// The GPU halves: a GpuGrid pair per field, which is all a field needs — a
// field is a grid of one value per site. Returns nothing on failure; the caller
// has a REQUIRE to spend and this header cannot.
class GpuFields {
public:
    static std::variant<GpuFields, core::Error> create(const rule::CompiledRule& rule,
                                                      const core::GridSpec& spec) {
        GpuFields made;
        for (const rule::CompiledField& f : rule.fields) {
            core::GridSpec fs = spec;
            fs.cell_type = f.cell_type;
            auto g = core::GpuGrid::create(fs, core::queryVram());
            if (const auto* e = std::get_if<core::Error>(&g)) return *e;
            made.grids_.push_back(std::move(std::get<core::GpuGrid>(g)));
        }
        made.textures_.assign(made.grids_.size(), sim::FieldTextures{});
        return made;
    }

    size_t size() const { return grids_.size(); }
    void upload(const HostFields& host) {
        for (size_t f = 0; f < grids_.size(); ++f) grids_[f].upload(host.raw(f));
    }
    void download(size_t f, std::span<uint8_t> out) const { grids_[f].download(out); }

    // Refilled rather than rebuilt, for the same reason the engine does it.
    std::span<const sim::FieldTextures> textures() {
        for (size_t f = 0; f < grids_.size(); ++f) textures_[f] = {grids_[f].current(), grids_[f].next()};
        return textures_;
    }
    void swap() {
        for (core::GpuGrid& g : grids_) g.swap();
    }

private:
    std::vector<core::GpuGrid>        grids_;
    std::vector<sim::FieldTextures>   textures_;
};

}  // namespace aether::test
