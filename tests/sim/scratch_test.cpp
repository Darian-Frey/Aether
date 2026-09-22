// The pattern editor's scratch pad (F-029, D-018).
//
// Every case here runs with no GL context, which is the point of the module:
// the scratch pad is host-side, so it is testable where most of `ui/` is not.

#include "sim/scratch.hpp"

#include "rule/dsl.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>
#include <vector>

using namespace aether;
using core::GridSpec;
using sim::Pattern;
using sim::Scratch;

namespace {

rule::RuleIR ir(const char* dsl, const rule::DslContext& ctx = {}) {
    auto r = rule::parseDsl(dsl, ctx);
    REQUIRE(r);
    return *r.ir;
}

Scratch pad(uint32_t w = 16, uint32_t h = 16, const char* dsl = "B3/S23") {
    auto s = Scratch::make(GridSpec{2, w, h, 1}, ir(dsl));
    if (const auto* e = std::get_if<sim::PatternError>(&s)) FAIL(e->message);
    return std::get<Scratch>(std::move(s));
}

// A glider at (x, y), the same cells the bundled pattern holds.
void glider(Scratch& s, uint32_t x, uint32_t y) {
    s.set(x + 1, y + 0, 0, 1);
    s.set(x + 2, y + 1, 0, 1);
    s.set(x + 0, y + 2, 0, 1);
    s.set(x + 1, y + 2, 0, 1);
    s.set(x + 2, y + 2, 0, 1);
}

std::set<std::pair<uint32_t, uint32_t>> alive(const Scratch& s) {
    std::set<std::pair<uint32_t, uint32_t>> out;
    for (uint32_t y = 0; y < s.spec().height; ++y) {
        for (uint32_t x = 0; x < s.spec().width; ++x) {
            if (s.get(x, y) != 0) out.insert({x, y});
        }
    }
    return out;
}

}  // namespace

TEST_CASE("a fresh pad is empty, at generation zero, with nothing to step back to", "[scratch]") {
    Scratch s = pad();
    CHECK(alive(s).empty());
    CHECK(s.generation() == 0);
    CHECK(s.history() == 0);
    CHECK_FALSE(s.stepBack());
}

TEST_CASE("the pad steps on the CPU path", "[scratch]") {
    Scratch s = pad();
    glider(s, 2, 2);
    const auto start = alive(s);
    for (int i = 0; i < 4; ++i) s.step();
    CHECK(s.generation() == 4);

    // Four generations displace a glider by one cell on each axis.
    std::set<std::pair<uint32_t, uint32_t>> want;
    for (auto [x, y] : start) want.insert({x + 1, y + 1});
    CHECK(alive(s) == want);
}

TEST_CASE("stepping back returns the cells that were there", "[scratch]") {
    Scratch s = pad();
    glider(s, 2, 2);
    std::vector<std::set<std::pair<uint32_t, uint32_t>>> seen{alive(s)};
    for (int i = 0; i < 6; ++i) { s.step(); seen.push_back(alive(s)); }

    for (int i = 6; i > 0; --i) {
        CHECK(s.generation() == static_cast<uint64_t>(i));
        REQUIRE(s.stepBack());
        CHECK(alive(s) == seen[static_cast<size_t>(i) - 1]);
    }
    CHECK(s.generation() == 0);
    CHECK_FALSE(s.stepBack());
}

TEST_CASE("the history is a ring, so the oldest generation is dropped", "[scratch]") {
    Scratch s = pad();
    glider(s, 2, 2);
    for (size_t i = 0; i < Scratch::kHistory + 10; ++i) s.step();
    CHECK(s.history() == Scratch::kHistory);

    size_t back = 0;
    while (s.stepBack()) ++back;
    CHECK(back == Scratch::kHistory);
    CHECK(s.generation() == 10);
}

TEST_CASE("stepping forward over the same generation twice gives the same cells", "[scratch]") {
    // The scratch pad applies no cell mutation, so this holds. It is what
    // makes step-back an editor control rather than a die roll.
    Scratch s = pad();
    glider(s, 2, 2);
    for (int i = 0; i < 5; ++i) s.step();
    const auto once = alive(s);
    REQUIRE(s.stepBack());
    s.step();
    CHECK(alive(s) == once);
}

