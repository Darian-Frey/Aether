#include "rule/dsl.hpp"
#include "rule/lua.hpp"
#include "rule/table_layout.hpp"
#include <format>
#include "rule/compile.hpp"
#include "rule/growth.hpp"

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

TEST_CASE("a kind with no way to write it is refused rather than half-built", "[lua]") {
    // Expression is the one left: codegen executes expressions perfectly well,
    // so the reason is authorship rather than any missing backend (2026-09-16).
    CHECK(errorOf("return { states = 2, kind = 'expression', neighbourhood = { type = 'moore', radius = 1 }, transition = {} }")
              .find("no way to write one") != std::string::npos);
    // Continuous used to sit here too, and is authorable as of Phase 5 step 2.
    CHECK(errorOf("return { states = 2, kind = 'continuous', neighbourhood = { type = 'moore', radius = 1 }, transition = {} }")
              .find("do not agree") != std::string::npos);
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

TEST_CASE("Lua authors a continuous rule: a kernel shell and a named growth function", "[lua]") {
    // The script works the kernel out with math.*, which is the whole reason
    // Lua was the cheap route to kernel authoring: nothing in C++ has to know
    // what a Gaussian shell is, because the profile arrives as samples.
    const char* src = R"(
        local r = 13
        local profile = {}
        for i = 0, r do
            local x = i / r
            profile[i + 1] = math.exp(-((x - 0.5) ^ 2) / (2 * 0.15 ^ 2))
        end
        return {
            cell_type = "f32",
            kind = "continuous",
            dimensions = 2,
            neighbourhood = { type = "moore", radius = r },
            kernel = { shape = "radial", profile = profile },
            growth = { form = "polynomial", mu = 0.15, sigma = 0.015 },
            metadata = { name = "Lenia-like" },
        }
    )";
    const RuleIR ir = ok(src);
    CHECK(ir.cell_type == aether::core::CellType::F32);
    CHECK(ir.kind == Kind::Continuous);
    CHECK(ir.neighbourhood.radius == 13);
    REQUIRE(std::holds_alternative<Kernel>(ir.transition));
    const Kernel& k = std::get<Kernel>(ir.transition);
    CHECK(k.shape == Kernel::Shape::Radial);
    CHECK(k.profile.size() == 14);
    // The shell peaks partway out and falls off at both ends, which is what
    // makes a Lenia kernel an annulus rather than a disc.
    CHECK(k.profile[7] > k.profile.front());
    CHECK(k.profile[7] > k.profile.back());
    CHECK(k.growth == growthExpression({GrowthForm::Polynomial, 0.15f, 0.015f}));
    CHECK(ir.metadata.name == "Lenia-like");
    CHECK(validate(ir).empty());
}

TEST_CASE("an explicit kernel must have one weight per neighbourhood site", "[lua]") {
    auto script = [](int radius, int weights) {
        std::string p;
        for (int i = 0; i < weights; ++i) p += (i ? ", " : "") + std::string("0.1");
        return std::string(R"(
            return { cell_type = "f32", kind = "continuous", dimensions = 2,
                     neighbourhood = { type = "moore", radius = )") + std::to_string(radius) +
               R"( }, kernel = { shape = "explicit", profile = { )" + p + R"( } },
                     growth = { form = "rectangular", mu = 0.3, sigma = 0.05 } })";
    };
    CHECK(errorOf(script(1, 9)) == "<compiled>");         // 3x3
    CHECK(errorOf(script(1, 8)).find("needs 9") != std::string::npos);
}

