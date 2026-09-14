#include "rule/dsl.hpp"
#include "rule/library.hpp"
#include "rule/lua.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <format>
#include <set>
#include <variant>
#include <string>

using namespace aether::rule;

namespace {

// Compiles a library rule through whichever front end its extension names.
std::variant<RuleIR, std::string> compile(const LibraryRule& rule) {
    if (rule.isLua) {
        LuaContext ctx;
        ctx.dimensions = rule.dimensions;
        auto r = compileLua(rule.source, ctx);
        if (const auto* e = std::get_if<LuaError>(&r)) return e->message;
        return std::get<RuleIR>(std::move(r));
    }
    DslContext ctx;
    ctx.dimensions = rule.dimensions;
    auto r = parseDsl(rule.source, ctx);
    if (!r) return std::format("{}:{}: {}", r.error->line, r.error->column, r.error->message);
    return *r.ir;
}

}  // namespace

TEST_CASE("a rule file's header is read and its body left intact", "[library]") {
    const std::string text =
        "# name: Conway's Life\n"
        "# description: The rule the field is named after.\n"
        "# palette: 1 = #ECF0EE  2 = #C2712Eff\n"
        "B3/S23\n";
    const LibraryRule r = parseRuleFile("life", text, false);
    CHECK(r.id == "life");
    CHECK(r.name == "Conway's Life");
    CHECK(r.description == "The rule the field is named after.");
    CHECK(r.dimensions == 2);
    CHECK_FALSE(r.isLua);
    REQUIRE(r.palette.size() == 2);
    CHECK(r.palette[0].state == 1);
    CHECK(r.palette[0].rgba == std::array<uint8_t, 4>{0xEC, 0xF0, 0xEE, 255});
    CHECK(r.palette[1].state == 2);
    CHECK(r.palette[1].rgba == std::array<uint8_t, 4>{0xC2, 0x71, 0x2E, 0xFF});
    // The header is a comment in the rule's own language, so the source is
    // handed to the front end whole.
    CHECK(r.source == text);
    const auto ir = parseDsl(r.source);
    REQUIRE(ir);
    CHECK(ir.ir->states == 2);
}

TEST_CASE("Lua headers use Lua's comment marker", "[library]") {
    const std::string text = "-- name: Cyclic\n-- dimensions: 3\nreturn {}\n";
    const LibraryRule r = parseRuleFile("cyclic", text, true);
    CHECK(r.name == "Cyclic");
    CHECK(r.dimensions == 3);
    CHECK(r.isLua);
}

TEST_CASE("a file with no header still loads under its own name", "[library]") {
    const LibraryRule r = parseRuleFile("b3s23", "B3/S23\n", false);
    CHECK(r.name == "b3s23");
    CHECK(r.description.empty());
    CHECK(r.palette.empty());
}

TEST_CASE("the header stops at the first line of rule text", "[library]") {
    // A comment further down is part of the rule, not the header.
    const std::string text = "# name: Stopper\nB3/S23\n# description: not a header line\n";
    const LibraryRule r = parseRuleFile("stopper", text, false);
    CHECK(r.name == "Stopper");
    CHECK(r.description.empty());
}

TEST_CASE("malformed palette entries are ignored rather than guessed at", "[library]") {
    const LibraryRule r = parseRuleFile("x", "# palette: 1 = ECF0EE 2 = #GGG\nB3/S23\n", false);
    CHECK(r.palette.empty());
    const LibraryRule ok = parseRuleFile("y", "# palette: 3 = #010203\nB3/S23\n", false);
    REQUIRE(ok.palette.size() == 1);
    CHECK(ok.palette[0].rgba == std::array<uint8_t, 4>{1, 2, 3, 255});
}

