#include "ui/brush.hpp"

#include <algorithm>
#include <cmath>

namespace aether::ui {

std::vector<Span> brushSpans(int cx, int cy, int radius, uint32_t width, uint32_t height, bool hex) {
    std::vector<Span> out;
    const int r = std::max(0, radius);
    const double rr = (r + 0.5) * (r + 0.5);
    for (int dy = -r; dy <= r; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= static_cast<int>(height)) continue;
        int lo, hi;
        if (hex) {
            // max(|dq|, |dy|, |dq + dy|) <= r  <=>  dq in [max(-r, -r - dy), min(r, r - dy)]
            lo = std::max(-r, -r - dy);
            hi = std::min(r, r - dy);
        } else {
            const int half = static_cast<int>(std::floor(std::sqrt(rr - dy * dy)));
            lo = -half;
            hi = half;
        }
        const int x0 = std::max(0, cx + lo);
        const int x1 = std::min(static_cast<int>(width) - 1, cx + hi);
        if (x0 > x1) continue;
        out.push_back({static_cast<uint32_t>(x0), static_cast<uint32_t>(x1), static_cast<uint32_t>(y)});
    }
    return out;
}

std::vector<std::pair<int, int>> strokePoints(int x0, int y0, int x1, int y1, int step) {
    std::vector<std::pair<int, int>> out;
    const int dist = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    const int n = std::max(1, (dist + std::max(1, step) - 1) / std::max(1, step));
    for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        out.push_back({static_cast<int>(std::lround(x0 + (x1 - x0) * t)),
                       static_cast<int>(std::lround(y0 + (y1 - y0) * t))});
    }
    return out;
}

}  // namespace aether::ui
