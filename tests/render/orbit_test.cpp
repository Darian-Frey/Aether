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
