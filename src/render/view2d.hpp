// 2D camera (F-018): a zoom and the cell coordinate under the viewport
// centre. Pure arithmetic, shared by the renderer (to place pixels) and the
// canvas (to place brush strokes), so the two agree by construction.

#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace aether::render {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;   // top-left origin, pixels
};

struct View2D {
    double zoom     = 4.0;   // screen pixels per cell
    double centre_x = 0.0;   // cell coordinate at the viewport centre
    double centre_y = 0.0;

    // Cell coordinate at the viewport's top-left pixel.
    std::pair<double, double> origin(const Rect& vp) const {
        return {centre_x - vp.w * 0.5 / zoom, centre_y - vp.h * 0.5 / zoom};
    }

    // Origin snapped so that at integer zoom every cell edge lands on a
    // pixel edge (SPEC §13: pixel-exact at 1:1).
    std::pair<double, double> snappedOrigin(const Rect& vp) const {
        auto [ox, oy] = origin(vp);
        if (std::abs(zoom - std::round(zoom)) < 1e-9) {
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

    // Screen pixel -> integer cell, or nullopt if outside the grid.
    std::optional<std::pair<int, int>> cellAt(double sx, double sy, const Rect& vp,
                                              unsigned width, unsigned height) const {
        auto [cx, cy] = screenToCell(sx, sy, vp);
        if (cx < 0 || cy < 0 || cx >= width || cy >= height) return std::nullopt;
        return std::pair{static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(cy))};
    }

    // Cell coordinate -> screen pixel (absolute).
    std::pair<double, double> cellToScreen(double cx, double cy, const Rect& vp) const {
        auto [ox, oy] = snappedOrigin(vp);
        return {vp.x + (cx - ox) * zoom, vp.y + (cy - oy) * zoom};
    }

    // Zoom so the whole grid fits the viewport, centred, at an integer zoom
    // where one fits and a fractional one otherwise (tiny viewports).
    void fit(unsigned width, unsigned height, const Rect& vp) {
        if (vp.w <= 0 || vp.h <= 0 || width == 0 || height == 0) return;   // nothing to fit into yet
        const double z = std::min(static_cast<double>(vp.w) / width, static_cast<double>(vp.h) / height);
        zoom = z >= 1.0 ? std::floor(z) : z;
        centre_x = width * 0.5;
        centre_y = height * 0.5;
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
