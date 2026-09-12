// Orbit camera for the 3D view (F-019). Pure arithmetic, like View2D: the
// renderer places rays with it and the canvas picks cells with it, so the
// two agree by construction.
//
// World units are cells: the grid occupies [0, W) x [0, H) x [0, D). +y is
// up on screen, so a 2D grid seen in 3D appears vertically flipped relative
// to the 2D view; the 3D view is its own presentation.

#pragma once

#include "render/view2d.hpp"   // Rect

#include <array>
#include <cmath>
#include <optional>

namespace aether::render {

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    double dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    Vec3 normalised() const { const double l = length(); return l > 0 ? *this * (1.0 / l) : *this; }
};

struct Ray {
    Vec3 origin;
    Vec3 dir;   // unit
};

struct Orbit {
    double yaw      = 0.6;    // radians about +y
    double pitch    = 0.45;   // radians above the xz plane, clamped to ±89°
    double distance = 10.0;   // from target, in cells
    double fovY     = 0.9;    // radians
    Vec3   target;            // usually the grid centre

    Vec3 position() const {
        const double cp = std::cos(pitch);
        return target + Vec3{cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)} * distance;
    }

    // Camera basis: forward, right, up (all unit).
    std::array<Vec3, 3> basis() const {
        const Vec3 forward = (target - position()).normalised();
        Vec3 right = forward.cross(Vec3{0, 1, 0}).normalised();
        if (right.length() < 1e-9) right = Vec3{1, 0, 0};   // looking straight down
        const Vec3 up = right.cross(forward);
        return {forward, right, up};
    }

    // Ray through screen pixel (sx, sy) of the viewport.
    Ray rayFor(double sx, double sy, const Rect& vp) const {
        const auto [forward, right, up] = basis();
        const double ndcX = ((sx - vp.x) / vp.w) * 2.0 - 1.0;
        const double ndcY = 1.0 - ((sy - vp.y) / vp.h) * 2.0;
        const double t = std::tan(fovY * 0.5);
        const double aspect = vp.w / vp.h;
        const Vec3 dir = (forward + right * (ndcX * t * aspect) + up * (ndcY * t)).normalised();
        return {position(), dir};
    }

    // Cell hit by the pixel's ray on the axis-aligned slab of thickness one
    // at `index` along `axis` (0 = x, 1 = y, 2 = z), or nullopt. This is
    // how slice painting finds its cell (F-011).
    std::optional<std::array<int, 3>> pickOnSlab(double sx, double sy, const Rect& vp, int axis, int index,
                                                 unsigned w, unsigned h, unsigned d) const {
        const Ray ray = rayFor(sx, sy, vp);
        const double o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
        const double dd[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
        const unsigned ext[3] = {w, h, d};
        // Plane through the slab's middle; a ray parallel to it misses.
        if (std::abs(dd[axis]) < 1e-12) return std::nullopt;
        const double t = (index + 0.5 - o[axis]) / dd[axis];
        if (t < 0) return std::nullopt;
        std::array<int, 3> cell{};
        for (size_t a = 0; a < 3; ++a) {
            if (static_cast<int>(a) == axis) { cell[a] = index; continue; }
            const double p = o[a] + dd[a] * t;
            if (p < 0 || p >= ext[a]) return std::nullopt;
            cell[a] = static_cast<int>(std::floor(p));
        }
        return cell;
    }

    // Look at the grid's centre from far enough that it all fits.
    void fit(unsigned w, unsigned h, unsigned d) {
        target = {w * 0.5, h * 0.5, d * 0.5};
        const double radius = 0.5 * std::sqrt(double(w) * w + double(h) * h + double(d) * d);
        distance = radius / std::sin(fovY * 0.5) * 1.05;
    }

    void rotate(double dyaw, double dpitch) {
        yaw += dyaw;
        pitch = std::clamp(pitch + dpitch, -1.553, 1.553);
    }

    void zoom(double factor) {
        distance = std::clamp(distance * factor, 0.5, 100000.0);
    }
};

}  // namespace aether::render
