// The abiotic resource field, the IR half (F-032, D-024).
//
// One declared field may be marked as the resource and another as its per-site
// capacity. What this file covers is what the IR says about that: the pair is
// declared, both are quantities, a capacity is not something a rule writes, the
// hash and the JSON carry it, Lua can author it, and a rule that declares no
// resource is exactly the rule it was before any of this existed.
//
// The *rate* is deliberately not here. Regeneration rate, diffusion and minimum
// seed are run-time controls in sim::Simulation, not rule text — a constant here
// would make the primary control of the feature the one thing that cannot be
// adjusted without recompiling (D-024). A test that found a rate in the IR would
// be reporting that decision had been reversed.

#include "rule/compile.hpp"
#include "rule/ir.hpp"
#include "rule/ir_json.hpp"
#include "rule/lua.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

using namespace aether;
using namespace aether::rule;

namespace {

// Two f32 fields and a transition that reads neither, so the resource pairing is
// what is under test and nothing else.
RuleIR base() {
    RuleIR ir;
    ir.states = 2;
    ir.kind = Kind::Expression;
    ir.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression e;
    e.nodes = {{ExprOp::Self}};
    ir.transition = e;

    Field store;
    store.name = "resource";
    store.cell_type = CellType::F32;
    // The draw-down: a live cell takes a tenth of what is here.
    Expression draw;
    draw.nodes = {{ExprOp::FieldSelf, 0},
                  {ExprOp::FloatLiteral, 0, 0, 0, 0, 0.9f},
                  {ExprOp::Mul, 0, 1},
                  {ExprOp::Self},
                  {ExprOp::IntLiteral, 0, 0, 0, 1},
                  {ExprOp::Eq, 3, 4},
                  {ExprOp::FieldSelf, 0},
                  {ExprOp::Select, 5, 2, 6}};
    store.write = draw;

    Field capacity;
    capacity.name = "capacity";
    capacity.cell_type = CellType::F32;   // no write: the world's shape

    ir.fields = {store, capacity};
    ir.resource = Resource{0, 1};
    return ir;
}

std::string firstProblem(const RuleIR& ir) {
    const auto d = validate(ir);
    return d.empty() ? "" : d.front().message;
}

}  // namespace

TEST_CASE("a resource names two of the rule's own fields", "[resource]") {
    const RuleIR ir = base();
    CHECK(firstProblem(ir).empty());
    REQUIRE(ir.resource.has_value());
    CHECK(ir.resource->field == 0);
    CHECK(ir.resource->capacity == 1);
    // D-022 still decides the backend: a rule with fields is codegen.
    auto compiled = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(compiled));
    const CompiledRule& c = std::get<CompiledRule>(compiled);
    CHECK(c.backend == Backend::Codegen);
    REQUIRE(c.resource.has_value());
    CHECK(c.resource->capacity == 1);
}

TEST_CASE("a resource must be a pair of quantities the rule declares", "[resource]") {
    SECTION("an index past the end") {
        RuleIR ir = base();
        ir.resource = Resource{0, 7};
        CHECK(firstProblem(ir).find("declares 2") != std::string::npos);
    }
    SECTION("the same field twice") {
        RuleIR ir = base();
        ir.resource = Resource{1, 1};
        CHECK(firstProblem(ir).find("same field") != std::string::npos);
    }
    SECTION("a u8 resource") {
        // A resource is a quantity. Eight bits of it would make every rate a
        // rounding decision.
        RuleIR ir = base();
        ir.fields[0].cell_type = CellType::U8;
        ir.fields[0].write.reset();
        CHECK(firstProblem(ir).find("must be f32") != std::string::npos);
    }
    SECTION("a u8 capacity") {
        RuleIR ir = base();
        ir.fields[1].cell_type = CellType::U8;
        CHECK(firstProblem(ir).find("must be f32") != std::string::npos);
    }
    SECTION("a capacity the rule writes") {
        // A lineage that could raise its own ceiling is AV-018 with the leak in
        // the open, so this is refused rather than merely discouraged.
        RuleIR ir = base();
        Expression raise;
        raise.nodes = {{ExprOp::FieldSelf, 1},
                       {ExprOp::FloatLiteral, 0, 0, 0, 0, 2.0f},
                       {ExprOp::Mul, 0, 1}};
        ir.fields[1].write = raise;
        CHECK(firstProblem(ir).find("world's shape") != std::string::npos);
    }
}

