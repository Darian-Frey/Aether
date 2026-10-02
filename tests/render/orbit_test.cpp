#include "render/orbit.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace aether::render;
using Catch::Matchers::WithinAbs;

TEST_CASE("the centre pixel's ray passes through the target", "[orbit]") {
    Orbit o;
    o.fit(32, 24, 16);
    const Rect vp{100, 50, 640, 480};
    const Ray r = o.rayFor(100 + 320, 50 + 240, vp);
    // Closest approach of the ray to the target is (numerically) zero.
    const Vec3 toT = o.target - r.origin;
    const double along = toT.dot(r.dir);
    const Vec3 closest = r.origin + r.dir * along;
    CHECK_THAT((closest - o.target).length(), WithinAbs(0.0, 1e-9));
    CHECK(along > 0);
    // The basis is orthonormal.
    const auto [f, rt, up] = o.basis();
    CHECK_THAT(f.length(), WithinAbs(1.0, 1e-12));
    CHECK_THAT(f.dot(rt), WithinAbs(0.0, 1e-12));
    CHECK_THAT(f.dot(up), WithinAbs(0.0, 1e-12));
    CHECK_THAT(rt.dot(up), WithinAbs(0.0, 1e-12));
}

TEST_CASE("fit places the camera outside the grid and looking at its centre", "[orbit]") {
    Orbit o;
    o.fit(64, 64, 64);
    CHECK(o.target.x == 32.0);
    CHECK(o.distance > 0.5 * std::sqrt(3.0) * 64);
    const Vec3 p = o.position();
    CHECK((p.x < 0 || p.x >= 64 || p.y < 0 || p.y >= 64 || p.z < 0 || p.z >= 64));
}

TEST_CASE("slab picking finds the cell under the pixel", "[orbit]") {
    Orbit o;
    o.fit(16, 16, 16);
    o.yaw = 0.0;
    o.pitch = 0.0;        // looking along -z from +z
    const Rect vp{0, 0, 400, 400};
    // Centre pixel on the z = 8 slab: the centre cell.
    const auto c = o.pickOnSlab(200, 200, vp, 2, 8, 16, 16, 16);
    REQUIRE(c);
    CHECK((*c)[2] == 8);
    CHECK((*c)[0] == 8);
    CHECK((*c)[1] == 8);
    // Well off to the side: misses.
    CHECK_FALSE(o.pickOnSlab(2, 200, vp, 2, 8, 16, 16, 16).has_value());
    // A slab seen edge-on from this camera (x = 8, ray parallel) still
    // resolves because the ray is not exactly parallel from off-centre pixels.
    const auto e = o.pickOnSlab(210, 200, vp, 0, 8, 16, 16, 16);
    if (e) CHECK((*e)[0] == 8);
}

TEST_CASE("rotate clamps pitch and zoom clamps distance", "[orbit]") {
    Orbit o;
    o.rotate(0.0, 10.0);
    CHECK(o.pitch < 1.56);
    o.rotate(0.0, -20.0);
    CHECK(o.pitch > -1.56);
    o.zoom(1e-9);
    CHECK(o.distance == 0.5);
}

TEST_CASE("project is the inverse of rayFor (F-037)", "[orbit]") {
    // The preview and the pick must agree about where a cell is, and the only
    // way to be sure is to send a point round the loop: project it to a pixel,
    // shoot a ray through that pixel, and check the ray passes through the
    // point it came from.
    Orbit o;
    o.fit(32, 24, 16);
    o.yaw = 0.7;
    o.pitch = 0.3;
    const Rect vp{37, 11, 800, 600};

    for (const Vec3 p : {Vec3{0, 0, 0}, Vec3{32, 24, 16}, Vec3{5.5, 20.5, 3.5},
                         Vec3{16, 12, 8}, Vec3{31, 1, 15}}) {
        const auto px = o.project(p, vp);
        REQUIRE(px);
        const Ray r = o.rayFor(px->first, px->second, vp);
        // The point lies along the ray: its distance from the line is zero.
        const Vec3 rel = p - r.origin;
        const Vec3 along = r.dir * rel.dot(r.dir);
        CHECK_THAT((rel - along).length(), WithinAbs(0.0, 1e-9));
    }

    // The grid centre projects to the viewport centre, since that is what the
    // camera is pointed at.
    const auto mid = o.project(o.target, vp);
    REQUIRE(mid);
    CHECK_THAT(mid->first, WithinAbs(vp.x + vp.w * 0.5, 1e-9));
    CHECK_THAT(mid->second, WithinAbs(vp.y + vp.h * 0.5, 1e-9));

    // And a point behind the camera has no pixel rather than a mirrored one,
    // which is the bug a naive divide gives: the far side of the orbit would
    // draw a box behind the viewer as though it were in front.
    const Vec3 behind = o.position() + (o.position() - o.target);
    CHECK_FALSE(o.project(behind, vp).has_value());

    // A cell picked off the screen projects back to the pixel it was picked
    // with, to within the cell it names.
    const auto cell = o.pickOnSlab(400, 300, vp, 2, 8, 32, 24, 16);
    REQUIRE(cell);
    const auto back = o.project(Vec3{(*cell)[0] + 0.5, (*cell)[1] + 0.5, (*cell)[2] + 0.5}, vp);
    REQUIRE(back);
    CHECK_THAT(back->first, WithinAbs(400.0, 30.0));
    CHECK_THAT(back->second, WithinAbs(300.0, 30.0));
}
