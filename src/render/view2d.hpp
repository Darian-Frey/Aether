// 2D camera (F-018): a zoom and the cell coordinate under the viewport
// centre. Pure arithmetic, shared by the renderer (to place pixels) and the
// canvas (to place brush strokes), so the two agree by construction.

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <utility>

namespace aether::render {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;   // top-left origin, pixels
};

enum class Lattice { Square, Hex };

// Hex lattices are pointy-topped, axial (q, r) = (x, y). At zoom z a hex is
// z pixels wide; its centre sits at z * (q + r/2, r * sqrt(3)/2) from the
// origin cell's centre. These two matrices map axial <-> "cell space", the
// square-lattice frame the rest of the view works in.
constexpr double kHexA = 0.5;                 // x += A * r
constexpr double kHexB = 0.86602540378443865; // y  = B * r  (sqrt(3)/2)

struct View2D {
    double  zoom     = 4.0;   // screen pixels per cell
    double  centre_x = 0.0;   // cell coordinate at the viewport centre
    double  centre_y = 0.0;
    Lattice lattice  = Lattice::Square;

    // Axial -> cell-space and back (identity on a square lattice).
    std::pair<double, double> toCellSpace(double q, double r) const {
        if (lattice == Lattice::Square) return {q, r};
        return {q + kHexA * r, kHexB * r};
    }
    std::pair<double, double> fromCellSpace(double u, double v) const {
        if (lattice == Lattice::Square) return {u, v};
        const double r = v / kHexB;
        return {u - kHexA * r, r};
    }

    // Nearest hex to a fractional axial coordinate (cube rounding).
    static std::pair<int, int> hexRound(double q, double r) {
        const double x = q, z = r, y = -x - z;
        double rx = std::round(x), ry = std::round(y), rz = std::round(z);
        const double dx = std::abs(rx - x), dy = std::abs(ry - y), dz = std::abs(rz - z);
        if (dx > dy && dx > dz) rx = -ry - rz;
        else if (dy > dz)       ry = -rx - rz;
        else                    rz = -rx - ry;
        (void)ry;
        return {static_cast<int>(rx), static_cast<int>(rz)};
    }

    // Cell coordinate at the viewport's top-left pixel.
    std::pair<double, double> origin(const Rect& vp) const {
        return {centre_x - vp.w * 0.5 / zoom, centre_y - vp.h * 0.5 / zoom};
    }

    // Origin snapped so that at integer zoom every cell edge lands on a
    // pixel edge (SPEC §13: pixel-exact at 1:1). Square lattices only; a
    // hex tiling has no pixel-exact zoom.
    std::pair<double, double> snappedOrigin(const Rect& vp) const {
        auto [ox, oy] = origin(vp);
        if (lattice == Lattice::Square && std::abs(zoom - std::round(zoom)) < 1e-9) {
            ox = std::round(ox * zoom) / zoom;
            oy = std::round(oy * zoom) / zoom;
        }
        return {ox, oy};
    }

    // Screen pixel (absolute) -> fractional cell coordinate.
    std::pair<double, double> screenToCell(double sx, double sy, const Rect& vp) const {
        auto [ox, oy] = snappedOrigin(vp);
        return {ox + (sx - vp.x) / zoom, oy + (sy - vp.y) / zoom};
    }

    // Screen pixel -> integer cell (axial on a hex lattice), or nullopt if
    // outside the grid.
    std::optional<std::pair<int, int>> cellAt(double sx, double sy, const Rect& vp,
                                              unsigned width, unsigned height) const {
        auto [u, v] = screenToCell(sx, sy, vp);
        int ix, iy;
        if (lattice == Lattice::Square) {
            if (u < 0 || v < 0 || u >= width || v >= height) return std::nullopt;
            ix = static_cast<int>(std::floor(u));
            iy = static_cast<int>(std::floor(v));
        } else {
            // Cell-space (u, v) is measured from the origin cell's centre.
            auto [q, r] = fromCellSpace(u - 0.5, v - 0.5);
            std::tie(ix, iy) = hexRound(q, r);
        }
        if (ix < 0 || iy < 0 || ix >= static_cast<int>(width) || iy >= static_cast<int>(height)) return std::nullopt;
        return std::pair{ix, iy};
    }

    // Cell coordinate -> screen pixel (absolute). On a hex lattice (cx, cy)
    // is axial and the result is that hex's centre when cx, cy are integers
    // plus 0.5 in cell space, matching the square convention.
    std::pair<double, double> cellToScreen(double cx, double cy, const Rect& vp) const {
        auto [ox, oy] = snappedOrigin(vp);
        auto [u, v] = lattice == Lattice::Square ? std::pair{cx, cy}
                                                 : [&] { auto p = toCellSpace(cx, cy); return std::pair{p.first + 0.5, p.second + 0.5}; }();
        return {vp.x + (u - ox) * zoom, vp.y + (v - oy) * zoom};
    }

    // Cell-space bounding box of the grid: [0, w) x [0, h) on a square
    // lattice; the rhombus's box on a hex one.
    std::pair<double, double> extent(unsigned width, unsigned height) const {
        if (lattice == Lattice::Square) return {width, height};
        return {width + kHexA * (height - 1) + 1.0, kHexB * (height - 1) + 1.0};
    }

    // Zoom so the whole grid fits the viewport, centred. `pixelExact` rounds
    // down to a whole number of pixels per cell, which is what SPEC §13
    // asks for and what leaves margins; passing false fills the viewport
    // instead and gives up the exactness.
    void fit(unsigned width, unsigned height, const Rect& vp, bool pixelExact = true) {
        if (vp.w <= 0 || vp.h <= 0 || width == 0 || height == 0) return;   // nothing to fit into yet
        auto [ew, eh] = extent(width, height);
        const double z = std::min(static_cast<double>(vp.w) / ew, static_cast<double>(vp.h) / eh);
        zoom = (pixelExact && z >= 1.0 && lattice == Lattice::Square) ? std::floor(z) : z;
        centre_x = ew * 0.5;
        centre_y = eh * 0.5;
    }

    // Multiply zoom by `factor`, keeping the cell under screen point
    // (sx, sy) where it is.
    // Uses the unsnapped transform on both sides so the fixed point is
    // exact; snapping then shifts the result by less than a pixel.
    void zoomAt(double sx, double sy, double factor, const Rect& vp) {
        auto [ox, oy] = origin(vp);
        const double cx = ox + (sx - vp.x) / zoom;
        const double cy = oy + (sy - vp.y) / zoom;
        zoom = std::clamp(zoom * factor, 1.0 / 64.0, 256.0);
        centre_x = cx - (sx - vp.x) / zoom + vp.w * 0.5 / zoom;
        centre_y = cy - (sy - vp.y) / zoom + vp.h * 0.5 / zoom;
    }

    // Move the view by a screen-pixel delta (drag).
    void pan(double dx, double dy) {
        centre_x -= dx / zoom;
        centre_y -= dy / zoom;
    }
};

}  // namespace aether::render