TEST_CASE("every bundled rule loads, compiles and validates", "[library]") {
    const auto rules = loadLibrary({AETHER_RULES_DIR});
    REQUIRE(rules.size() >= 14);

    std::set<std::string> ids;
    for (const LibraryRule& rule : rules) {
        INFO("rule " << rule.id << " (" << rule.path << ")");
        CHECK(ids.insert(rule.id).second);          // ids are unique
        CHECK_FALSE(rule.name.empty());
        CHECK_FALSE(rule.description.empty());      // each carries its one-line description
        auto ir = compile(rule);
        if (const auto* e = std::get_if<std::string>(&ir)) FAIL(*e);
        const RuleIR& compiled = std::get<RuleIR>(ir);
        CHECK(isValid(compiled));
        CHECK(compiled.dimensions == rule.dimensions);
        for (const PaletteOverride& p : rule.palette) CHECK(p.state < compiled.states);
    }

    // The families the library is meant to cover.
    CHECK(ids.count("life"));
    CHECK(ids.count("brians-brain"));
    CHECK(ids.count("wireworld"));
    CHECK(ids.count("cyclic-8"));
    CHECK(ids.count("cyclic-14"));
    CHECK(ids.count("hex-life"));
    CHECK(ids.count("life-3d-4555"));
    CHECK(ids.count("fading-life"));
}

TEST_CASE("named rules are the rules they claim to be", "[library]") {
    const auto rules = loadLibrary({AETHER_RULES_DIR});
    auto find = [&](const std::string& id) {
        for (const auto& r : rules) if (r.id == id) return r;
        FAIL("no rule " + id);
        return rules.front();
    };
    CHECK(std::get<RuleIR>(compile(find("life"))) == *parseDsl("B3/S23").ir);
    CHECK(std::get<RuleIR>(compile(find("brians-brain"))) == *parseDsl("B2/S/C3").ir);

    const RuleIR hex = std::get<RuleIR>(compile(find("hex-life")));
    CHECK(hex.neighbourhood.type == NeighbourhoodType::Hexagonal);

    const RuleIR fading = std::get<RuleIR>(compile(find("fading-life")));
    CHECK(fading.metadata.decay_from == 2);
    CHECK(fading.states == 8);

    const RuleIR cyclic = std::get<RuleIR>(compile(find("cyclic-8")));
    CHECK(cyclic.states == 8);
    CHECK(cyclic.kind == Kind::CountedTotalistic);
    CHECK(std::get<Table>(cyclic.transition).entries.size() == 8 * 9);
    for (uint16_t own = 0; own < 8; ++own) CHECK(cyclic.counted[own].test((own + 1) % 8));

    // The fourteen-state cyclic rule needed 2.8M entries as a full count
    // vector and was left out of the library for it (D-016).
    const RuleIR big = std::get<RuleIR>(compile(find("cyclic-14")));
    CHECK(big.states == 14);
    CHECK(std::get<Table>(big.transition).entries.size() == 14 * 9);

    const RuleIR three = std::get<RuleIR>(compile(find("life-3d-4555")));
    CHECK(three.dimensions == 3);
    CHECK(neighbourCount(3, three.neighbourhood) == 26);
}

TEST_CASE("a rule saved to a directory comes back the same", "[library]") {
    const auto dir = (std::filesystem::temp_directory_path() / "aether_rules_test").string();
    std::filesystem::remove_all(dir);

    LibraryRule mine;
    mine.id = "mine";
    mine.name = "My Rule";
    mine.description = "Something I made.";
    mine.source = "B36/S23\n";
    REQUIRE_FALSE(saveRule(dir, mine).has_value());

    const auto back = loadLibrary({dir});
    REQUIRE(back.size() == 1);
    CHECK(back[0].id == "mine");
    CHECK(back[0].name == "My Rule");
    CHECK(back[0].description == "Something I made.");
    CHECK(std::get<RuleIR>(compile(back[0])) == *parseDsl("B36/S23").ir);

    // Saving a source that already carries a header does not double it.
    LibraryRule headed;
    headed.id = "headed";
    headed.name = "ignored";
    headed.source = "# name: Already Named\nB2/S\n";
    REQUIRE_FALSE(saveRule(dir, headed).has_value());
    const auto both = loadLibrary({dir});
    REQUIRE(both.size() == 2);
    for (const auto& r : both) {
        if (r.id == "headed") CHECK(r.name == "Already Named");
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("missing directories are skipped, and the first with rules wins", "[library]") {
    CHECK(loadLibrary({"/nonexistent/aether/rules"}).empty());
    CHECK(loadLibrary({}).empty());
    const auto rules = loadLibrary({"/nonexistent/aether/rules", AETHER_RULES_DIR});
    CHECK(rules.size() >= 14);
}
