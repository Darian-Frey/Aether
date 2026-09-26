// Multi-field grids, the IR half (F-031, D-022).
//
// A site may carry auxiliary fields beyond the state, each its own cell type,
// read through two new operators and written by an expression apiece. This
// file covers what the IR says about them: that they are declared, typed,
// bounded, hashed, serialised, and that a rule which declares none is exactly
// the rule it was before any of this existed.

#include "rule/ir.hpp"
#include "rule/ir_json.hpp"

#include <nlohmann/json.hpp>
#include "rule/compile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace aether;
using namespace aether::rule;

namespace {

// A two-state Moore rule whose state expression is `self`, so the transition
// is valid and uninteresting and the fields are what is under test.
RuleIR base() {
    RuleIR ir;
    ir.states = 2;
    ir.kind = Kind::Expression;
    ir.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression e;
    e.nodes = {{ExprOp::Self}};
    ir.transition = e;
    return ir;
}

std::string firstProblem(const RuleIR& ir) {
    const auto d = validate(ir);
    return d.empty() ? std::string{} : d.front().message;
}

}  // namespace

TEST_CASE("an empty field list hashes nothing at all", "[fields]") {
    // The additive claim of D-022. That the hash a rule had *before* F-031 is
    // the hash it has now is proved by `ir_test.cpp`, which pins Life's at
    // 0x98a54a4243c28e7e and was written before fields existed; repeating a
    // constant here would only be a second place to get it wrong.
    //
    // What this checks is the mechanism behind that: the empty list
    // contributes no bytes, so a rule that gains a field and loses it again
    // comes back to exactly where it started.
    const RuleIR plain = base();
    const uint64_t before = irHash(plain);

    RuleIR ir = plain;
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    CHECK(irHash(ir) != before);

    ir.fields.clear();
    CHECK(irHash(ir) == before);
}

TEST_CASE("a declared field can be read at this site and at a neighbour", "[fields]") {
    RuleIR ir = base();
    ir.fields.push_back({"energy", core::CellType::F32, std::nullopt});

    Expression e;
    e.nodes = {
        {ExprOp::FieldSelf, 0},          // 0  energy here
        {ExprOp::FieldNeighbour, 0, 3},  // 1  energy at neighbour 3
        {ExprOp::Add, 0, 1},             // 2
    };
    ir.fields[0].write = e;
    CHECK(firstProblem(ir).empty());

    const auto types = expressionTypes(e, 8, 2, false, std::vector<core::CellType>{core::CellType::F32});
    REQUIRE(types.size() == 3);
    CHECK(types[0] == ExprType::Float);
    CHECK(types[1] == ExprType::Float);
    CHECK(types[2] == ExprType::Float);
}

TEST_CASE("a field's cell type decides what reading it produces", "[fields]") {
    const std::vector<core::CellType> u8{core::CellType::U8};
    Expression e;
    e.nodes = {{ExprOp::FieldSelf, 0}};
    CHECK(expressionTypes(e, 8, 2, false, u8)[0] == ExprType::Int);

    const std::vector<core::CellType> f32{core::CellType::F32};
    CHECK(expressionTypes(e, 8, 2, false, f32)[0] == ExprType::Float);
}

TEST_CASE("reading a field that was never declared is refused", "[fields]") {
    RuleIR ir = base();
    Expression e;
    e.nodes = {{ExprOp::FieldSelf, 0}, {ExprOp::Self}, {ExprOp::Add, 0, 1}};
    ir.transition = e;
    // No fields declared at all.
    CHECK(firstProblem(ir).find("field 0 is not declared") != std::string::npos);

    // One declared, two read.
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    Expression far;
    far.nodes = {{ExprOp::FieldSelf, 1}};
    ir.fields[0].write = far;
    CHECK(firstProblem(ir).find("field 1 is not declared") != std::string::npos);
}

TEST_CASE("a field neighbour index is bounded like any other", "[fields]") {
    RuleIR ir = base();
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    Expression e;
    e.nodes = {{ExprOp::FieldNeighbour, 0, 99}};
    ir.fields[0].write = e;
    CHECK(firstProblem(ir).find("neighbour index 99 out of range") != std::string::npos);
}

