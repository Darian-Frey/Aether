#include "render/view2d.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using aether::render::Rect;
using aether::render::View2D;
using Catch::Matchers::WithinAbs;

TEST_CASE("fit centres the grid at the largest integer zoom that fits", "[view]") {
    View2D v;
    v.fit(64, 64, Rect{0, 0, 800, 600});
    CHECK(v.zoom == 9.0);
    CHECK(v.centre_x == 32.0);
    CHECK(v.centre_y == 32.0);
    v.fit(1024, 1024, Rect{0, 0, 512, 512});
    CHECK(v.zoom == 0.5);
}

TEST_CASE("screen <-> cell round-trips through the view", "[view]") {
    View2D v;
    v.zoom = 3.0;
    v.centre_x = 10.25;
    v.centre_y = 7.5;
    const Rect vp{100, 50, 300, 200};
    const auto [sx, sy] = v.cellToScreen(4.0, 9.0, vp);
    const auto [cx, cy] = v.screenToCell(sx, sy, vp);
    CHECK_THAT(cx, WithinAbs(4.0, 1e-9));
    CHECK_THAT(cy, WithinAbs(9.0, 1e-9));
}

TEST_CASE("at integer zoom the origin snaps to a pixel boundary", "[view]") {
    View2D v;
    v.zoom = 1.0;
    v.centre_x = 10.3;
    v.centre_y = 10.7;
    const Rect vp{0, 0, 21, 21};
    const auto [ox, oy] = v.snappedOrigin(vp);
    CHECK(ox == std::round(ox));
    CHECK(oy == std::round(oy));
    // Every pixel maps to exactly one whole cell.
    for (int px = 0; px < 21; ++px) {
        const auto c = v.cellAt(px + 0.5, 0.5, vp, 100, 100);
        REQUIRE(c);
        CHECK(c->first == static_cast<int>(ox) + px);
    }
}

TEST_CASE("cellAt returns nullopt outside the grid", "[view]") {
    View2D v;
    v.fit(8, 8, Rect{0, 0, 80, 80});   // zoom 10, grid fills the viewport exactly
    CHECK(v.cellAt(5, 5, Rect{0, 0, 80, 80}, 8, 8) == std::pair{0, 0});
    CHECK(v.cellAt(79, 79, Rect{0, 0, 80, 80}, 8, 8) == std::pair{7, 7});
    v.zoom = 5.0;   // grid now occupies the middle 40 px
    CHECK_FALSE(v.cellAt(5, 5, Rect{0, 0, 80, 80}, 8, 8).has_value());
    CHECK(v.cellAt(40, 40, Rect{0, 0, 80, 80}, 8, 8) == std::pair{4, 4});
}

TEST_CASE("zoomAt keeps the cell under the cursor fixed", "[view]") {
    View2D v;
    v.zoom = 2.5;   // non-integer on both sides so snapping does not apply
    v.centre_x = 50;
    v.centre_y = 50;
    const Rect vp{0, 0, 400, 300};
    const auto [bx, by] = v.screenToCell(123, 77, vp);
    v.zoomAt(123, 77, 1.5, vp);
    CHECK(v.zoom == 3.75);
    const auto [ax, ay] = v.screenToCell(123, 77, vp);
    CHECK_THAT(ax, WithinAbs(bx, 1e-9));
    CHECK_THAT(ay, WithinAbs(by, 1e-9));

    // At integer zooms snapping may shift the result by under one cell.
    View2D w;
    w.zoom = 2.0;
    w.centre_x = 50.3;
    w.centre_y = 49.7;
    const auto [px, py] = w.screenToCell(123, 77, vp);
    w.zoomAt(123, 77, 2.0, vp);
    const auto [qx, qy] = w.screenToCell(123, 77, vp);
    CHECK_THAT(qx, WithinAbs(px, 1.0 / 2.0));
    CHECK_THAT(qy, WithinAbs(py, 1.0 / 2.0));
}

TEST_CASE("pan moves the view by screen pixels", "[view]") {
    View2D v;
    v.zoom = 4.0;
    v.centre_x = 10;
    v.centre_y = 10;
    v.pan(40, -8);   // dragged right 40 px, up 8 px: content follows the drag
    CHECK(v.centre_x == 0.0);
    CHECK(v.centre_y == 12.0);
}

TEST_CASE("hex lattice: every hex centre maps back to its own cell", "[view][hex]") {
    using aether::render::Lattice;
    View2D v;
    v.lattice = Lattice::Hex;
    v.zoom = 12.0;
    v.centre_x = 8;
    v.centre_y = 6;
    const Rect vp{0, 0, 400, 300};
    for (int r = 0; r < 12; ++r) {
        for (int q = 0; q < 16; ++q) {
            const auto [sx, sy] = v.cellToScreen(q, r, vp);
            const auto c = v.cellAt(sx, sy, vp, 16, 12);
            REQUIRE(c);
            CHECK(c->first == q);
            CHECK(c->second == r);
            // A third of the way to each neighbour is still this cell.
            const auto n = v.cellAt(sx + v.zoom / 3.0, sy, vp, 16, 12);
            REQUIRE(n);
            CHECK(n->first == q);
        }
    }
    // Neighbouring centres are one zoom apart in all six directions.
    const auto [ax, ay] = v.cellToScreen(5, 5, vp);
    for (auto [dq, dr] : {std::pair{1, 0}, {0, 1}, {1, -1}, {-1, 1}, {-1, 0}, {0, -1}}) {
        const auto [bx, by] = v.cellToScreen(5 + dq, 5 + dr, vp);
        CHECK_THAT(std::hypot(bx - ax, by - ay), WithinAbs(v.zoom, 1e-9));
    }
}

TEST_CASE("hex lattice: fit covers the rhombus", "[view][hex]") {
    using aether::render::Lattice;
    View2D v;
    v.lattice = Lattice::Hex;
    v.fit(20, 10, Rect{0, 0, 600, 400});
    const auto [ew, eh] = v.extent(20, 10);
    CHECK(ew > 20.0);           // sheared
    CHECK(eh < 10.0);           // rows are closer than one cell
    CHECK(v.zoom * ew <= 600.0 + 1e-9);
    CHECK(v.zoom * eh <= 400.0 + 1e-9);
}