TEST_CASE("the resource is in the hash and a rule without one is unmoved", "[resource]") {
    RuleIR ir = base();
    const uint64_t withResource = irHash(ir);

    ir.resource.reset();
    const uint64_t without = irHash(ir);
    CHECK(withResource != without);

    // Pointing it at a different pair is a different rule.
    ir.resource = Resource{1, 0};
    CHECK(irHash(ir) != withResource);

    // And the whole of the additive claim: a rule that declares no field and no
    // resource hashes exactly what it hashed before either existed. The pinned
    // pre-F-031 value lives in ir_test.cpp; this checks the mechanism, which is
    // that an absent resource contributes no bytes.
    RuleIR plain;
    plain.states = 2;
    plain.kind = Kind::Expression;
    plain.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression self;
    self.nodes = {{ExprOp::Self}};
    plain.transition = self;
    const uint64_t before = irHash(plain);
    plain.resource.reset();
    CHECK(irHash(plain) == before);
}

TEST_CASE("the resource round-trips through JSON by name", "[resource]") {
    const RuleIR ir = base();
    const nlohmann::json j = irToJson(ir);
    // Named rather than indexed, so the file says which fields these are.
    REQUIRE(j.contains("resource"));
    CHECK(j["resource"]["field"] == "resource");
    CHECK(j["resource"]["capacity"] == "capacity");

    auto back = irFromJson(j);
    REQUIRE(std::holds_alternative<RuleIR>(back));
    const RuleIR& read = std::get<RuleIR>(back);
    REQUIRE(read.resource.has_value());
    CHECK(read.resource->field == 0);
    CHECK(read.resource->capacity == 1);
    CHECK(irHash(read) == irHash(ir));

    // A name that is not there is refused rather than read as field zero.
    nlohmann::json bad = j;
    bad["resource"]["capacity"] = "nothing";
    auto refused = irFromJson(bad);
    REQUIRE(std::holds_alternative<std::string>(refused));
    CHECK(std::get<std::string>(refused).find("nothing") != std::string::npos);

    // And a rule without a resource writes no key at all.
    RuleIR plain = ir;
    plain.resource.reset();
    CHECK_FALSE(irToJson(plain).contains("resource"));
}

TEST_CASE("Lua declares a resource", "[resource][lua]") {
    auto compiled = compileLua(R"(
        local e = expr
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            fields = {
                { name = "food", cell_type = "f32",
                  write = e.select(e.eq(e.self(), e.int(1)),
                                   e.mul(e.field("food"), e.float(0.75)),
                                   e.field("food")) },
                { name = "fertility", cell_type = "f32" },
            },
            resource = { field = "food", capacity = "fertility" },
            transition = e.select(e.gt(e.field("food"), e.float(0.5)), e.int(1), e.self()),
        }
    )", LuaContext{});
    if (const auto* e = std::get_if<LuaError>(&compiled)) FAIL(e->message);
    const RuleIR& ir = std::get<RuleIR>(compiled);
    REQUIRE(ir.resource.has_value());
    CHECK(ir.fields[ir.resource->field].name == "food");
    CHECK(ir.fields[ir.resource->capacity].name == "fertility");
    CHECK(isValid(ir));

    auto fails = [](const char* src) {
        auto r = compileLua(src, LuaContext{});
        REQUIRE(std::holds_alternative<LuaError>(r));
        return std::get<LuaError>(r).message;
    };
    CHECK(fails(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 fields = { { name = "food", cell_type = "f32" } },
                 resource = { field = "food", capacity = "missing" },
                 transition = expr.self() }
    )").find("missing") != std::string::npos);
    CHECK(fails(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 fields = { { name = "food", cell_type = "f32" } },
                 resource = { field = "food" },
                 transition = expr.self() }
    )").find("capacity") != std::string::npos);
}
