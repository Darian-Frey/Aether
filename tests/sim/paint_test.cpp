#include "rule/dsl.hpp"
#include "sim/simulation.hpp"
#include "support/gl_context.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether;
using aether::test::GlContext;
using aether::test::requireGl;

TEST_CASE("paintSpan writes host and GPU together without a readback", "[gpu][simulation]") {
    GlContext gl;
    requireGl(gl);
    const core::GridSpec spec{2, 20, 12, 1};
    auto made = sim::Simulation::create(spec, *rule::parseDsl("B2/S/C3").ir, sim::Path::Gpu);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    auto& s = std::get<sim::Simulation>(made);

    s.paintSpan(3, 7, 5, 0, 2);
    s.paintSpan(19, 19, 11, 0, 1);
    // Host copy has it immediately.
    CHECK(s.host().get(3, 5) == 2);
    CHECK(s.host().get(7, 5) == 2);
    CHECK(s.host().get(8, 5) == 0);
    CHECK(s.host().get(19, 11) == 1);

    // The GPU copy has it too: read it back through a path switch.
    s.host().clear();                       // scribble on the host copy...
    REQUIRE_FALSE(s.setPath(sim::Path::Cpu).has_value());   // ...then download over it
    CHECK(s.host().get(3, 5) == 2);
    CHECK(s.host().get(7, 5) == 2);
    CHECK(s.host().get(8, 5) == 0);
    CHECK(s.host().get(19, 11) == 1);
}

TEST_CASE("paintSpan on a 3D grid lands on the right slice", "[gpu][simulation][3d]") {
    GlContext gl;
    requireGl(gl);
    rule::DslContext ctx;
    ctx.dimensions = 3;
    const core::GridSpec spec{3, 12, 10, 8};
    auto made = sim::Simulation::create(spec, *rule::parseDsl("B5/S45", ctx).ir, sim::Path::Gpu);
    REQUIRE(std::holds_alternative<sim::Simulation>(made));
    auto& s = std::get<sim::Simulation>(made);
    s.paintSpan(2, 6, 4, 5, 1);
    s.paintSpan(11, 11, 9, 7, 1);
    s.host().clear();
    REQUIRE_FALSE(s.setPath(sim::Path::Cpu).has_value());   // download the GPU copy
    CHECK(s.host().get(2, 4, 5) == 1);
    CHECK(s.host().get(6, 4, 5) == 1);
    CHECK(s.host().get(7, 4, 5) == 0);
    CHECK(s.host().get(4, 4, 4) == 0);   // neighbouring slice untouched
    CHECK(s.host().get(4, 4, 6) == 0);
    CHECK(s.host().get(11, 9, 7) == 1);
    uint32_t total = 0;
    for (uint8_t c : s.host().current()) total += c;
    CHECK(total == 6);
}
