// Per-cell genomes, the IR half (F-033, D-025).
//
// Two schema changes land here and this file is about both: a `u32` cell type
// that is for auxiliary fields and *not* for the state, and five bitwise
// operators. The genome itself is declared, validated, hashed, serialised and
// authorable in Lua; what it *means* is the rule's business and nothing here
// knows that bit 3 is "born on three neighbours".
//
// The operators' agreement rules are the part most able to drift: `Shr` is
// logical rather than arithmetic and both shifts mask their count to five bits,
// because C++ and GLSL leave a shift by 32 or more undefined. The equivalence
// sweep is what proves the two paths agree; these cases pin what they agree on.

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

// A two-state Moore rule carrying one u32 genome field and nothing else.
RuleIR base() {
    RuleIR ir;
    ir.states = 2;
    ir.kind = Kind::Expression;
    ir.neighbourhood = {NeighbourhoodType::Moore, 1};

    Field gene;
    gene.name = "genome";
    gene.cell_type = CellType::U32;   // no write: the engine owns it at birth
    ir.fields = {gene};
    ir.genome = Genome{0};

    // (genome >> count(1)) & 1 — the reference case, and the whole reason the
    // bitwise operators exist.
    Expression e;
    e.nodes = {{ExprOp::FieldSelf, 0},
               {ExprOp::Count, 1},
               {ExprOp::Shr, 0, 1},
               {ExprOp::IntLiteral, 0, 0, 0, 1},
               {ExprOp::BitAnd, 2, 3}};
    ir.transition = e;
    return ir;
}

std::string firstProblem(const RuleIR& ir) {
    const auto d = validate(ir);
    return d.empty() ? "" : d.front().message;
}

}  // namespace

TEST_CASE("u32 is a field type and not a state type", "[genome]") {
    CHECK(core::cellBytes(CellType::U32) == 4);
    CHECK(core::toString(CellType::U32) == "u32");
    CHECK(core::parseCellType("u32") == CellType::U32);

    // A field may be one.
    CHECK(firstProblem(base()).empty());

    // A state may not. What a state count would mean for four billion of them,
    // and what a palette would do with it, are questions D-025 leaves unasked.
    RuleIR ir = base();
    ir.cell_type = CellType::U32;
    CHECK(firstProblem(ir).find("u32 is for auxiliary fields") != std::string::npos);
}

TEST_CASE("a genome must be a u32 field the engine alone writes", "[genome]") {
    SECTION("an index past the end") {
        RuleIR ir = base();
        ir.genome = Genome{4};
        CHECK(firstProblem(ir).find("declares 1") != std::string::npos);
    }
    SECTION("a u8 genome") {
        RuleIR ir = base();
        ir.fields[0].cell_type = CellType::U8;
        CHECK(firstProblem(ir).find("must be u32") != std::string::npos);
    }
    SECTION("an f32 genome") {
        RuleIR ir = base();
        ir.fields[0].cell_type = CellType::F32;
        CHECK(firstProblem(ir).find("must be u32") != std::string::npos);
    }
    SECTION("a genome the rule writes") {
        // Lamarckian, in a feature whose point is that variation is inherited
        // rather than acquired — and a rule and the engine fighting for the same
        // bytes besides.
        RuleIR ir = base();
        Expression acquire;
        acquire.nodes = {{ExprOp::FieldSelf, 0},
                         {ExprOp::IntLiteral, 0, 0, 0, 1},
                         {ExprOp::BitOr, 0, 1}};
        ir.fields[0].write = acquire;
        CHECK(firstProblem(ir).find("does not acquire one") != std::string::npos);
    }
}

TEST_CASE("the bitwise operators are integer-only and named", "[genome]") {
    CHECK(toString(ExprOp::BitAnd) == "band");
    CHECK(toString(ExprOp::Shr) == "shr");

    // A float has no bits to speak of here: a rule asking to shift a convolution
    // result is a mistake worth naming rather than a conversion worth guessing.
    RuleIR ir = base();
    Expression bad;
    bad.nodes = {{ExprOp::FieldSelf, 0},
                 {ExprOp::FloatLiteral, 0, 0, 0, 0, 2.0f},
                 {ExprOp::BitAnd, 0, 1}};
    ir.transition = bad;
    CHECK(firstProblem(ir).find("bitwise operands must both be integers") != std::string::npos);
}

