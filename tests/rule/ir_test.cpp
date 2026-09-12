#include "rule/ir.hpp"
#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace aether::rule;
using Catch::Matchers::ContainsSubstring;

namespace {

// Conway's Life as an IR, built by hand here because no front end exists yet.
RuleIR life() {
    RuleIR ir;
    ir.dimensions = 2;
    ir.states = 2;
    ir.neighbourhood = {NeighbourhoodType::Moore, 1};
    ir.kind = Kind::OuterTotalistic;
    Table t;
    t.entries.assign(18, 0);
    t.entries[0 * 9 + 3] = 1;                     // B3
    t.entries[1 * 9 + 2] = 1;                     // S2
    t.entries[1 * 9 + 3] = 1;                     // S3
    ir.transition = t;
    ir.metadata.name = "Life";
    ir.metadata.source_notation = "B3/S23";
    return ir;
}

// An expression: self == 0 ? (count(1) == 3 ? 1 : 0) : 1
Expression lifeBirthExpr() {
    Expression e;
    e.nodes = {
        {ExprOp::Self},                                   // 0
        {ExprOp::IntLiteral, 0, 0, 0, 0},                 // 1
        {ExprOp::Eq, 0, 1},                               // 2: self == 0
        {ExprOp::Count, 1},                               // 3: count(1)
        {ExprOp::IntLiteral, 0, 0, 0, 3},                 // 4
        {ExprOp::Eq, 3, 4},                               // 5: count(1) == 3
        {ExprOp::IntLiteral, 0, 0, 0, 1},                 // 6
        {ExprOp::IntLiteral, 0, 0, 0, 0},                 // 7
        {ExprOp::Select, 5, 6, 7},                        // 8
        {ExprOp::IntLiteral, 0, 0, 0, 1},                 // 9
        {ExprOp::Select, 2, 8, 9},                        // 10 root
    };
    return e;
}

bool hasDiagnostic(const std::vector<Diagnostic>& ds, const char* fragment) {
    for (const auto& d : ds) {
        if (d.message.find(fragment) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("a hand-built Life IR validates", "[ir]") {
    const auto ds = validate(life());
    for (const auto& d : ds) INFO(d.message);
    CHECK(ds.empty());
    CHECK(isValid(life()));
}

TEST_CASE("state count bounds", "[ir]") {
    auto ir = life();
    ir.states = 1;
    CHECK(hasDiagnostic(validate(ir), "states must be in 2..256"));
    ir.states = 257;
    CHECK(hasDiagnostic(validate(ir), "states must be in 2..256"));
}

TEST_CASE("dimensions and radius bounds", "[ir]") {
    auto ir = life();
    ir.dimensions = 0;
    CHECK(hasDiagnostic(validate(ir), "dimensions must be 1, 2 or 3"));
    ir.dimensions = 4;
    CHECK(hasDiagnostic(validate(ir), "dimensions must be 1, 2 or 3"));
    ir = life();
    ir.neighbourhood.radius = 0;
    CHECK(hasDiagnostic(validate(ir), "radius must be >= 1"));
}

TEST_CASE("table length must match the §5 size exactly", "[ir]") {
    auto ir = life();
    std::get<Table>(ir.transition).entries.push_back(0);
    CHECK(hasDiagnostic(validate(ir), "table has 19 entries"));
    std::get<Table>(ir.transition).entries.resize(17);
    CHECK(hasDiagnostic(validate(ir), "requires 18"));
}

TEST_CASE("table entries must be state indices", "[ir]") {
    auto ir = life();
    std::get<Table>(ir.transition).entries[5] = 2;
    CHECK(hasDiagnostic(validate(ir), "table entry 5 is state 2"));
}

TEST_CASE("non-totalistic signature must fit 64 bits", "[ir]") {
    RuleIR ir;
    ir.dimensions = 3;
    ir.states = 2;
    ir.neighbourhood = {NeighbourhoodType::Moore, 2};   // 124 neighbours
    ir.kind = Kind::NonTotalistic;
    ir.transition = lifeBirthExpr();
    CHECK(hasDiagnostic(validate(ir), "the limit is 64"));

    ir.neighbourhood = {NeighbourhoodType::Moore, 1};   // 26
    CHECK(isValid(ir));
}

TEST_CASE("cell type and kind must agree", "[ir]") {
    auto ir = life();
    ir.cell_type = CellType::F32;
    CHECK(hasDiagnostic(validate(ir), "does not agree with kind"));

    RuleIR c;
    c.cell_type = CellType::U8;
    c.kind = Kind::Continuous;
    c.transition = Kernel{};
    CHECK(hasDiagnostic(validate(c), "does not agree with kind"));
}

TEST_CASE("transition form must suit the kind", "[ir]") {
    auto ir = life();
    ir.transition = lifeBirthExpr();
    CHECK(hasDiagnostic(validate(ir), "does not permit this transition form"));

    ir.kind = Kind::Expression;
    CHECK(isValid(ir));

    ir.kind = Kind::NonTotalistic;   // permits either form
    CHECK(isValid(ir));
}

TEST_CASE("expression type checking", "[ir]") {
    RuleIR ir = life();
    ir.kind = Kind::Expression;

    SECTION("forward reference") {
        Expression e;
        e.nodes = {{ExprOp::Not, 1}, {ExprOp::IntLiteral}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "refers forward"));
    }
    SECTION("neighbour index out of range") {
        Expression e;
        e.nodes = {{ExprOp::Neighbour, 8}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "neighbour index 8 out of range"));
    }
    SECTION("count of a non-existent state") {
        Expression e;
        e.nodes = {{ExprOp::Count, 2}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "count of state 2 out of range"));
    }
    SECTION("mixed numeric types") {
        Expression e;
        e.nodes = {{ExprOp::Self}, {ExprOp::FloatLiteral, 0, 0, 0, 0, 1.0f}, {ExprOp::Add, 0, 1}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "same type"));
    }
    SECTION("boolean root is not a state") {
        Expression e;
        e.nodes = {{ExprOp::Self}, {ExprOp::IntLiteral}, {ExprOp::Eq, 0, 1}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "must produce an integer state"));
    }
    SECTION("select branches must agree") {
        Expression e;
        e.nodes = {{ExprOp::Self}, {ExprOp::IntLiteral}, {ExprOp::Eq, 0, 1},
                   {ExprOp::FloatLiteral}, {ExprOp::Select, 2, 0, 3}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "branches must have the same type"));
    }
    SECTION("result literal out of range") {
        Expression e;
        e.nodes = {{ExprOp::Self}, {ExprOp::IntLiteral}, {ExprOp::Eq, 0, 1},
                   {ExprOp::IntLiteral, 0, 0, 0, 7}, {ExprOp::IntLiteral, 0, 0, 0, 0},
                   {ExprOp::Select, 2, 3, 4}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "result literal 7 outside 0..1"));
    }
    SECTION("orphan node") {
        Expression e;
        e.nodes = {{ExprOp::IntLiteral, 0, 0, 0, 1}, {ExprOp::IntLiteral, 0, 0, 0, 0}};
        ir.transition = e;
        CHECK(hasDiagnostic(validate(ir), "unreachable from the root"));
    }
    SECTION("empty expression") {
        ir.transition = Expression{};
        CHECK(hasDiagnostic(validate(ir), "no nodes"));
    }
}

