#include "rule/dsl.hpp"
#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace aether::rule;

namespace {

// The table entry for one own state and one neighbour vector.
uint8_t entry(const RuleIR& ir, uint8_t own, std::vector<uint8_t> nbr) {
    const TableLayout L(ir.kind, ir.states, neighbourCount(ir.dimensions, ir.neighbourhood));
    return std::get<Table>(ir.transition).entries[L.indexNonTotalistic(own, nbr)];
}

}  // namespace

TEST_CASE("rotation permutations map a neighbourhood onto itself", "[signature]") {
    // 2D von Neumann r=1 is [above, left, right, below]; a quarter turn
    // sends above to right, right to below, below to left, left to above.
    const auto vn = rotationPermutation(2, {NeighbourhoodType::VonNeumann, 1});
    REQUIRE(vn);
    CHECK(*vn == std::vector<uint32_t>{2, 0, 3, 1});

    for (auto type : {NeighbourhoodType::Moore, NeighbourhoodType::VonNeumann, NeighbourhoodType::Hexagonal}) {
        for (uint8_t r = 1; r <= 2; ++r) {
            const auto p = rotationPermutation(2, {type, r});
            REQUIRE(p);
            // A permutation: every index appears exactly once.
            std::vector<uint32_t> sorted = *p;
            std::sort(sorted.begin(), sorted.end());
            for (uint32_t i = 0; i < sorted.size(); ++i) CHECK(sorted[i] == i);
        }
    }
    CHECK_FALSE(rotationPermutation(1, {NeighbourhoodType::Moore, 1}).has_value());
    CHECK_FALSE(rotationPermutation(3, {NeighbourhoodType::Moore, 1}).has_value());
}

TEST_CASE("a literal matches one exact neighbourhood", "[signature]") {
    const auto r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 0, 0, 0] -> 1;");
    REQUIRE(r);
    CHECK(r.ir->kind == Kind::NonTotalistic);
    CHECK(std::get<Table>(r.ir->transition).entries.size() == 2 * 16);
    CHECK(entry(*r.ir, 0, {1, 0, 0, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 1, 0, 0}) == 0);
    CHECK(entry(*r.ir, 0, {1, 1, 0, 0}) == 0);   // the other elements must agree too
    CHECK(entry(*r.ir, 1, {1, 0, 0, 0}) == 1);   // no statement for state 1: retains
}

TEST_CASE("wildcards match any state in their position", "[signature]") {
    const auto r = parseDsl("states 3; neighbourhood von_neumann 1; 0: [2, _, _, _] -> 1;");
    REQUIRE(r);
    for (uint8_t a = 0; a < 3; ++a) {
        for (uint8_t b = 0; b < 3; ++b) {
            CHECK(entry(*r.ir, 0, {2, a, b, 1}) == 1);
            CHECK(entry(*r.ir, 0, {1, a, b, 1}) == 0);
        }
    }
}

TEST_CASE("rot expands a literal to its rotations", "[signature]") {
    const auto r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 0, 0, 0] rot -> 1;");
    REQUIRE(r);
    // Exactly one live neighbour, in any of the four directions.
    CHECK(entry(*r.ir, 0, {1, 0, 0, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 1, 0, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 0, 1, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 0, 0, 1}) == 1);
    CHECK(entry(*r.ir, 0, {1, 1, 0, 0}) == 0);
    CHECK(entry(*r.ir, 0, {0, 0, 0, 0}) == 0);

    // Without rot, only the written direction.
    const auto one = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 0, 0, 0] -> 1;");
    REQUIRE(one);
    CHECK(entry(*one.ir, 0, {0, 1, 0, 0}) == 0);
}

TEST_CASE("rot on a hexagonal lattice gives six rotations", "[signature][hex]") {
    const auto r = parseDsl("states 2; neighbourhood hex 1; 0: [1, 0, 0, 0, 0, 0] rot -> 1;");
    REQUIRE(r);
    CHECK(neighbourCount(2, r.ir->neighbourhood) == 6);
    for (size_t i = 0; i < 6; ++i) {
        std::vector<uint8_t> nbr(6, 0);
        nbr[i] = 1;
        CHECK(entry(*r.ir, 0, nbr) == 1);
    }
    CHECK(entry(*r.ir, 0, {1, 1, 0, 0, 0, 0}) == 0);
}

