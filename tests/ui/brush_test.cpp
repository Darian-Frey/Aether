#include "ui/brush.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::ui;

TEST_CASE("radius 0 is a single cell; radius 1 is a plus with corners", "[brush]") {
    CHECK(brushSpans(5, 5, 0, 10, 10) == std::vector<Span>{{5, 5, 5}});
    // (r+0.5)^2 = 2.25: dy=±1 gives half = floor(sqrt(1.25)) = 1, so 3 wide.
    CHECK(brushSpans(5, 5, 1, 10, 10) == std::vector<Span>{{4, 6, 4}, {4, 6, 5}, {4, 6, 6}});
}

TEST_CASE("larger brushes are round and clip to the grid", "[brush]") {
    const auto spans = brushSpans(0, 0, 3, 10, 10);
    // Only rows 0..3 survive clipping; row 0 is the widest.
    REQUIRE(spans.size() == 4);
    CHECK(spans[0] == Span{0, 3, 0});
    CHECK(spans[3].x1 < spans[0].x1);
    for (const Span& s : spans) { CHECK(s.x0 == 0); CHECK(s.y <= 3); }

    const auto off = brushSpans(-5, -5, 1, 10, 10);
    CHECK(off.empty());

    const auto edge = brushSpans(9, 9, 2, 10, 10);
    for (const Span& s : edge) { CHECK(s.x1 <= 9); CHECK(s.y <= 9); }
    CHECK_FALSE(edge.empty());
}

TEST_CASE("stroke points include both ends and step no further than asked", "[brush]") {
    const auto pts = strokePoints(0, 0, 10, 4, 2);
    REQUIRE(pts.size() >= 6);
    CHECK(pts.front() == std::pair{0, 0});
    CHECK(pts.back() == std::pair{10, 4});
    for (size_t i = 1; i < pts.size(); ++i) {
        CHECK(std::abs(pts[i].first - pts[i - 1].first) <= 2);
        CHECK(std::abs(pts[i].second - pts[i - 1].second) <= 2);
    }
    CHECK(strokePoints(3, 3, 3, 3, 5) == std::vector<std::pair<int, int>>{{3, 3}, {3, 3}});
}

TEST_CASE("hex brush is a hexagon in axial coordinates", "[brush][hex]") {
    // Radius 1: seven cells — the centre and its six axial neighbours.
    CHECK(brushSpans(5, 5, 1, 10, 10, true) == std::vector<Span>{{5, 6, 4}, {4, 6, 5}, {4, 5, 6}});
    size_t cells = 0;
    for (const Span& s : brushSpans(10, 10, 3, 30, 30, true)) cells += s.x1 - s.x0 + 1;
    CHECK(cells == 1 + 3 * 3 * 4);   // 1 + 3r(r+1) = 37
    CHECK(brushSpans(5, 5, 0, 10, 10, true) == std::vector<Span>{{5, 5, 5}});
}
