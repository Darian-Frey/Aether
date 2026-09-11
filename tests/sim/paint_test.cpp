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