TEST_CASE("painting ignores what is off the pad or outside the rule", "[scratch]") {
    Scratch s = pad(8, 8);
    s.set(3, 3, 0, 1);
    s.set(8, 3, 0, 1);      // off the right edge
    s.set(3, 99, 0, 1);     // off the bottom
    s.set(4, 4, 0, 7);      // state 7 on a two-state rule
    CHECK(alive(s) == std::set<std::pair<uint32_t, uint32_t>>{{3, 3}});
}

TEST_CASE("resizing keeps what still fits", "[scratch]") {
    Scratch s = pad(16, 16);
    glider(s, 1, 1);
    s.set(15, 15, 0, 1);
    const auto before = alive(s);

    REQUIRE_FALSE(s.resize(8, 8, 1));
    CHECK(s.spec().width == 8);
    CHECK(s.spec().height == 8);
    // The glider is inside the new extent; the far corner is not.
    std::set<std::pair<uint32_t, uint32_t>> want;
    for (auto [x, y] : before) { if (x < 8 && y < 8) want.insert({x, y}); }
    CHECK(alive(s) == want);

    // Growing back leaves the survivors where they were, with new empty space.
    REQUIRE_FALSE(s.resize(24, 24, 1));
    CHECK(alive(s) == want);
    CHECK(s.generation() == 0);
    CHECK(s.history() == 0);
}

TEST_CASE("adopting a rule keeps the drawing and resets states it cannot hold", "[scratch]") {
    Scratch s = pad(8, 8, "B3/S23/C6");   // six states
    s.set(1, 1, 0, 1);
    s.set(2, 2, 0, 4);
    REQUIRE(s.ir().states == 6);

    REQUIRE_FALSE(s.setRule(ir("B3/S23")));
    CHECK(s.ir().states == 2);
    CHECK(s.get(1, 1) == 1);   // still a valid state
    CHECK(s.get(2, 2) == 0);   // state 4 is not, so it goes
}

TEST_CASE("a rule of another dimensionality is refused rather than applied", "[scratch]") {
    Scratch s = pad(8, 8);
    rule::DslContext ctx;
    ctx.dimensions = 3;
    auto e = s.setRule(ir("B5/S45", ctx));
    REQUIRE(e);
    CHECK(e->message.find("3D") != std::string::npos);
    CHECK(s.ir().dimensions == 2);      // unchanged
}

TEST_CASE("the pad round-trips through a pattern", "[scratch]") {
    Scratch s = pad(16, 16);
    glider(s, 4, 4);
    const Pattern whole = s.toPattern();
    CHECK(whole.width == 16);
    CHECK(whole.height == 16);

    auto region = s.toPattern(4, 4, 0, 3, 3, 1);
    if (const auto* e = std::get_if<sim::PatternError>(&region)) FAIL(e->message);
    const Pattern p = std::get<Pattern>(region);
    CHECK(p.width == 3);
    CHECK(p.height == 3);

    Scratch other = pad(16, 16);
    REQUIRE_FALSE(other.place(p, 4, 4, 0));
    CHECK(alive(other) == alive(s));
}

TEST_CASE("the pad refuses a pattern it cannot hold, as the live grid does", "[scratch]") {
    Scratch s = pad(8, 8);
    Pattern big;
    big.width = 12; big.height = 2; big.states = 2;
    big.cells.assign(24, 0);
    auto e = s.place(big, 0, 0, 0);
    REQUIRE(e);
    CHECK(e->message.find("hangs over the edge") != std::string::npos);
}

TEST_CASE("placing onto the pad resets the generation and the history", "[scratch]") {
    Scratch s = pad(16, 16);
    glider(s, 2, 2);
    for (int i = 0; i < 3; ++i) s.step();
    REQUIRE(s.history() == 3);

    Pattern dot;
    dot.width = 1; dot.height = 1; dot.states = 2;
    dot.cells = {1};
    REQUIRE_FALSE(s.place(dot, 0, 0, 0));
    CHECK(s.generation() == 0);
    CHECK(s.history() == 0);
    CHECK_FALSE(s.stepBack());
}

TEST_CASE("clearing empties the pad and forgets where it had been", "[scratch]") {
    Scratch s = pad();
    glider(s, 2, 2);
    s.step();
    s.clear();
    CHECK(alive(s).empty());
    CHECK(s.generation() == 0);
    CHECK_FALSE(s.stepBack());
}
