#include "rule/kernel.hpp"
#include "rule/neighbourhood.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <numeric>

using namespace aether::rule;
using Catch::Approx;

namespace {

RuleIR continuous(NeighbourhoodType type, uint8_t radius, Kernel::Shape shape,
                  std::vector<float> profile, uint8_t dimensions = 2) {
    RuleIR ir;
    ir.dimensions = dimensions;
    ir.cell_type = aether::core::CellType::F32;
    ir.kind = Kind::Continuous;
    ir.neighbourhood = {type, radius};
    Kernel k;
    k.shape = shape;
    k.profile = std::move(profile);
    k.growth.nodes = {{ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
    ir.transition = k;
    return ir;
}

ResolvedKernel resolved(const RuleIR& ir) {
    auto r = resolveKernel(ir);
    if (const auto* e = std::get_if<std::string>(&r)) FAIL(*e);
    return std::get<ResolvedKernel>(std::move(r));
}

float total(const ResolvedKernel& k) {
    return std::accumulate(k.weights.begin(), k.weights.end(), k.self);
}

}  // namespace

TEST_CASE("kernel weights are normalised, centre included", "[kernel]") {
    // A flat profile over a Moore r=1 neighbourhood: nine sites, a ninth each.
    const auto k = resolved(continuous(NeighbourhoodType::Moore, 1, Kernel::Shape::Radial, {1.0f}));
    CHECK(k.weights.size() == 8);
    CHECK(total(k) == Approx(1.0f));
    CHECK(k.self == Approx(1.0f / 9.0f));
    for (float w : k.weights) CHECK(w == Approx(1.0f / 9.0f));

    // The scale of the profile cannot matter once it is normalised, which is
    // what lets a growth mu mean the same thing against any kernel.
    const auto scaled = resolved(continuous(NeighbourhoodType::Moore, 1, Kernel::Shape::Radial, {1000.0f}));
    CHECK(scaled.self == Approx(k.self));
}

TEST_CASE("a radial profile is sampled by distance, not by index", "[kernel]") {
    // Centre 1, rim 0, over radius 2: weight falls with Euclidean distance.
    const auto k = resolved(continuous(NeighbourhoodType::Moore, 2, Kernel::Shape::Radial,
                                       {1.0f, 0.5f, 0.0f}));
    const auto offsets = neighbourOffsets(2, {NeighbourhoodType::Moore, 2});
    REQUIRE(k.weights.size() == offsets.size());

    auto weightAt = [&](int dx, int dy) {
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (offsets[i].dx == dx && offsets[i].dy == dy) return k.weights[i];
        }
        FAIL("no such offset");
        return 0.0f;
    };
    // Distance 1 is halfway to the rim, so it takes the middle sample.
    CHECK(weightAt(1, 0) == Approx(weightAt(0, 1)));
    CHECK(weightAt(1, 0) > weightAt(2, 0));
    // A corner at distance 2·√2 is past the rim and takes the rim's value.
    CHECK(weightAt(2, 2) == Approx(0.0f));
    CHECK(weightAt(2, 0) == Approx(0.0f));
    // Equal distances get equal weights, so the kernel is isotropic.
    CHECK(weightAt(1, 1) == Approx(weightAt(-1, -1)));
    CHECK(weightAt(1, 1) == Approx(weightAt(1, -1)));
}

TEST_CASE("an explicit profile is a box the centre sits in the middle of", "[kernel]") {
    // 3x3 row-major, x fastest: only the site to the right carries weight.
    std::vector<float> box(9, 0.0f);
    box[5] = 1.0f;                      // (dx=+1, dy=0)
    const auto k = resolved(continuous(NeighbourhoodType::Moore, 1, Kernel::Shape::Explicit, box));
    const auto offsets = neighbourOffsets(2, {NeighbourhoodType::Moore, 1});
    CHECK(k.self == Approx(0.0f));
    for (size_t i = 0; i < offsets.size(); ++i) {
        const bool right = offsets[i].dx == 1 && offsets[i].dy == 0;
        INFO("offset " << int(offsets[i].dx) << "," << int(offsets[i].dy));
        CHECK(k.weights[i] == Approx(right ? 1.0f : 0.0f));
    }

    // The centre of the box is the cell itself, which is never an offset.
    std::vector<float> centre(9, 0.0f);
    centre[4] = 1.0f;
    const auto c = resolved(continuous(NeighbourhoodType::Moore, 1, Kernel::Shape::Explicit, centre));
    CHECK(c.self == Approx(1.0f));
    for (float w : c.weights) CHECK(w == Approx(0.0f));
}

TEST_CASE("a kernel that cannot be resolved says so rather than guessing", "[kernel]") {
    auto errorOf = [](const RuleIR& ir) {
        auto r = resolveKernel(ir);
        return std::holds_alternative<std::string>(r) ? std::get<std::string>(r) : std::string("<resolved>");
    };
    // Nothing to normalise against.
    CHECK(errorOf(continuous(NeighbourhoodType::Moore, 1, Kernel::Shape::Radial, {0.0f}))
              .find("sum to") != std::string::npos);
    // A hexagonal neighbourhood is not box-shaped, so an explicit profile has
    // no honest mapping onto it; a radial one does.
    CHECK(errorOf(continuous(NeighbourhoodType::Hexagonal, 1, Kernel::Shape::Explicit, std::vector<float>(9, 1.0f)))
              .find("square lattice") != std::string::npos);
    CHECK(errorOf(continuous(NeighbourhoodType::Hexagonal, 2, Kernel::Shape::Radial, {1.0f, 0.5f, 0.0f}))
          == "<resolved>");
}

TEST_CASE("a hexagonal radial kernel measures distance on the lattice", "[kernel]") {
    // Axial storage puts the six radius-1 neighbours at differing Euclidean
    // lengths in stored coordinates; on the lattice they are all one step out,
    // so an isotropic profile must give them equal weight.
    const auto k = resolved(continuous(NeighbourhoodType::Hexagonal, 2, Kernel::Shape::Radial,
                                       {1.0f, 0.5f, 0.0f}));
    const auto offsets = neighbourOffsets(2, {NeighbourhoodType::Hexagonal, 2});
    REQUIRE(k.weights.size() == offsets.size());
    std::vector<float> ring;
    for (size_t i = 0; i < offsets.size(); ++i) {
        const int q = offsets[i].dx, r = offsets[i].dy;
        if ((std::abs(q) + std::abs(q + r) + std::abs(r)) / 2 == 1) ring.push_back(k.weights[i]);
    }
    REQUIRE(ring.size() == 6);
    for (float w : ring) CHECK(w == Approx(ring.front()));
}