TEST_CASE("a field's expression must produce its own cell type", "[fields]") {
    RuleIR ir = base();
    Expression intExpr;
    intExpr.nodes = {{ExprOp::IntLiteral, 0, 0, 0, 3}};

    ir.fields.push_back({"energy", core::CellType::F32, intExpr});
    CHECK(firstProblem(ir).find("must produce a float") != std::string::npos);

    ir.fields[0].cell_type = core::CellType::U8;
    CHECK(firstProblem(ir).empty());

    Expression floatExpr;
    floatExpr.nodes = {{ExprOp::FloatLiteral, 0, 0, 0, 0, 0.5f}};
    ir.fields[0].write = floatExpr;
    CHECK(firstProblem(ir).find("must produce an integer") != std::string::npos);
}

TEST_CASE("fields are named, and named once", "[fields]") {
    RuleIR ir = base();
    ir.fields.push_back({"", core::CellType::U8, std::nullopt});
    CHECK(firstProblem(ir).find("has no name") != std::string::npos);

    ir.fields[0].name = "age";
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    CHECK(firstProblem(ir).find("two fields are named 'age'") != std::string::npos);
}

TEST_CASE("a field may read a field declared after it", "[fields]") {
    // The fields of a site are simultaneous. Making declaration order decide
    // what a field can see would be an accident of how it was typed.
    RuleIR ir = base();
    Expression readsSecond;
    readsSecond.nodes = {{ExprOp::FieldSelf, 1}};
    ir.fields.push_back({"first", core::CellType::U8, readsSecond});
    ir.fields.push_back({"second", core::CellType::U8, std::nullopt});
    CHECK(firstProblem(ir).empty());
}

TEST_CASE("a field a rule does not write is allowed, and keeps its value", "[fields]") {
    // No `write` at all: the resource of F-032 seen by a rule that reads it
    // without consuming it.
    RuleIR ir = base();
    ir.fields.push_back({"resource", core::CellType::F32, std::nullopt});
    CHECK(firstProblem(ir).empty());
    CHECK_FALSE(ir.fields[0].write.has_value());
}

TEST_CASE("declaring a field moves the hash, and declaring none does not", "[fields]") {
    const RuleIR plain = base();
    RuleIR withField = base();
    withField.fields.push_back({"age", core::CellType::U8, std::nullopt});
    CHECK(irHash(plain) != irHash(withField));

    RuleIR renamed = withField;
    renamed.fields[0].name = "years";
    CHECK(irHash(withField) != irHash(renamed));

    RuleIR retyped = withField;
    retyped.fields[0].cell_type = core::CellType::F32;
    CHECK(irHash(withField) != irHash(retyped));
}

TEST_CASE("a multi-field rule round-trips through JSON", "[fields]") {
    RuleIR ir = base();
    Expression add;
    add.nodes = {{ExprOp::FieldSelf, 0}, {ExprOp::FloatLiteral, 0, 0, 0, 0, 0.25f}, {ExprOp::Add, 0, 1}};
    ir.fields.push_back({"energy", core::CellType::F32, add});
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    REQUIRE(firstProblem(ir).empty());

    const auto text = irToJson(ir);
    auto back = irFromJson(text);
    if (const auto* e = std::get_if<std::string>(&back)) FAIL(*e);
    const RuleIR& got = std::get<RuleIR>(back);

    REQUIRE(got.fields.size() == 2);
    CHECK(got.fields[0].name == "energy");
    CHECK(got.fields[0].cell_type == core::CellType::F32);
    REQUIRE(got.fields[0].write.has_value());
    CHECK(got.fields[1].name == "age");
    CHECK_FALSE(got.fields[1].write.has_value());
    CHECK(got == ir);
    CHECK(irHash(got) == irHash(ir));
}

TEST_CASE("a rule with fields compiles through codegen, never a table", "[fields]") {
    // D-022: the table form cannot express it, so the choice is the form's
    // rather than the threshold's.
    RuleIR ir = base();
    ir.fields.push_back({"age", core::CellType::U8, std::nullopt});
    CHECK(selectBackend(ir) == Backend::Codegen);

    RuleIR tiny;                       // would otherwise be a table
    tiny.states = 2;
    tiny.neighbourhood = {NeighbourhoodType::VonNeumann, 1};
    tiny.transition = Table{std::vector<uint8_t>(10, 0)};
    CHECK(selectBackend(tiny) == Backend::Lut);
    tiny.fields.push_back({"age", core::CellType::U8, std::nullopt});
    CHECK(selectBackend(tiny) == Backend::Codegen);
}
