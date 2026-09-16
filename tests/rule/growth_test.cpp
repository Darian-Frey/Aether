#include "rule/growth.hpp"
#include "rule/ir.hpp"
#include "sim/cpu_step.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using namespace aether;
using rule::GrowthForm;
using rule::GrowthSpec;

namespace {

// Evaluates a lowered growth function the way the engine will: through the
// oracle's arena walk, not a second one written for the test (AV-017).
float at(const GrowthSpec& g, float conv) {
    const rule::Expression e = rule::growthExpression(g);
    const auto types = rule::expressionTypes(e, 0, 0, /*selfIsFloat=*/true);
    std::vector<sim::ExprValue> scratch;
    return sim::evalGrowth(e, types, conv, scratch);
}

}  // namespace

TEST_CASE("growth forms round-trip through their names", "[growth]") {
    for (auto f : {GrowthForm::Rectangular, GrowthForm::Polynomial}) {
        CHECK(rule::parseGrowthForm(rule::toString(f)) == f);
    }
    CHECK_FALSE(rule::parseGrowthForm("gaussian").has_value());   // needs exp; see growth.hpp
    CHECK_FALSE(rule::parseGrowthForm("").has_value());
}

TEST_CASE("a growth function must have a positive width and a reachable peak", "[growth]") {
    CHECK(rule::problems({GrowthForm::Polynomial, 0.15f, 0.015f}).empty());
    // Zero sigma would divide by zero, which SPEC §6 defines as zero, turning
    // the function into the constant 1 rather than failing.
    CHECK_FALSE(rule::problems({GrowthForm::Polynomial, 0.15f, 0.0f}).empty());
    CHECK_FALSE(rule::problems({GrowthForm::Polynomial, 0.15f, -0.01f}).empty());
    // A normalised kernel over cells in [0, 1] convolves to [0, 1].
    CHECK_FALSE(rule::problems({GrowthForm::Polynomial, 1.5f, 0.01f}).empty());
}

TEST_CASE("a lowered growth function type-checks as a float function", "[growth]") {
    for (auto f : {GrowthForm::Rectangular, GrowthForm::Polynomial}) {
        const rule::Expression e = rule::growthExpression({f, 0.15f, 0.015f});
        const auto types = rule::expressionTypes(e, 0, 0, true);
        REQUIRE(types.size() == e.nodes.size());
        CHECK(types.back() == rule::ExprType::Float);
        for (auto t : types) CHECK(t != rule::ExprType::Invalid);

        // Without the growth context Self is an own state, the arithmetic
        // mixes int with float, and the whole thing is ill-typed (BUG-010).
        const auto asDiscrete = rule::expressionTypes(e, 0, 0, false);
        CHECK(asDiscrete.back() != rule::ExprType::Float);
    }
}

TEST_CASE("rectangular growth is 1 inside the band and -1 outside", "[growth]") {
    const GrowthSpec g{GrowthForm::Rectangular, 0.30f, 0.05f};
    CHECK(at(g, 0.30f) == 1.0f);
    CHECK(at(g, 0.26f) == 1.0f);
    CHECK(at(g, 0.34f) == 1.0f);
    CHECK(at(g, 0.25f) == 1.0f);    // the band is closed at both ends
    CHECK(at(g, 0.35f) == 1.0f);
    CHECK(at(g, 0.20f) == -1.0f);
    CHECK(at(g, 0.40f) == -1.0f);
    CHECK(at(g, 0.0f) == -1.0f);
    CHECK(at(g, 1.0f) == -1.0f);
}

TEST_CASE("polynomial growth peaks at mu and falls to -1 away from it", "[growth]") {
    using Catch::Matchers::WithinAbs;
    const GrowthSpec g{GrowthForm::Polynomial, 0.15f, 0.015f};

    CHECK_THAT(at(g, 0.15f), WithinAbs(1.0f, 1e-5));          // the peak
    CHECK_THAT(at(g, 0.15f + 0.045f), WithinAbs(-1.0f, 1e-5));  // 3 sigma out, where the quartic hits zero
    CHECK_THAT(at(g, 0.15f - 0.045f), WithinAbs(-1.0f, 1e-5));
    CHECK_THAT(at(g, 0.0f), WithinAbs(-1.0f, 1e-5));
    CHECK_THAT(at(g, 1.0f), WithinAbs(-1.0f, 1e-5));

    // Symmetric about mu, and never outside [-1, 1] anywhere in the range a
    // convolution can produce.
    for (int i = 0; i <= 1000; ++i) {
        const float u = static_cast<float>(i) / 1000.0f;
        const float v = at(g, u);
        INFO("u = " << u << " gives " << v);
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
        CHECK_THAT(at(g, 0.15f + (u - 0.15f)), WithinAbs(at(g, 0.15f - (u - 0.15f)), 1e-5));
    }

    // Monotone falling as it leaves the peak, which is what makes it a growth
    // function rather than a band: rectangular has no gradient to climb.
    float previous = at(g, 0.15f);
    for (int i = 1; i <= 30; ++i) {
        const float v = at(g, 0.15f + static_cast<float>(i) * 0.0015f);
        INFO("step " << i);
        CHECK(v <= previous);
        previous = v;
    }
}
