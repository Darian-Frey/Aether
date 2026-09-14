#include "rule/dsl.hpp"
#include "rule/lut.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace aether::rule;

TEST_CASE("backend selection follows table size and form", "[lut]") {
    CHECK(selectBackend(*parseDsl("B3/S23").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C3").ir) == Backend::Lut);
    CHECK(selectBackend(*parseDsl("B2/S/C25").ir) == Backend::Lut);        // counted, so it fits (D-016)
    CHECK(selectBackend(*parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1; 1: n(1) < 2 -> 2;").ir) == Backend::Codegen);   // expression form

    RuleIR big;
    big.dimensions = 3;
    big.states = 2;
    big.kind = Kind::NonTotalistic;
    big.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression e;
    e.nodes = {{ExprOp::Self}};
    big.transition = e;
    CHECK(selectBackend(big) == Backend::Codegen);
}

TEST_CASE("compileLut carries everything the steppers need", "[lut]") {
    const auto ir = *parseDsl("B3/S23").ir;
    auto made = compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(made));
    const LutRule& r = std::get<LutRule>(made);
    CHECK(r.ir_hash == irHash(ir));
    CHECK(r.states == 2);
    CHECK(r.kind == Kind::OuterTotalistic);
    CHECK(r.neighbourCount() == 8);
    CHECK(r.offsets == neighbourOffsets(2, {NeighbourhoodType::Moore, 1}));
    CHECK(r.table.size() == 18);
    CHECK(r.layout.size() == 18);
    REQUIRE(r.aux.size() == 9 * 2);
    CHECK(r.aux[8 * 2 + 1] == 9);   // W(8, 1) = 9
}

TEST_CASE("compileLut carries the compositions a full count vector needs", "[lut]") {
    // A rule whose own state asks about two different counts keeps the full
    // vector, so the auxiliary buffer is the W table.
    const auto ir = *parseDsl("states 4; neighbourhood moore 1; 0: n(1) == 1 and n(2) == 1 -> 1;").ir;
    REQUIRE(ir.kind == Kind::OuterTotalistic);
    auto made = compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(made));
    const LutRule& r = std::get<LutRule>(made);
    const TableLayout L(Kind::OuterTotalistic, 4, 8);
    REQUIRE(r.aux.size() == 9 * 4);
    for (uint32_t n = 0; n <= 8; ++n)
        for (uint32_t m = 0; m < 4; ++m)
            CHECK(r.aux[n * 4 + m] == L.compositions(n, m));
    CHECK(r.aux[8 * 4 + 3] == 165);
}

TEST_CASE("compileLut carries the counted sets as masks", "[lut]") {
    const auto ir = *parseDsl("B2/S/C4").ir;
    REQUIRE(ir.kind == Kind::CountedTotalistic);
    auto made = compileLut(ir);
    REQUIRE(std::holds_alternative<LutRule>(made));
    const LutRule& r = std::get<LutRule>(made);
    CHECK(r.table.size() == 4 * 9);
    REQUIRE(r.aux.size() == 4 * 8);        // eight words of mask per own state
    CHECK(r.aux[0 * 8] == 0b10u);          // state 0 counts state 1
    CHECK(r.aux[1 * 8] == 0b10u);
    CHECK(r.aux[2 * 8] == 0u);             // the tail counts nothing
    CHECK(r.aux[3 * 8] == 0u);
    CHECK(r.counted.size() == 4);
    CHECK(r.counted[0].test(1));
    CHECK_FALSE(r.counted[0].test(2));
}

TEST_CASE("compileLut refuses what the table backend cannot serve", "[lut]") {
    auto expr = compileLut(*parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1; 1: n(1) < 2 -> 2;").ir);
    REQUIRE(std::holds_alternative<CompileError>(expr));
    CHECK(std::get<CompileError>(expr).message.find("no table") != std::string::npos);

    auto ir = *parseDsl("B3/S23").ir;
    std::get<Table>(ir.transition).entries.pop_back();
    auto bad = compileLut(ir);
    REQUIRE(std::holds_alternative<CompileError>(bad));
    CHECK(std::get<CompileError>(bad).message.find("invalid IR") != std::string::npos);
}
