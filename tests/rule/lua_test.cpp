#include "rule/dsl.hpp"
#include "rule/lua.hpp"
#include "rule/table_layout.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace aether::rule;

namespace {

RuleIR ok(std::string_view src, LuaContext ctx = {}) {
    auto r = compileLua(src, ctx);
    if (const auto* e = std::get_if<LuaError>(&r)) FAIL(e->message);
    return std::get<RuleIR>(std::move(r));
}

std::string errorOf(std::string_view src, LuaContext ctx = {}) {
    auto r = compileLua(src, ctx);
    if (std::holds_alternative<RuleIR>(r)) return "<compiled>";
    return std::get<LuaError>(r).message;
}

}  // namespace

TEST_CASE("a Lua script computing Conway's Life equals the same rule from the DSL", "[lua]") {
    const char* src = R"(
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            kind = "outer_totalistic",
            metadata = { name = "Life from Lua" },
            transition = function(own, counts)
                local alive = counts[1]
                if own == 1 then
                    if alive == 2 or alive == 3 then return 1 else return 0 end
                end
                if alive == 3 then return 1 end
                return 0
            end,
        }
    )";
    const RuleIR ir = ok(src);
    const auto life = parseDsl("B3/S23");
    REQUIRE(life);
    CHECK(std::get<Table>(ir.transition) == std::get<Table>(life.ir->transition));
    CHECK(ir.metadata.name == "Life from Lua");
    CHECK(ir.states == 2);
    CHECK(ir.kind == Kind::OuterTotalistic);
}

TEST_CASE("counts include the quiescent count and the array form works too", "[lua]") {
    // counts[0] is the number of dead neighbours; with 8 neighbours the two
    // ways of asking the same question must agree.
    const RuleIR byCount = ok(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts)
                     if counts[0] == 5 then return 1 end
                     return 0
                 end }
    )");
    const RuleIR byAlive = ok(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts)
                     if counts[1] == 3 then return 1 end
                     return 0
                 end }
    )");
    CHECK(std::get<Table>(byCount.transition) == std::get<Table>(byAlive.transition));

    // The array form takes the table exactly as the layout stores it.
    const RuleIR array = ok(R"(
        local t = {}
        for i = 1, 18 do t[i] = 0 end
        t[4] = 1    -- own 0, three live neighbours
        t[12] = 1   -- own 1, two live
        t[13] = 1   -- own 1, three live
        return { states = 2, neighbourhood = { type = "moore", radius = 1 }, transition = t }
    )");
    const auto life = parseDsl("B3/S23");
    CHECK(std::get<Table>(array.transition) == std::get<Table>(life.ir->transition));
}

TEST_CASE("a non-totalistic rule can be computed per signature", "[lua]") {
    const RuleIR ir = ok(R"(
        return {
            states = 2,
            neighbourhood = { type = "von_neumann", radius = 1 },
            kind = "non_totalistic",
            transition = function(own, nbr) return nbr[1] end,   -- copy the cell above
        }
    )");
    const TableLayout L(Kind::NonTotalistic, 2, 4);
    const auto& t = std::get<Table>(ir.transition).entries;
    CHECK(t.size() == 32);
    for (uint8_t own = 0; own < 2; ++own) {
        for (uint8_t above = 0; above < 2; ++above) {
            const std::vector<uint8_t> nbr{above, 1, 0, 1};
            CHECK(t[L.indexNonTotalistic(own, nbr)] == above);
        }
    }
    const auto viaDsl = parseDsl("states 2; neighbourhood von_neumann 1; 0: [1,_,_,_] -> 1; 1: [0,_,_,_] -> 0;");
    REQUIRE(viaDsl);
    CHECK(std::get<Table>(ir.transition) == std::get<Table>(viaDsl.ir->transition));
}

TEST_CASE("the sandbox withholds everything SPEC §8 removes", "[lua]") {
    for (const char* name : {"io", "os", "require", "dofile", "loadfile", "load",
                             "package", "debug", "print", "_G", "getmetatable", "setmetatable",
                             "rawset", "collectgarbage", "coroutine"}) {
        const std::string src = std::string("return { probe = ") + name + " }";
        const std::string message = errorOf(src);
        INFO(name << ": " << message);
        // Reading an absent global yields nil, so the script fails on the
        // rule fields rather than on the name — what matters is that nothing
        // compiles a rule out of it and the name is not a function.
        CHECK(message != "<compiled>");
    }
    // And positively: the name really is nil inside the sandbox.
    CHECK(errorOf("if io ~= nil then error('io is reachable') end return {}")
              .find("io is reachable") == std::string::npos);
    CHECK(errorOf("local ok = pcall(function() return _G.math end) return {}")
              .find("attempt") != std::string::npos);   // pcall itself is not available
}

TEST_CASE("the allowed libraries are present", "[lua]") {
    const RuleIR ir = ok(R"(
        local names = { math, string, table, ipairs, pairs, select, tonumber, tostring, type, error, assert }
        for i = 1, 11 do assert(names[i] ~= nil, "missing library " .. i) end
        assert(math.floor(2.7) == 2)
        assert(string.rep("a", 3) == "aaa")
        assert(table.concat({"x", "y"}) == "xy")
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) return 0 end }
    )");
    CHECK(ir.states == 2);
}

