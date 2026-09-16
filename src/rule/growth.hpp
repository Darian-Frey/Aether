// Named growth functions, lowered to the expression form (F-006, Phase 5).
//
// Lenia and SmoothLife configurations are published as a kernel shell and two
// numbers — a centre and a width — so the front ends take them that way and
// lower them here. Backends see an ordinary Expression and know nothing about
// the names, which is the same arrangement `rule/decay` has for ageing tails:
// a front-end desugaring, not a concept the engine carries.
//
// The set is closed on purpose. Lenia's third growth function is a Gaussian,
// which needs `exp`; SPEC §6 forbids relying on built-ins whose precision the
// driver decides, and adding one would make AV-015 worse in the phase that
// already expects to narrow the determinism claim for f32.

#pragma once

#include "rule/ir.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace aether::rule {

enum class GrowthForm : uint8_t {
    Rectangular,   // 1 inside [mu - sigma, mu + sigma], -1 outside
    Polynomial,    // 2 * max(0, 1 - (u - mu)^2 / (9 sigma^2))^4 - 1
};

std::string_view          toString(GrowthForm);
std::optional<GrowthForm> parseGrowthForm(std::string_view);

struct GrowthSpec {
    GrowthForm form  = GrowthForm::Polynomial;
    float      mu    = 0.15f;    // where growth peaks
    float      sigma = 0.015f;   // how wide the peak is

    bool operator==(const GrowthSpec&) const = default;
};

// Empty when well-formed. `sigma` must be positive: a zero width divides by
// zero, which both paths define as zero (SPEC §6) and which would silently
// turn a growth function into the constant 1 rather than failing.
std::vector<std::string> problems(const GrowthSpec&);

// Self is the convolution result. The value is in [-1, 1] for every input.
Expression growthExpression(const GrowthSpec&);

}  // namespace aether::rule