TEST_CASE("a rotationally symmetric literal does not multiply", "[signature]") {
    // All four neighbours alike: every rotation is the same pattern.
    const auto r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 1, 1, 1] rot -> 1;");
    REQUIRE(r);
    CHECK(entry(*r.ir, 0, {1, 1, 1, 1}) == 1);
    CHECK(entry(*r.ir, 0, {1, 1, 1, 0}) == 0);
}

TEST_CASE("literals and count conditions mix, first match winning", "[signature]") {
    const char* src = R"(
        states 3;
        neighbourhood von_neumann 1;
        0: [1, _, _, _] and n(2) == 0 -> 1;
        0: n(1) >= 2 -> 2;
    )";
    const auto r = parseDsl(src);
    REQUIRE(r);
    CHECK(entry(*r.ir, 0, {1, 0, 0, 0}) == 1);   // literal matches, no state 2 about
    CHECK(entry(*r.ir, 0, {1, 2, 0, 0}) == 0);   // literal matches but the count forbids it,
                                                 // and only one live neighbour for the second
    CHECK(entry(*r.ir, 0, {1, 1, 0, 0}) == 1);   // first statement still wins
    CHECK(entry(*r.ir, 0, {0, 1, 1, 0}) == 2);   // second statement
}

TEST_CASE("a signature rule runs: copy the cell above", "[signature]") {
    const auto r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1,_,_,_] -> 1; 1: [0,_,_,_] -> 0;");
    REQUIRE(r);
    // next = the state above, so a pattern marches one row down per step.
    for (uint8_t above = 0; above < 2; ++above) {
        for (uint8_t own = 0; own < 2; ++own) {
            CHECK(entry(*r.ir, own, {above, 1, 0, 1}) == above);
        }
    }
}

TEST_CASE("signature errors report position and reason", "[signature]") {
    auto r = parseDsl("states 2;\nneighbourhood von_neumann 1;\n0: [1, 0, 0] -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->line == 3);
    CHECK(r.error->column == 4);
    CHECK(r.error->message.find("3 elements") != std::string::npos);
    CHECK(r.error->message.find("4 neighbours") != std::string::npos);

    r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 0, 0, 2] -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("state must be in 0..1") != std::string::npos);

    DslContext ctx;
    ctx.dimensions = 3;
    r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1,_,_,_,_,_] rot -> 1;", ctx);
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("2D lattices only") != std::string::npos);

    r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1, 0, 0, 0 -> 1;");
    REQUIRE_FALSE(r);
    CHECK(r.error->message.find("']'") != std::string::npos);
}

TEST_CASE("a signature table over the threshold is refused with its size", "[signature]") {
    // 8 states over von Neumann is 8 * 8^4 = 32768 and fits; Moore does not.
    const auto ok = parseDsl("states 8; neighbourhood von_neumann 1; 0: [1,_,_,_] -> 2;");
    REQUIRE(ok);
    CHECK(std::get<Table>(ok.ir->transition).entries.size() == 32768);

    const auto tooBig = parseDsl("states 8; neighbourhood moore 1; 0: [1,_,_,_,_,_,_,_] -> 2;");
    REQUIRE_FALSE(tooBig);
    CHECK(tooBig.error->message.find("table entries") != std::string::npos);
    CHECK(tooBig.error->message.find("codegen") != std::string::npos);
}

TEST_CASE("3D signature rules work; rotation is what 3D lacks", "[signature]") {
    DslContext ctx;
    ctx.dimensions = 3;
    const auto r = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1,_,_,_,_,_] -> 1;", ctx);
    REQUIRE(r);
    CHECK(r.ir->kind == Kind::NonTotalistic);
    CHECK(std::get<Table>(r.ir->transition).entries.size() == 2 * 64);
    CHECK(entry(*r.ir, 0, {1, 0, 0, 0, 0, 0}) == 1);
    CHECK(entry(*r.ir, 0, {0, 1, 0, 0, 0, 0}) == 0);
}
