#include "rule/kernel.hpp"

#include "rule/neighbourhood.hpp"

#include <cmath>
#include <format>

namespace aether::rule {

namespace {

// Hexagonal storage is axial, so a step in q and a step in r are not at right
// angles and Euclidean length in stored coordinates is not distance on the
// lattice. This is the standard cube distance, which is what "radius" already
// means for a hex neighbourhood (SPEC §3).
float hexDistance(const Offset& o) {
    const int q = o.dx, r = o.dy;
    return static_cast<float>(std::abs(q) + std::abs(q + r) + std::abs(r)) / 2.0f;
}

float euclideanDistance(const Offset& o) {
    const float x = o.dx, y = o.dy, z = o.dz;
    return std::sqrt(x * x + y * y + z * z);
}

// Samples the profile at a normalised radius. profile[0] is the centre and
// profile[n-1] the rim; anything beyond the rim — a Moore corner sits at
// r·√2 — takes the rim's value, which for an annulus is the zero it tails to.
float sampleRadial(const std::vector<float>& profile, float distance, uint8_t radius) {
    if (profile.size() == 1) return profile[0];
    const float u = radius == 0 ? 0.0f : std::min(1.0f, distance / static_cast<float>(radius));
    const float position = u * static_cast<float>(profile.size() - 1);
    const auto lower = static_cast<size_t>(position);
    if (lower + 1 >= profile.size()) return profile.back();
    const float t = position - static_cast<float>(lower);
    return profile[lower] * (1.0f - t) + profile[lower + 1] * t;
}

// Row-major index into the (2r+1)^d box an explicit profile describes, x
// fastest, matching the grid's own ordering.
size_t boxIndex(const Offset& o, uint8_t radius, uint8_t dimensions) {
    const size_t side = 2u * radius + 1u;
    const size_t x = static_cast<size_t>(o.dx + radius);
    const size_t y = dimensions >= 2 ? static_cast<size_t>(o.dy + radius) : 0;
    const size_t z = dimensions >= 3 ? static_cast<size_t>(o.dz + radius) : 0;
    return (z * side + y) * side + x;
}

}  // namespace

std::variant<ResolvedKernel, std::string> resolveKernel(const RuleIR& ir) {
    const auto* kernel = std::get_if<Kernel>(&ir.transition);
    if (!kernel) return std::string("the rule has no kernel");
    if (kernel->profile.empty()) return std::string("kernel has no coefficients");

    const bool hex = ir.neighbourhood.type == NeighbourhoodType::Hexagonal;
    if (kernel->shape == Kernel::Shape::Explicit && hex) {
        // An explicit profile is a box of weights, and a hex neighbourhood is
        // not box-shaped: there is no honest way to line the two up. A radial
        // profile works on hex, so this is a limitation of the shape.
        return std::string("an explicit kernel needs a square lattice; use a radial profile on hexagonal");
    }

    const auto offsets = neighbourOffsets(ir.dimensions, ir.neighbourhood);
    ResolvedKernel out;
    out.weights.reserve(offsets.size());

    if (kernel->shape == Kernel::Shape::Explicit) {
        const Offset centre{0, 0, 0};
        out.self = kernel->profile[boxIndex(centre, ir.neighbourhood.radius, ir.dimensions)];
        for (const Offset& o : offsets) {
            const size_t i = boxIndex(o, ir.neighbourhood.radius, ir.dimensions);
            if (i >= kernel->profile.size()) {
                return std::format("offset ({},{},{}) falls outside the {} weights given",
                                   o.dx, o.dy, o.dz, kernel->profile.size());
            }
            out.weights.push_back(kernel->profile[i]);
        }
    } else {
        out.self = kernel->profile.front();
        for (const Offset& o : offsets) {
            const float d = hex ? hexDistance(o) : euclideanDistance(o);
            out.weights.push_back(sampleRadial(kernel->profile, d, ir.neighbourhood.radius));
        }
    }

    // Normalised so the convolution of cells in [0, 1] lands in [0, 1]. Done
    // here rather than asked of the author, so that `mu` compares across
    // kernels and a profile can be written in whatever units suit it.
    double total = out.self;
    for (float w : out.weights) total += w;
    if (!(total > 0.0)) {
        return std::format("kernel weights sum to {}; they must sum to something positive to normalise", total);
    }
    const auto scale = static_cast<float>(1.0 / total);
    out.self *= scale;
    for (float& w : out.weights) w *= scale;
    return out;
}

}  // namespace aether::rule
