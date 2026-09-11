#include "render/palette.hpp"

#include <cmath>

namespace aether::render {

namespace {

Rgba hsv(double h, double s, double v) {
    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double r = 0, g = 0, b = 0;
    if      (h < 60)  { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    auto to8 = [](double u) { return static_cast<uint8_t>(std::lround((u) * 255.0)); };
    return {to8(r + m), to8(g + m), to8(b + m), 255};
}

}  // namespace

Palette Palette::defaultFor(uint16_t states) {
    Palette p;
    p.entries[0] = {14, 16, 20, 255};
    p.entries[1] = {236, 240, 238, 255};
    for (uint16_t s = 2; s < 256; ++s) {
        // Spread the remaining states evenly round the circle; states past
        // the rule's count still get a colour so a stale cell is visible.
        const uint16_t span = states > 2 ? states - 2 : 254;
        const double h = 200.0 + 300.0 * static_cast<double>((s - 2) % span) / span;
        p.entries[s] = hsv(std::fmod(h, 360.0), 0.75, 0.85);
    }
    return p;
}

}  // namespace aether::render