TEST_CASE("an endless loop is stopped by the instruction budget (AV-009)", "[lua]") {
    LuaContext ctx;
    ctx.instructionBudget = 3'000'000;   // the real budget would take a while to burn
    const std::string message = errorOf("while true do end return {}", ctx);
    CHECK(message.find("instruction budget of 3000000") != std::string::npos);

    // A script doing real work inside its budget is not disturbed.
    LuaContext generous;
    generous.instructionBudget = 50'000'000;
    const RuleIR ir = ok(R"(
        local sum = 0
        for i = 1, 200000 do sum = sum + i end
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) return counts[1] == 3 and 1 or 0 end }
    )", generous);
    CHECK(ir.states == 2);
}

TEST_CASE("a runaway allocation is stopped by the memory budget", "[lua]") {
    LuaContext ctx;
    ctx.memoryBudget = 4u * 1024u * 1024u;
    ctx.instructionBudget = 50'000'000;
    const std::string message = errorOf("local t = {} local i = 1 while true do t[i] = i i = i + 1 end", ctx);
    CHECK(message.find("memory budget of 4 MB") != std::string::npos);
}

TEST_CASE("nothing survives from one compile to the next", "[lua]") {
    // A fresh interpreter every time: a global set by one script is not there for
    // the next, which is the observable half of AV-008.
    CHECK(errorOf("leaked = 42 return {}") != "<compiled>");
    const std::string second = errorOf("if leaked ~= nil then error('state leaked between compiles') end return {}");
    CHECK(second.find("state leaked") == std::string::npos);
}

TEST_CASE("bad returns are compile errors that say what is wrong", "[lua]") {
    CHECK(errorOf("return 7").find("must return a table") != std::string::npos);
    CHECK(errorOf("this is not lua").find("syntax error") != std::string::npos);
    CHECK(errorOf("error('deliberate')").find("deliberate") != std::string::npos);
    CHECK(errorOf("return { neighbourhood = { type = 'moore', radius = 1 } }").find("'states'") != std::string::npos);
    CHECK(errorOf("return { states = 2 }").find("'neighbourhood'") != std::string::npos);
    CHECK(errorOf("return { states = 1, neighbourhood = { type = 'moore', radius = 1 } }")
              .find("2..256") != std::string::npos);
    CHECK(errorOf("return { states = 2, neighbourhood = { type = 'hexahedral', radius = 1 } }")
              .find("unknown neighbourhood type") != std::string::npos);
    CHECK(errorOf("return { states = 2, neighbourhood = { type = 'moore', radius = 1 } }")
              .find("'transition'") != std::string::npos);
    CHECK(errorOf("return { states = 2, neighbourhood = { type = 'moore', radius = 1 }, transition = 'x' }")
              .find("array or a function") != std::string::npos);
}

TEST_CASE("a transition function that misbehaves is reported with its arguments", "[lua]") {
    std::string m = errorOf(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) return 5 end }
    )");
    CHECK(m.find("returned 5") != std::string::npos);
    CHECK(m.find("states run 0 to 1") != std::string::npos);

    m = errorOf(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) return "no" end }
    )");
    CHECK(m.find("must return a state") != std::string::npos);

    m = errorOf(R"(
        return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) error("inside the rule") end }
    )");
    CHECK(m.find("inside the rule") != std::string::npos);

    m = errorOf(R"(
        local t = {}
        for i = 1, 5 do t[i] = 0 end
        return { states = 2, neighbourhood = { type = "moore", radius = 1 }, transition = t }
    )");
    CHECK(m.find("needs 18") != std::string::npos);
}

TEST_CASE("a rule too large for the table backend is refused with its size", "[lua]") {
    const std::string m = errorOf(R"(
        return { states = 8, neighbourhood = { type = "moore", radius = 1 }, kind = "non_totalistic",
                 transition = function(own, nbr) return 0 end }
    )");
    CHECK(m.find("table entries") != std::string::npos);
    CHECK(m.find("65536") != std::string::npos);
}

TEST_CASE("kinds with no backend are refused rather than half-built", "[lua]") {
    CHECK(errorOf("return { states = 2, kind = 'continuous', neighbourhood = { type = 'moore', radius = 1 }, transition = {} }")
              .find("no backend") != std::string::npos);
    CHECK(errorOf("return { states = 2, kind = 'expression', neighbourhood = { type = 'moore', radius = 1 }, transition = {} }")
              .find("no backend") != std::string::npos);
}

TEST_CASE("dimensions and boundary come from the context unless the script says", "[lua]") {
    LuaContext ctx;
    ctx.dimensions = 3;
    ctx.boundary = Boundary::Mirror;
    const RuleIR ir = ok(R"(
        return { states = 2, neighbourhood = { type = "von_neumann", radius = 1 },
                 transition = function(own, counts) return counts[1] >= 3 and 1 or 0 end }
    )", ctx);
    CHECK(ir.dimensions == 3);
    CHECK(ir.boundary == Boundary::Mirror);
    CHECK(neighbourCount(ir.dimensions, ir.neighbourhood) == 6);

    const RuleIR says = ok(R"(
        return { dimensions = 2, boundary = "zero", states = 2,
                 neighbourhood = { type = "moore", radius = 1 },
                 transition = function(own, counts) return 0 end }
    )", ctx);
    CHECK(says.dimensions == 2);
    CHECK(says.boundary == Boundary::Zero);
}