TEST_CASE("the genome is in the hash and a rule without one is unmoved", "[genome]") {
    RuleIR ir = base();
    const uint64_t withGenome = irHash(ir);
    ir.genome.reset();
    CHECK(irHash(ir) != withGenome);

    // The additive claim once more: an absent genome contributes no bytes, so a
    // rule that has never heard of one hashes what it always did.
    RuleIR plain;
    plain.states = 2;
    plain.kind = Kind::Expression;
    plain.neighbourhood = {NeighbourhoodType::Moore, 1};
    Expression self;
    self.nodes = {{ExprOp::Self}};
    plain.transition = self;
    const uint64_t before = irHash(plain);
    plain.genome.reset();
    CHECK(irHash(plain) == before);
}

TEST_CASE("the genome round-trips through JSON by name", "[genome]") {
    const RuleIR ir = base();
    const nlohmann::json j = irToJson(ir);
    REQUIRE(j.contains("genome"));
    CHECK(j["genome"]["field"] == "genome");
    CHECK(j["fields"][0]["cell_type"] == "u32");

    auto back = irFromJson(j);
    REQUIRE(std::holds_alternative<RuleIR>(back));
    const RuleIR& read = std::get<RuleIR>(back);
    REQUIRE(read.genome.has_value());
    CHECK(read.genome->field == 0);
    CHECK(irHash(read) == irHash(ir));

    nlohmann::json bad = j;
    bad["genome"]["field"] = "elsewhere";
    auto refused = irFromJson(bad);
    REQUIRE(std::holds_alternative<std::string>(refused));
    CHECK(std::get<std::string>(refused).find("elsewhere") != std::string::npos);

    RuleIR plain = ir;
    plain.genome.reset();
    CHECK_FALSE(irToJson(plain).contains("genome"));
}

TEST_CASE("Lua declares a genome and tests its bits", "[genome][lua]") {
    auto compiled = compileLua(R"(
        local e = expr
        -- Born or surviving on the bit the live-neighbour count selects. The
        -- survival half of the mask sits nine bits up, which is arithmetic the
        -- script does and the engine never sees.
        local n = e.count(1)
        local shift = e.select(e.eq(e.self(), e.int(1)), e.add(n, e.int(9)), n)
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            fields = { { name = "rule_bits", cell_type = "u32" } },
            genome = { field = "rule_bits" },
            transition = e.band(e.shr(e.field("rule_bits"), shift), e.int(1)),
        }
    )", LuaContext{});
    if (const auto* e = std::get_if<LuaError>(&compiled)) FAIL(e->message);
    const RuleIR& ir = std::get<RuleIR>(compiled);
    REQUIRE(ir.genome.has_value());
    CHECK(ir.fields[ir.genome->field].name == "rule_bits");
    CHECK(ir.fields[0].cell_type == CellType::U32);
    CHECK(isValid(ir));
    // D-022 still decides the backend, which is F-033's routing bullet already
    // satisfied by a decision taken earlier.
    auto built = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(built));
    CHECK(std::get<CompiledRule>(built).backend == Backend::Codegen);
    REQUIRE(std::get<CompiledRule>(built).genome.has_value());

    auto fails = [](const char* src) {
        auto r = compileLua(src, LuaContext{});
        REQUIRE(std::holds_alternative<LuaError>(r));
        return std::get<LuaError>(r).message;
    };
    CHECK(fails(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 fields = { { name = "g", cell_type = "u32" } },
                 genome = { field = "missing" },
                 transition = expr.self() }
    )").find("missing") != std::string::npos);
    CHECK(fails(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 fields = { { name = "g", cell_type = "u8" } },
                 genome = { field = "g" },
                 transition = expr.self() }
    )").find("must be u32") != std::string::npos);
}
