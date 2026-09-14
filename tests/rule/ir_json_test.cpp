#include "rule/dsl.hpp"
#include "rule/ir_json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace aether::rule;

TEST_CASE("base64 round-trips including every padding case", "[json]") {
    for (size_t n = 0; n < 10; ++n) {
        std::vector<uint8_t> bytes(n);
        for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<uint8_t>(i * 37 + 11);
        const auto back = base64Decode(base64Encode(bytes));
        REQUIRE(std::holds_alternative<std::vector<uint8_t>>(back));
        CHECK(std::get<std::vector<uint8_t>>(back) == bytes);
    }
    CHECK(base64Encode({}) == "");
    CHECK(base64Encode({'M', 'a', 'n'}) == "TWFu");
    CHECK(base64Encode({'M', 'a'}) == "TWE=");
    CHECK(std::holds_alternative<std::string>(base64Decode("TWF")));
    CHECK(std::holds_alternative<std::string>(base64Decode("TW$u")));
}

TEST_CASE("IR JSON round-trips a table rule, hash and all", "[json]") {
    const auto ir = *parseDsl("B2/S/C3").ir;
    const auto j = irToJson(ir);
    CHECK(j["transition"]["form"] == "table");
    CHECK(j["metadata"]["source_notation"] == "B2/S/C3");
    const auto back = irFromJson(j);
    REQUIRE(std::holds_alternative<RuleIR>(back));
    const RuleIR& r = std::get<RuleIR>(back);
    CHECK(r == ir);
    CHECK(irHash(r) == irHash(ir));
    // Through text, too.
    const auto back2 = irFromJson(nlohmann::json::parse(j.dump()));
    REQUIRE(std::holds_alternative<RuleIR>(back2));
    CHECK(std::get<RuleIR>(back2) == ir);
}

TEST_CASE("IR JSON round-trips an expression rule", "[json]") {
    const auto ir = *parseDsl("states 16; neighbourhood moore 1; 0: n(1) == 3 and n(2) == 0 -> 1; 1: n(1) < 2 -> 2;").ir;
    REQUIRE(std::holds_alternative<Expression>(ir.transition));
    const auto back = irFromJson(irToJson(ir));
    REQUIRE(std::holds_alternative<RuleIR>(back));
    CHECK(std::get<RuleIR>(back) == ir);
}

TEST_CASE("IR JSON round-trips a counted rule with its sets", "[json]") {
    const auto ir = *parseDsl("B2/S/C5").ir;
    REQUIRE(ir.kind == Kind::CountedTotalistic);
    REQUIRE(ir.counted.size() == 5);
    const auto back = irFromJson(irToJson(ir));
    REQUIRE(std::holds_alternative<RuleIR>(back));
    CHECK(std::get<RuleIR>(back) == ir);
    CHECK(irHash(std::get<RuleIR>(back)) == irHash(ir));

    // The sets are part of the rule, so changing one changes the hash.
    RuleIR altered = ir;
    altered.counted[1].set(2);
    CHECK(irHash(altered) != irHash(ir));
}

TEST_CASE("IR JSON round-trips a kernel rule", "[json]") {
    RuleIR ir;
    ir.cell_type = aether::core::CellType::F32;
    ir.kind = Kind::Continuous;
    Kernel k;
    k.shape = Kernel::Shape::Explicit;
    k.profile.assign(9, 0.125f);
    k.growth.nodes = {{ExprOp::FloatLiteral, 0, 0, 0, 0, 0.25f}};
    ir.transition = k;
    ir.metadata.author = "test";
    REQUIRE(isValid(ir));
    const auto back = irFromJson(irToJson(ir));
    REQUIRE(std::holds_alternative<RuleIR>(back));
    CHECK(std::get<RuleIR>(back) == ir);
}

TEST_CASE("IR JSON rejects malformed and invalid input with a message", "[json]") {
    auto j = irToJson(*parseDsl("B3/S23").ir);
    j["states"] = 1;
    auto r = irFromJson(j);
    REQUIRE(std::holds_alternative<std::string>(r));
    CHECK(std::get<std::string>(r).find("invalid rule") != std::string::npos);

    j = irToJson(*parseDsl("B3/S23").ir);
    j["transition"]["entries"] = "not base64!";
    CHECK(std::holds_alternative<std::string>(irFromJson(j)));

    j = irToJson(*parseDsl("B3/S23").ir);
    j.erase("kind");
    r = irFromJson(j);
    REQUIRE(std::holds_alternative<std::string>(r));
    CHECK(std::get<std::string>(r).find("malformed") != std::string::npos);

    j = irToJson(*parseDsl("B3/S23").ir);
    j["ir_version"] = 99;
    CHECK(std::holds_alternative<std::string>(irFromJson(j)));

    CHECK(std::holds_alternative<std::string>(irFromJson(nlohmann::json::array())));
}
