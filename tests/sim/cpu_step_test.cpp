#include "core/grid.hpp"
#include "rule/dsl.hpp"
#include "rule/lut.hpp"
#include "sim/cpu_step.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>

using namespace aether;
using core::GridSpec;
using core::HostGrid;
using rule::LutRule;

namespace {

LutRule compile(const char* dsl, const rule::DslContext& ctx = {}) {
    auto r = rule::parseDsl(dsl, ctx);
    REQUIRE(r);
    auto c = rule::compileLut(*r.ir);
    REQUIRE(std::holds_alternative<LutRule>(c));
    return std::get<LutRule>(std::move(c));
}

using Cells = std::set<std::pair<uint32_t, uint32_t>>;

Cells alive(const HostGrid& g) {
    Cells out;
    for (uint32_t y = 0; y < g.spec().height; ++y)
        for (uint32_t x = 0; x < g.spec().width; ++x)
            if (g.get(x, y) != 0) out.insert({x, y});
    return out;
}

void paint(HostGrid& g, const Cells& cells, uint8_t v = 1) {
    for (const auto& [x, y] : cells) g.set(x, y, 0, v);
}

void run(const LutRule& rule, HostGrid& g, int generations) {
    for (int i = 0; i < generations; ++i) sim::cpuStep(rule, g);
}

}  // namespace

TEST_CASE("glider arrives at its predicted offset (AV-004 detector)", "[cpu]") {
    const LutRule life = compile("B3/S23");
    HostGrid g({2, 32, 32, 1});
    const Cells glider = {{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}};
    paint(g, glider);

    run(life, g, 4);
    Cells expected;
    for (const auto& [x, y] : glider) expected.insert({x + 1, y + 1});
    CHECK(alive(g) == expected);

    run(life, g, 96);   // 100 total: 25 cells diagonally, wrapped on 32
    expected.clear();
    for (const auto& [x, y] : glider) expected.insert({(x + 25) % 32, (y + 25) % 32});
    CHECK(alive(g) == expected);
}

TEST_CASE("blinker has period 2; block is still", "[cpu]") {
    const LutRule life = compile("B3/S23");
    HostGrid g({2, 8, 8, 1});
    paint(g, {{2, 3}, {3, 3}, {4, 3}});
    run(life, g, 1);
    CHECK(alive(g) == Cells{{3, 2}, {3, 3}, {3, 4}});
    run(life, g, 1);
    CHECK(alive(g) == Cells{{2, 3}, {3, 3}, {4, 3}});

    g.clear();
    paint(g, {{1, 1}, {2, 1}, {1, 2}, {2, 2}});
    run(life, g, 5);
    CHECK(alive(g) == Cells{{1, 1}, {2, 1}, {1, 2}, {2, 2}});
}

TEST_CASE("step reads only current and writes only next", "[cpu]") {
    const LutRule life = compile("B3/S23");
    HostGrid g({2, 8, 8, 1});
    paint(g, {{2, 3}, {3, 3}, {4, 3}});
    const std::vector<uint8_t> before(g.current().begin(), g.current().end());
    sim::cpuStep(life, g.spec(), g.current(), g.next());
    CHECK(std::vector<uint8_t>(g.current().begin(), g.current().end()) == before);
    CHECK(g.next()[g.index(3, 2)] == 1);
    CHECK(g.next()[g.index(2, 3)] == 0);
}

TEST_CASE("boundary modes differ at the corner in the predicted way", "[cpu]") {
    // An L of three cells in the corner. Under zero and wrap (on a grid too
    // large for the L to see itself) the fourth cell is born and a block
    // forms. Under mirror the corner cell sees its reflections and dies.
    const Cells L = {{0, 0}, {1, 0}, {0, 1}};

    SECTION("zero") {
        rule::DslContext ctx;
        ctx.boundary = rule::Boundary::Zero;
        HostGrid g({2, 6, 6, 1});
        paint(g, L);
        run(compile("B3/S23", ctx), g, 1);
        CHECK(alive(g) == Cells{{0, 0}, {1, 0}, {0, 1}, {1, 1}});
    }
    SECTION("wrap") {
        HostGrid g({2, 6, 6, 1});
        paint(g, L);
        run(compile("B3/S23"), g, 1);
        CHECK(alive(g) == Cells{{0, 0}, {1, 0}, {0, 1}, {1, 1}});
    }
    SECTION("mirror") {
        rule::DslContext ctx;
        ctx.boundary = rule::Boundary::Mirror;
        HostGrid g({2, 6, 6, 1});
        paint(g, L);
        run(compile("B3/S23", ctx), g, 1);
        CHECK(alive(g) == Cells{{1, 0}, {0, 1}, {1, 1}});
    }
}

TEST_CASE("a blinker across the wrap seam behaves as in the interior", "[cpu]") {
    const LutRule life = compile("B3/S23");
    HostGrid g({2, 8, 8, 1});
    paint(g, {{7, 4}, {0, 4}, {1, 4}});
    run(life, g, 1);
    CHECK(alive(g) == Cells{{0, 3}, {0, 4}, {0, 5}});
    run(life, g, 1);
    CHECK(alive(g) == Cells{{7, 4}, {0, 4}, {1, 4}});
}

TEST_CASE("Brian's Brain: birth on two firing neighbours, then refractory, then dead", "[cpu]") {
    const LutRule bb = compile("B2/S/C3");
    HostGrid g({2, 6, 6, 1});
    paint(g, {{2, 2}, {3, 2}}, 1);
    run(bb, g, 1);
    // Cells above and below the pair each see both firing cells.
    CHECK(g.get(2, 1) == 1);
    CHECK(g.get(3, 1) == 1);
    CHECK(g.get(2, 3) == 1);
    CHECK(g.get(3, 3) == 1);
    CHECK(g.get(2, 2) == 2);   // refractory
    CHECK(g.get(3, 2) == 2);
    CHECK(g.get(1, 2) == 0);   // saw only one
    run(bb, g, 1);
    CHECK(g.get(2, 2) == 0);   // refractory dies regardless
}

