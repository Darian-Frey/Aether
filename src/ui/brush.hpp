// Brush geometry (F-011). Pure functions so the canvas's arithmetic is
// testable without a window.

#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace aether::ui {

struct Span {
    uint32_t x0, x1, y;   // inclusive
    bool operator==(const Span&) const = default;
};

// Row spans of a disc of `radius` cells centred on (cx, cy), clipped to
// [0, width) x [0, height). radius 0 is a single cell.
std::vector<Span> brushSpans(int cx, int cy, int radius, uint32_t width, uint32_t height);

// Points along the segment from (x0, y0) to (x1, y1), spaced at most `step`
// cells apart, always including both ends. For dragging a brush.
std::vector<std::pair<int, int>> strokePoints(int x0, int y0, int x1, int y1, int step);

}  // namespace aether::ui
