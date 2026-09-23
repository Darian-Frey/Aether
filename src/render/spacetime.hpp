// Space-time diagram for 1D automata (F-005).
//
// A one-dimensional automaton has nothing to look at in its own geometry: a
// row of cells changing is a flickering line. What is worth seeing is its
// history, so each generation becomes a raster row and time runs down the
// screen — the presentation every elementary-rule picture uses, and the one
// that makes rule 110's structure and rule 30's noise legible at all.
//
// The history is a texture of the grid's width by however many rows fit, and
// it is a *ring*: the newest generation overwrites the oldest, and where the
// ring's seam falls is decided at draw time rather than by shuffling rows.
// Scrolling a full history by copying it up one row every generation would be
// a texture copy per generation for no gain.
//
// The row is copied from the simulation's own texture on the GPU, so a 1D run
// costs no readback (AV-002). Nothing here writes to the simulation
// (ARCHITECTURE §Key invariants 6); it reads the grid texture and its own.

#pragma once

#include "core/gpu_grid.hpp"
#include "core/grid.hpp"
#include "render/renderer2d.hpp"

#include <cstdint>
#include <variant>

namespace aether::render {

class SpaceTime {
public:
    static std::variant<SpaceTime, core::Error> create(uint32_t width, uint32_t rows);

    SpaceTime(SpaceTime&&) noexcept;
    SpaceTime& operator=(SpaceTime&&) noexcept;
    SpaceTime(const SpaceTime&) = delete;
    SpaceTime& operator=(const SpaceTime&) = delete;
    ~SpaceTime();

    uint32_t width() const { return width_; }
    uint32_t rows() const { return rows_; }
    // How many generations have been recorded, which is not the same as how
    // many are still visible once the ring has wrapped.
    uint64_t captured() const { return captured_; }

    // Everything back to state 0 and the next row back to the top.
    void clear();

    // Copies row 0 of a 1D grid's texture into the next row of the history.
    // The texture must be the same width and GL_R8UI, which a u8 1D grid is.
    void capture(unsigned int gridTexture);

    // Draws the history into `viewport`, oldest row at the top. `zoom` is
    // pixels per cell. Two draws where the ring has wrapped, because the seam
    // is a discontinuity in the texture's rows and not in what is shown.
    void draw(Renderer2D& renderer, const Rect& viewport, int frameWidth, int frameHeight,
              unsigned int states, double zoom) const;

private:
    SpaceTime() = default;
    void release();

    unsigned int texture_ = 0;
    uint32_t width_ = 0, rows_ = 0;
    uint32_t next_ = 0;        // where the next generation is written
    uint64_t captured_ = 0;
};

}  // namespace aether::render
