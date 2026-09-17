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

Palette Palette::continuousRamp() {
    Palette p;
    // Empty is the quiescent colour the rest of the interface uses; full is
    // the live one. Between them the ramp lifts through a cool mid-tone, so
    // that a thin field is visible without a dense one washing out.
    for (uint16_t i = 0; i < 256; ++i) {
        const double t = static_cast<double>(i) / 255.0;
        const double h = 215.0 - 55.0 * t;            // deep blue toward cyan
        const double sat = 0.70 * (1.0 - t * t);      // desaturating as it brightens
        const double val = 0.08 + 0.92 * std::pow(t, 0.75);
        Rgba c = hsv(h, sat, val);
        c.a = static_cast<uint8_t>(std::lround(255.0 * t));   // opacity for the 3D view
        p.entries[i] = c;
    }
    return p;
}

Palette Palette::defaultFor(uint16_t states, std::optional<uint16_t> decayFrom) {
    Palette p;
    p.entries[0] = {14, 16, 20, 0};   // alpha is opacity in the 3D view: quiescent is invisible
    p.entries[1] = {236, 240, 238, 255};

    // Live states the rule uses in its own right.
    const uint16_t live = decayFrom.value_or(states);
    for (uint16_t s = 2; s < 256; ++s) {
        const uint16_t span = live > 2 ? live - 2 : 254;
        const double h = 200.0 + 300.0 * static_cast<double>((s - 2) % span) / span;
        p.entries[s] = hsv(std::fmod(h, 360.0), 0.75, 0.85);
    }
    if (!decayFrom || *decayFrom >= states) return p;

    // The ageing tail, cooling from the colour of state 1.
    const uint16_t tail = static_cast<uint16_t>(states - *decayFrom);
    const Rgba from = p.entries[1];
    for (uint16_t i = 0; i < tail; ++i) {
        const double t = static_cast<double>(i + 1) / (tail + 1);
        auto fade = [&](uint8_t c) { return static_cast<uint8_t>(std::lround(c * (1.0 - 0.82 * t))); };
        p.entries[static_cast<size_t>(*decayFrom + i)] =
            {fade(from.r), fade(from.g), fade(from.b), static_cast<uint8_t>(std::lround(255.0 * (1.0 - t)))};
    }
    return p;
}

}  // namespace aether::render
