// Grid storage (SPEC §2).
//
// A dense lattice of fixed extent held as a ping-pong pair. This file is the
// host side: the spec, the footprint arithmetic, and the byte buffers the CPU
// reference path steps and save/load reads. The GPU pair is in gpu_grid.hpp.
//
// core/ knows nothing about rules. Boundary handling is rule semantics and
// lives with the steppers.

#pragma once

#include "core/cell.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aether::core {

struct GridSpec {
    uint8_t  dimensions = 2;
    uint32_t width      = 1;
    uint32_t height     = 1;   // must be 1 when dimensions < 2
    uint32_t depth      = 1;   // must be 1 when dimensions < 3
    CellType cell_type  = CellType::U8;

    uint64_t cellCount()      const { return uint64_t{width} * height * depth; }
    uint64_t bytesPerBuffer() const { return cellCount() * cellBytes(cell_type); }
    uint64_t footprintBytes() const { return bytesPerBuffer() * 2; }   // the pair

    // Empty when well-formed.
    std::vector<std::string> problems() const;

    bool operator==(const GridSpec&) const = default;
};

// The one place a read/write pair is swapped (ARCHITECTURE §Key invariants 3,
// AV-004). Steppers read current() and write next(); nothing else touches
// next(); swap() is called between generations by whoever owns the step.
template <typename T>
class PingPong {
public:
    PingPong() = default;
    PingPong(T a, T b) : buf_{std::move(a), std::move(b)} {}

    T&       current()       { return buf_[cur_]; }
    const T& current() const { return buf_[cur_]; }
    T&       next()          { return buf_[cur_ ^ 1]; }
    const T& next()    const { return buf_[cur_ ^ 1]; }

    void swap() { cur_ ^= 1; }

private:
    T   buf_[2];
    int cur_ = 0;
};

// Storage is bytes whatever the cell type, with typed views over it (SPEC §1).
// A variant over vector<uint8_t> and vector<float> was the alternative; bytes
// won because the session codec, the sidecar, the GPU transfers and the VRAM
// arithmetic are all in bytes already, so only the accessors have to care.
class HostGrid {
public:
    explicit HostGrid(GridSpec spec);

    const GridSpec& spec() const { return spec_; }

    // The raw buffers. Sized in bytes, not cells: for an f32 grid these are
    // four times as long as the cell count.
    std::span<const uint8_t> current() const { return pair_.current(); }
    std::span<uint8_t>       current()       { return pair_.current(); }
    std::span<uint8_t>       next()          { return pair_.next(); }
    void swap() { pair_.swap(); }

    // Linear index of a cell. Callers pass in-range coordinates; boundary
    // resolution has already happened by the time a stepper gets here.
    // This counts cells; multiply by cellBytes() to reach a byte.
    size_t index(uint32_t x, uint32_t y = 0, uint32_t z = 0) const {
        return (size_t{z} * spec_.height + y) * spec_.width + x;
    }

    // u8 accessors on the current buffer. The canvas and the random fill
    // edit the current state through these; they never see next().
    uint8_t get(uint32_t x, uint32_t y = 0, uint32_t z = 0) const {
        assert(spec_.cell_type == CellType::U8);
        return pair_.current()[index(x, y, z)];
    }
    void set(uint32_t x, uint32_t y, uint32_t z, uint8_t v) {
        assert(spec_.cell_type == CellType::U8);
        pair_.current()[index(x, y, z)] = v;
    }

    // f32 views and accessors (Phase 5). The buffers are allocated through
    // std::vector<uint8_t>, whose allocator returns storage aligned for any
    // scalar type, so a float view over a whole buffer is properly aligned.
    std::span<const float> currentFloats() const { return asFloats(current()); }
    std::span<float>       currentFloats()       { return asFloats(current()); }
    std::span<float>       nextFloats()          { return asFloats(next()); }

    float getFloat(uint32_t x, uint32_t y = 0, uint32_t z = 0) const {
        assert(spec_.cell_type == CellType::F32);
        return currentFloats()[index(x, y, z)];
    }
    void setFloat(uint32_t x, uint32_t y, uint32_t z, float v) {
        assert(spec_.cell_type == CellType::F32);
        currentFloats()[index(x, y, z)] = v;
    }

    // Both buffers to the quiescent value: state 0 for u8, +0.0 for f32,
    // which is all-zero bits either way.
    void clear();

private:
    static std::span<float> asFloats(std::span<uint8_t> b) {
        return {reinterpret_cast<float*>(b.data()), b.size() / sizeof(float)};
    }
    static std::span<const float> asFloats(std::span<const uint8_t> b) {
        return {reinterpret_cast<const float*>(b.data()), b.size() / sizeof(float)};
    }

    GridSpec                           spec_;
    PingPong<std::vector<uint8_t>>     pair_;
};

}  // namespace aether::core
