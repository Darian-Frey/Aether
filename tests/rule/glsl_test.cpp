#include "rule/compile.hpp"
#include "rule/dsl.hpp"
#include "rule/glsl.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace aether::rule;

namespace {

RuleIR expressionRule(Expression e, uint16_t states = 2) {
    RuleIR ir;
    ir.states = states;
    ir.kind = Kind::Expression;
    ir.transition = std::move(e);
    return ir;
}

std::string generate(const RuleIR& ir) {
    auto r = generateGlsl(ir);
    if (const auto* e = std::get_if<GlslError>(&r)) FAIL(e->message);
    return std::get<std::string>(r);
}

}  // namespace

TEST_CASE("the generated function has the signature SPEC §6 names", "[glsl]") {
    Expression e;
    e.nodes = {{ExprOp::Self}};
    const std::string src = generate(expressionRule(e));
    CHECK(src.find("uint aether_rule(uint self, uint nbr[8])") != std::string::npos);
    CHECK(src.find("int t0 = int(self);") != std::string::npos);
    // Every value it returns is a state, whatever the arithmetic did.
    CHECK(src.find("return uint(clamp(t0, 0, 1));") != std::string::npos);
    // No count array when nothing counts.
    CHECK(src.find("cnt[") == std::string::npos);
}

TEST_CASE("counting neighbours emits a statically bounded loop", "[glsl]") {
    Expression e;
    e.nodes = {{ExprOp::Count, 1}};
    const std::string src = generate(expressionRule(e, 4));
    CHECK(src.find("int cnt[4];") != std::string::npos);
    CHECK(src.find("for (int i = 0; i < 8; ++i) cnt[int(nbr[i])] += 1;") != std::string::npos);
    CHECK(src.find("int t0 = cnt[1];") != std::string::npos);
    // SPEC §6: no loop may depend on data.
    CHECK(src.find("while") == std::string::npos);
}

TEST_CASE("each node becomes one statement of its own type", "[glsl]") {
    Expression e;
    e.nodes = {
        {ExprOp::Self},                            // 0 int
        {ExprOp::IntLiteral, 0, 0, 0, 3},          // 1 int
        {ExprOp::Lt, 0, 1},                        // 2 bool
        {ExprOp::Neighbour, 5},                    // 3 int
        {ExprOp::Add, 0, 3},                       // 4 int
        {ExprOp::Select, 2, 4, 1},                 // 5 int
    };
    const std::string src = generate(expressionRule(e, 8));
    CHECK(src.find("int t1 = 3;") != std::string::npos);
    CHECK(src.find("bool t2 = t0 < t1;") != std::string::npos);
    CHECK(src.find("int t3 = int(nbr[5]);") != std::string::npos);
    CHECK(src.find("int t4 = t0 + t3;") != std::string::npos);
    CHECK(src.find("int t5 = t2 ? t4 : t1;") != std::string::npos);
}

TEST_CASE("division and modulo are guarded, since a zero divisor is undefined in GLSL", "[glsl]") {
    Expression e;
    e.nodes = {
        {ExprOp::Self},                            // 0
        {ExprOp::Count, 0},                        // 1
        {ExprOp::Div, 0, 1},                       // 2
        {ExprOp::Mod, 0, 1},                       // 3
        {ExprOp::Add, 2, 3},                       // 4
    };
    const std::string src = generate(expressionRule(e, 4));
    CHECK(src.find("int t2 = (t1 == 0) ? 0 : t0 / t1;") != std::string::npos);
    CHECK(src.find("int t3 = (t1 == 0) ? 0 : t0 % t1;") != std::string::npos);
}

TEST_CASE("generation is deterministic and refuses what it cannot compile", "[glsl]") {
    const auto ir = *parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1;").ir;
    REQUIRE(ir.kind == Kind::Expression);
    CHECK(generate(ir) == generate(ir));

    // A table rule has no expression to generate from.
    auto table = generateGlsl(*parseDsl("B3/S23").ir);
    REQUIRE(std::holds_alternative<GlslError>(table));
    CHECK(std::get<GlslError>(table).message.find("not in expression form") != std::string::npos);

    // An invalid tree is refused rather than emitted.
    Expression bad;
    bad.nodes = {{ExprOp::Neighbour, 99}};
    auto refused = generateGlsl(expressionRule(bad));
    REQUIRE(std::holds_alternative<GlslError>(refused));
    CHECK(std::get<GlslError>(refused).message.find("invalid IR") != std::string::npos);

    // A tree whose root is a condition is not a rule.
    Expression boolean;
    boolean.nodes = {{ExprOp::Self}, {ExprOp::IntLiteral}, {ExprOp::Eq, 0, 1}};
    auto notAState = generateGlsl(expressionRule(boolean));
    CHECK(std::holds_alternative<GlslError>(notAState));
}