TEST_CASE("continuous IR validates structurally", "[ir]") {
    RuleIR ir;
    ir.dimensions = 2;
    ir.cell_type = CellType::F32;
    ir.kind = Kind::Continuous;
    ir.neighbourhood = {NeighbourhoodType::Moore, 1};
    Kernel k;
    k.shape = Kernel::Shape::Explicit;
    k.profile.assign(9, 1.0f / 9.0f);
    k.growth.nodes = {{ExprOp::Self}};   // growth = conv, wrong type on purpose below
    ir.transition = k;
    CHECK(hasDiagnostic(validate(ir), "growth expression must produce a float"));

    k.growth.nodes = {{ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
    ir.transition = k;
    CHECK(isValid(ir));

    k.profile.resize(8);
    ir.transition = k;
    CHECK(hasDiagnostic(validate(ir), "explicit kernel has 8 weights"));
}

TEST_CASE("hash ignores metadata and is sensitive to everything else", "[ir]") {
    const auto a = life();
    auto b = life();
    b.metadata.name = "Something else";
    b.metadata.author = "Nobody";
    CHECK(irHash(a) == irHash(b));

    auto c = life();
    std::get<Table>(c.transition).entries[0] = 1;
    CHECK(irHash(a) != irHash(c));

    auto d = life();
    d.boundary = Boundary::Zero;
    CHECK(irHash(a) != irHash(d));

    auto e = life();
    e.neighbourhood.type = NeighbourhoodType::VonNeumann;
    CHECK(irHash(a) != irHash(e));
}

TEST_CASE("hash is a fixed value for Life", "[ir]") {
    // Pinned so that a change to the canonical serialisation is caught here
    // rather than by every session file on disk failing to match.
    CHECK(irHash(life()) == 0x98a54a4243c28e7eULL);
}

TEST_CASE("enum names round-trip", "[ir]") {
    for (auto v : {CellType::U8, CellType::F32}) CHECK(aether::core::parseCellType(aether::core::toString(v)) == v);
    for (auto v : {Boundary::Wrap, Boundary::Zero, Boundary::Mirror}) CHECK(parseBoundary(toString(v)) == v);
    for (auto v : {Kind::OuterTotalistic, Kind::Totalistic, Kind::NonTotalistic,
                   Kind::Expression, Kind::Continuous}) CHECK(parseKind(toString(v)) == v);
    for (auto v : {NeighbourhoodType::Moore, NeighbourhoodType::VonNeumann})
        CHECK(parseNeighbourhoodType(toString(v)) == v);
    CHECK_FALSE(parseKind("hexagonal").has_value());
}

TEST_CASE("hexagonal neighbourhoods are 2D only", "[ir][hex]") {
    auto ir = life();
    ir.neighbourhood = {NeighbourhoodType::Hexagonal, 1};
    std::get<Table>(ir.transition).entries.assign(14, 0);   // 2 * (6 + 1)
    CHECK(isValid(ir));
    ir.dimensions = 3;
    CHECK(hasDiagnostic(validate(ir), "1D and 2D lattices only"));
    CHECK(parseNeighbourhoodType("hex") == NeighbourhoodType::Hexagonal);
    CHECK(parseNeighbourhoodType(toString(NeighbourhoodType::Hexagonal)) == NeighbourhoodType::Hexagonal);
}