TEST_CASE("Wireworld: a head advances along a wire", "[cpu]") {
    const char* src = R"(
        states 4;
        neighbourhood moore 1;
        1: n(0) >= 0 -> 2;
        2: n(0) >= 0 -> 3;
        3: n(1) == 1 or n(1) == 2 -> 1;
    )";
    const LutRule ww = compile(src);
    HostGrid g({2, 8, 3, 1});
    for (uint32_t x = 0; x < 8; ++x) g.set(x, 1, 0, 3);   // wire
    g.set(1, 1, 0, 1);                                    // head
    g.set(0, 1, 0, 2);                                    // tail behind it
    run(ww, g, 1);
    CHECK(g.get(0, 1) == 3);
    CHECK(g.get(1, 1) == 2);
    CHECK(g.get(2, 1) == 1);
    CHECK(g.get(3, 1) == 3);
    run(ww, g, 3);
    CHECK(g.get(5, 1) == 1);
    CHECK(g.get(4, 1) == 2);
}

TEST_CASE("non-totalistic: a copy-from-neighbour-0 rule shifts the pattern", "[cpu]") {
    // next = neighbour[0], the (-1,-1) offset, so the pattern moves (+1,+1).
    rule::RuleIR ir;
    ir.dimensions = 2;
    ir.states = 2;
    ir.kind = rule::Kind::NonTotalistic;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};
    rule::Table t;
    t.entries.resize(512);
    for (uint32_t own = 0; own < 2; ++own)
        for (uint32_t sig = 0; sig < 256; ++sig)
            t.entries[own * 256 + sig] = static_cast<uint8_t>(sig & 1u);
    ir.transition = t;
    auto c = rule::compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(c));

    HostGrid g({2, 8, 8, 1});
    paint(g, {{2, 2}, {3, 2}});
    run(std::get<LutRule>(c), g, 3);
    CHECK(alive(g) == Cells{{5, 5}, {6, 5}});
}

TEST_CASE("totalistic: sum == 1 grows a single cell into a 3x3 block", "[cpu]") {
    rule::RuleIR ir;
    ir.dimensions = 2;
    ir.states = 2;
    ir.kind = rule::Kind::Totalistic;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};
    rule::Table t;
    t.entries.assign(10, 0);
    t.entries[1] = 1;
    ir.transition = t;
    auto c = rule::compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(c));

    HostGrid g({2, 7, 7, 1});
    g.set(3, 3, 0, 1);
    run(std::get<LutRule>(c), g, 1);
    CHECK(alive(g).size() == 9);
    CHECK(g.get(3, 3) == 1);
    CHECK(g.get(2, 2) == 1);
    CHECK(g.get(4, 4) == 1);
    CHECK(g.get(1, 1) == 0);
}

TEST_CASE("1D: Rule 30 from a single cell", "[cpu]") {
    // N = 2 (left, right in canonical order). index = own*4 + left + 2*right.
    // Rule 30: new = left XOR (centre OR right).
    rule::RuleIR ir;
    ir.dimensions = 1;
    ir.states = 2;
    ir.kind = rule::Kind::NonTotalistic;
    ir.neighbourhood = {rule::NeighbourhoodType::Moore, 1};
    ir.boundary = rule::Boundary::Zero;
    rule::Table t;
    t.entries.resize(8);
    for (uint32_t own = 0; own < 2; ++own)
        for (uint32_t l = 0; l < 2; ++l)
            for (uint32_t r = 0; r < 2; ++r)
                t.entries[own * 4 + l + 2 * r] = static_cast<uint8_t>(l ^ (own | r));
    ir.transition = t;
    auto c = rule::compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(c));

    HostGrid g({1, 11, 1, 1});
    g.set(5, 0, 0, 1);
    auto row = [&] {
        std::string s;
        for (uint32_t x = 0; x < 11; ++x) s += g.get(x) ? '#' : '.';
        return s;
    };
    CHECK(row() == ".....#.....");
    run(std::get<LutRule>(c), g, 1);
    CHECK(row() == "....###....");
    run(std::get<LutRule>(c), g, 1);
    CHECK(row() == "...##..#...");
    run(std::get<LutRule>(c), g, 1);
    CHECK(row() == "..##.####..");
    run(std::get<LutRule>(c), g, 1);
    CHECK(row() == ".##..#...#.");
}

TEST_CASE("3D: B1/S on von Neumann grows a cross and kills the seed", "[cpu]") {
    const char* src = R"(
        states 2;
        neighbourhood von_neumann 1;
        0: n(1) == 1 -> 1;
        1: n(1) >= 0 -> 0;
    )";
    rule::DslContext ctx;
    ctx.dimensions = 3;
    const LutRule r = compile(src, ctx);
    CHECK(r.neighbourCount() == 6);

    HostGrid g({3, 5, 5, 5});
    g.set(2, 2, 2, 1);
    run(r, g, 1);
    CHECK(g.get(2, 2, 2) == 0);
    CHECK(g.get(1, 2, 2) == 1);
    CHECK(g.get(3, 2, 2) == 1);
    CHECK(g.get(2, 1, 2) == 1);
    CHECK(g.get(2, 3, 2) == 1);
    CHECK(g.get(2, 2, 1) == 1);
    CHECK(g.get(2, 2, 3) == 1);
    CHECK(g.get(1, 1, 2) == 0);
    uint32_t total = 0;
    for (uint8_t v : g.current()) total += v;
    CHECK(total == 6);
}
