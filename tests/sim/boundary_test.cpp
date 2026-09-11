#include "sim/boundary.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::sim;
using aether::rule::Boundary;

TEST_CASE("wrap is toroidal for any distance out of range", "[boundary]") {
    CHECK(resolve(0, 10, Boundary::Wrap) == 0);
    CHECK(resolve(9, 10, Boundary::Wrap) == 9);
    CHECK(resolve(-1, 10, Boundary::Wrap) == 9);
    CHECK(resolve(10, 10, Boundary::Wrap) == 0);
    CHECK(resolve(-11, 10, Boundary::Wrap) == 9);
    CHECK(resolve(23, 10, Boundary::Wrap) == 3);
    CHECK(resolve(-1, 1, Boundary::Wrap) == 0);
}

TEST_CASE("zero reads as absent outside the grid", "[boundary]") {
    CHECK(resolve(0, 10, Boundary::Zero) == 0);
    CHECK(resolve(9, 10, Boundary::Zero) == 9);
    CHECK_FALSE(resolve(-1, 10, Boundary::Zero).has_value());
    CHECK_FALSE(resolve(10, 10, Boundary::Zero).has_value());
}

TEST_CASE("mirror reflects about the edge cell's centre", "[boundary]") {
    CHECK(resolve(-1, 10, Boundary::Mirror) == 1);
    CHECK(resolve(-2, 10, Boundary::Mirror) == 2);
    CHECK(resolve(10, 10, Boundary::Mirror) == 8);
    CHECK(resolve(11, 10, Boundary::Mirror) == 7);
    // Beyond one reflection it keeps folding.
    CHECK(resolve(-9, 10, Boundary::Mirror) == 9);
    CHECK(resolve(-10, 10, Boundary::Mirror) == 8);
    CHECK(resolve(18, 10, Boundary::Mirror) == 0);
    CHECK(resolve(19, 10, Boundary::Mirror) == 1);
    // Degenerate extents.
    CHECK(resolve(-1, 1, Boundary::Mirror) == 0);
    CHECK(resolve(5, 1, Boundary::Mirror) == 0);
    CHECK(resolve(-1, 2, Boundary::Mirror) == 1);
    CHECK(resolve(2, 2, Boundary::Mirror) == 0);
    CHECK(resolve(3, 2, Boundary::Mirror) == 1);
}