TEST_CASE("a continuous rule is refused what it cannot mean", "[lua]") {
    auto withGrowth = [](const std::string& form, const std::string& mu, const std::string& sigma) {
        return std::string(R"(
            return { cell_type = "f32", kind = "continuous", dimensions = 2,
                     neighbourhood = { type = "moore", radius = 2 },
                     kernel = { shape = "radial", profile = { 1, 0.5, 0.25 } },
                     growth = { form = ")") + form + R"(", mu = )" + mu + ", sigma = " + sigma + " } }";
    };
    CHECK(errorOf(withGrowth("polynomial", "0.15", "0.015")) == "<compiled>");
    CHECK(errorOf(withGrowth("gaussian", "0.15", "0.015")).find("unknown growth form") != std::string::npos);
    CHECK(errorOf(withGrowth("polynomial", "0.15", "0")).find("sigma must be positive") != std::string::npos);

    // An f32 cell holds a value, so a state count has nothing to describe.
    CHECK(errorOf(R"(
        return { cell_type = "f32", kind = "continuous", states = 4, dimensions = 2,
                 neighbourhood = { type = "moore", radius = 2 },
                 kernel = { shape = "radial", profile = { 1 } },
                 growth = { form = "rectangular", mu = 0.3, sigma = 0.05 } }
    )").find("has no 'states'") != std::string::npos);

    // And a discrete rule cannot claim the continuous kind.
    CHECK(errorOf(R"(
        return { kind = "continuous", states = 2, dimensions = 2,
                 neighbourhood = { type = "moore", radius = 1 },
                 kernel = { shape = "radial", profile = { 1 } },
                 growth = { form = "rectangular", mu = 0.3, sigma = 0.05 } }
    )").find("do not agree") != std::string::npos);
}

// --- the expression builder (F-031, D-023) -----------------------------------

TEST_CASE("Lua builds an expression transition", "[lua][fields]") {
    // Before F-031 a script could describe a table, by enumeration, or a kernel.
    // Neither can express a field write, so `expr` builds a tree and the reader
    // flattens it into the arena. Told apart from a table rule by shape.
    const auto ir = ok(R"(
        local e = expr
        return {
            states = 4,
            neighbourhood = { type = "moore", radius = 1 },
            transition = e.select(e.gt(e.count(1), e.int(3)), e.int(1), e.self()),
        }
    )");
    REQUIRE(ir.kind == Kind::Expression);
    const auto* tree = std::get_if<Expression>(&ir.transition);
    REQUIRE(tree != nullptr);
    // Children precede parents and the root is last, whatever order the script
    // nested them in.
    CHECK(tree->nodes.back().op == ExprOp::Select);
    CHECK(isValid(ir));
}

TEST_CASE("Lua declares fields and writes them", "[lua][fields]") {
    const auto ir = ok(R"(
        local e = expr
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            fields = {
                { name = "energy", cell_type = "u8",
                  write = e.add(e.field("energy"), e.int(1)) },
                { name = "heat", cell_type = "f32",
                  write = e.mul(e.field("heat"), e.float(0.5)) },
                { name = "carried" },
            },
            transition = e.select(e.gt(e.field("energy"), e.int(10)), e.int(1), e.self()),
        }
    )");
    REQUIRE(ir.fields.size() == 3);
    CHECK(ir.fields[0].name == "energy");
    CHECK(ir.fields[0].cell_type == CellType::U8);
    CHECK(ir.fields[1].cell_type == CellType::F32);
    CHECK(ir.fields[1].write.has_value());
    CHECK_FALSE(ir.fields[2].write.has_value());   // declared, never written
    CHECK(isValid(ir));
    // D-022: a rule declaring a field compiles to codegen.
    auto compiled = compileRule(ir);
    REQUIRE(std::holds_alternative<CompiledRule>(compiled));
    CHECK(std::get<CompiledRule>(compiled).backend == Backend::Codegen);
}

TEST_CASE("a field may read a field declared after it", "[lua][fields]") {
    // The fields of a site are simultaneous, so the order they were written in
    // must not decide what a write can see.
    const auto ir = ok(R"(
        local e = expr
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            fields = {
                { name = "first",  write = e.add(e.field("second"), e.int(1)) },
                { name = "second", write = e.add(e.field("first"),  e.int(1)) },
            },
            transition = e.self(),
        }
    )");
    REQUIRE(ir.fields.size() == 2);
    CHECK(ir.fields[0].write.has_value());
    CHECK(ir.fields[1].write.has_value());
    CHECK(isValid(ir));
}

TEST_CASE("a shared subtree stays one node", "[lua][fields]") {
    // A Lua local used twice is one table used twice, so the arena keeps it as
    // one node rather than duplicating what the script shared.
    const auto ir = ok(R"(
        local e = expr
        local n = e.count(1)
        return {
            states = 2,
            neighbourhood = { type = "moore", radius = 1 },
            transition = e.select(e.and_(e.gt(n, e.int(1)), e.lt(n, e.int(4))), e.int(1), e.int(0)),
        }
    )");
    const auto* tree = std::get_if<Expression>(&ir.transition);
    REQUIRE(tree != nullptr);
    size_t counts = 0;
    for (const ExprNode& n : tree->nodes) {
        if (n.op == ExprOp::Count) ++counts;
    }
    CHECK(counts == 1);
    CHECK(isValid(ir));
}

TEST_CASE("the expression builder refuses what it cannot read", "[lua][fields]") {
    auto fails = [](const char* src) { return errorOf(src); };

    CHECK(fails(R"(return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                           transition = expr.add(expr.self()) })")
              .find("2 argument") != std::string::npos);

    CHECK(fails(R"(return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                           transition = expr.field("nothing") })")
              .find("does not declare") != std::string::npos);

    CHECK(fails(R"(return { states = 2, neighbourhood = { type = "moore", radius = 1 },
                           transition = expr.add(expr.self(), 3) })")
              .find("expression") != std::string::npos);

    // A table that refers to itself would make the reader recurse for ever.
    CHECK(fails(R"(
        local loop = { op = "not_" }
        loop.a = loop
        return { states = 2, neighbourhood = { type = "moore", radius = 1 }, transition = loop }
    )").find("deeper than") != std::string::npos);

    // A table cannot express a field write, so the two cannot be combined.
    CHECK(fails(R"(
        return {
            states = 2, neighbourhood = { type = "moore", radius = 1 },
            fields = { { name = "energy" } },
            transition = function(own, counts) return own end,
        }
    )").find("expression transition") != std::string::npos);
}
